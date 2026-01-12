// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2024 MinIO, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MINIO_CPP_RDMA_H_INCLUDED
#define MINIO_CPP_RDMA_H_INCLUDED

#include "error.h"
#include "utils.h"
#include "credentials.h"
#include "signer.h"
#include "rdma-httplib.h"
#include "nvidia-cufile.h"
#include "nvidia-cuobjclient.h"

#define IO_DESC_STR							\
  "0102030405060708:01020304:01020304:0102:010203:1:0102030405060708090a0b0c0d0e0f10:0102030405060708:0102030405060708"

// SHA256 hash of empty string (for RDMA requests with no body)
inline constexpr const char* kEmptySha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
inline constexpr const char* kUnsignedPayload = "UNSIGNED-PAYLOAD";

// S3 RDMA Protocol Headers (AWS S3 RDMA spec)
inline constexpr const char* kAmzRDMAToken = "x-amz-rdma-token";
inline constexpr const char* kAmzRDMAReply = "x-amz-rdma-reply";
inline constexpr const char* kAmzRDMABytesTransferred = "x-amz-rdma-bytes-transferred";

// RDMA Reply Status Codes (aligned with HTTP status codes)
inline constexpr int kRDMAReplySuccess = 200;
inline constexpr int kRDMAReplyNoContent = 204;
inline constexpr int kRDMAReplyPartialContent = 206;
inline constexpr int kRDMAReplyNotImplemented = 501;

// These functions are invoked by cufile rdma layer either user shadow pages or direct gpu va address
// depending on whether nvidia-fs driver or nv peer mem is present
inline static ssize_t objectPut(const void *handle, const char* buf, size_t size, loff_t offset, const cufileRDMAInfo_t *infop)
{
  void *ctx = cuObjClient::getCtx(handle);
  s3_rdma_client_ctx_t *sctx = static_cast<s3_rdma_client_ctx_t *>(ctx);
  char io_str[sizeof IO_DESC_STR];
  unsigned io_len = sizeof io_str;

  if (infop == nullptr) {
    std::cerr << "obtained NULL descr" << std::endl;
    return -1;
  }

  const std::string descr = std::string(infop->desc_str, infop->desc_len);
  snprintf(io_str, io_len,"%s:%016lx:%016lx;",
	   infop->desc_str, (uint64_t)buf, (uint64_t)size);

  minio::utils::UtcTime date = minio::utils::UtcTime::Now();
  minio::creds::Credentials creds = sctx->provider->Fetch();
  minio::utils::Multimap query_params;  
  minio::http::Url url;
  std::string region = "us-east-1";
  
  if (sctx->uploadId != "") {
    query_params.Add("uploadId", sctx->uploadId);
    if (sctx->partNumber == 0) {
      std::cerr << "partNumber cannot be zero" << std::endl;
      return -1;
    }
    if (sctx->partNumber > 10000) {
      std::cerr << "partNumber cannot be > 10000" << std::endl;
      return -1;
    }
    query_params.Add("partNumber", std::to_string(sctx->partNumber));
  }

  if (minio::error::Error err = sctx->url.BuildUrl(url, minio::http::Method::kPut,
					     region, query_params,
					     sctx->bucket, sctx->object)) {
    std::cerr << "failed to build url. error=" << err
              << ". This should not happen" << std::endl;
    return -1;
  }

  std::string host = url.HostHeaderValue();

  // Build headers for SignV4S3
  minio::utils::Multimap sign_headers;
  sign_headers.Add("Host", host);
  sign_headers.Add("x-amz-date", date.ToAmzDate());
  sign_headers.Add("x-amz-content-sha256", kUnsignedPayload);
  sign_headers.Add(kAmzRDMAToken, io_str);
  sign_headers.Add("Content-Type", "application/octet-stream");
  sign_headers.Add("Content-Length", "0");

  // Add session token if present
  if (!creds.session_token.empty()) {
    sign_headers.Add("X-Amz-Security-Token", creds.session_token);
  }

  // Sign the request with SignV4S3 (adds Authorization header)
  minio::signer::SignV4S3(minio::http::Method::kPut, url.path, region,
                          sign_headers, query_params,
                          creds.access_key, creds.secret_key,
                          kUnsignedPayload, date);

  // Convert Multimap to httplib::Headers
  httplib::Headers headers;
  std::list<std::string> keys = sign_headers.Keys();
  for (const auto& key : keys) {
    std::list<std::string> values = sign_headers.Get(key);
    for (const auto& value : values) {
      headers.emplace(key, value);
    }
  }

  std::string path = url.path;
  std::string query_string = query_params.ToQueryString();
  std::string full_path = query_string.empty() ? path : path + "?" + query_string;

  url.path = "";
  url.query_string = "";
  httplib::Client cli(url.String());

  auto res = cli.Put(full_path, headers, "", "");
  if (res.error() != httplib::Error::Success) {
    std::cout << "Upload failed with error " << res.error() << std::endl;
    return -1;
  }

  // Check HTTP status first - 200 OK with ETag means success
  std::string etag = res->get_header_value("ETag");
  if (res->status == 200 && !etag.empty()) {
    // Success - got 200 OK with ETag, RDMA transfer completed
    sctx->etag = minio::utils::Trim(etag, '"');
    return size;
  }

  // Check x-amz-rdma-reply header per AWS S3 RDMA protocol spec
  std::string rdma_reply = res->get_header_value(kAmzRDMAReply);
  if (rdma_reply.empty() || rdma_reply == "501") {
    // RDMA declined by server - fallback needed
    std::cerr << "RDMA declined by server, fallback needed" << std::endl;
    return -2;
  }

  int reply_code = std::stoi(rdma_reply);
  if (reply_code != kRDMAReplySuccess && reply_code != kRDMAReplyNoContent) {
    std::cerr << "Unexpected RDMA reply: " << reply_code << std::endl;
    return -1;
  }

  sctx->etag = minio::utils::Trim(etag, '"');
  return size;
}

inline static ssize_t objectGet(const void *handle, char* buf, size_t size, loff_t offset, const cufileRDMAInfo_t *infop)
{
  void *ctx = cuObjClient::getCtx(handle);
  s3_rdma_client_ctx_t *sctx = static_cast<s3_rdma_client_ctx_t *>(ctx);
  char io_str[sizeof IO_DESC_STR];
  unsigned io_len = sizeof io_str;

  if (infop == nullptr) {
    std::cerr << "obtained NULL descr" << std::endl;
    return -1;
  }

  const std::string descr = std::string(infop->desc_str, infop->desc_len);
  snprintf(io_str, io_len,"%s:%016lx:%016lx;",
	   infop->desc_str, (uint64_t)buf, (uint64_t)size);

  minio::utils::UtcTime date = minio::utils::UtcTime::Now();
  minio::creds::Credentials creds = sctx->provider->Fetch();
  minio::utils::Multimap query_params;  
  minio::http::Url url;
  std::string region = "us-east-1";
  
  if (minio::error::Error err = sctx->url.BuildUrl(url, minio::http::Method::kGet,
					     region, query_params,
					     sctx->bucket, sctx->object)) {
    std::cerr << "failed to build url. error=" << err
              << ". This should not happen" << std::endl;
    return -1;
  }
  
  std::string host = url.HostHeaderValue();

  // Build headers for SignV4S3
  minio::utils::Multimap sign_headers;
  sign_headers.Add("Host", host);
  sign_headers.Add("x-amz-date", date.ToAmzDate());
  sign_headers.Add("x-amz-content-sha256", kUnsignedPayload);
  sign_headers.Add(kAmzRDMAToken, io_str);

  // Add session token if present
  if (!creds.session_token.empty()) {
    sign_headers.Add("X-Amz-Security-Token", creds.session_token);
  }

  // Sign the request with SignV4S3 (adds Authorization header)
  minio::signer::SignV4S3(minio::http::Method::kGet, url.path, region,
                          sign_headers, query_params,
                          creds.access_key, creds.secret_key,
                          kUnsignedPayload, date);

  // Convert Multimap to httplib::Headers
  httplib::Headers headers;
  std::list<std::string> hdr_keys = sign_headers.Keys();
  for (const auto& key : hdr_keys) {
    std::list<std::string> values = sign_headers.Get(key);
    for (const auto& value : values) {
      headers.emplace(key, value);
    }
  }

  std::string path = url.path;
  url.path = "";
  url.query_string = "";
  httplib::Client cli(url.String());

  auto res = cli.Get(path, headers);
  if (res.error() != httplib::Error::Success) {
    std::cerr << "Download failed with error " << res.error() << std::endl;
    return -1;
  }

  // Check x-amz-rdma-reply header per AWS S3 RDMA protocol spec
  std::string rdma_reply = res->get_header_value(kAmzRDMAReply);
  if (rdma_reply.empty() || rdma_reply == "501") {
    // RDMA declined by server - fallback needed
    std::cerr << "RDMA declined by server" << std::endl;
    return -2;
  }

  int reply_code = std::stoi(rdma_reply);
  if (reply_code != kRDMAReplySuccess && reply_code != kRDMAReplyPartialContent) {
    std::cerr << "Unexpected RDMA reply: " << reply_code << std::endl;
    return -1;
  }

  // Verify bytes transferred per spec
  std::string bytes_str = res->get_header_value(kAmzRDMABytesTransferred);
  if (!bytes_str.empty()) {
    ssize_t bytes_transferred = std::stoll(bytes_str);
    if (bytes_transferred != static_cast<ssize_t>(size)) {
      std::cerr << "RDMA bytes mismatch: expected " << size
                << ", got " << bytes_transferred << std::endl;
    }
  }

  return size;
}

#endif  // _MINIO_CPP_RDMA_H_INCLUDED
