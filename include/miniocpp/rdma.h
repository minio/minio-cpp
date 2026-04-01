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

#include "credentials.h"
#include "error.h"
#include "nvidia-cufile.h"
#include "nvidia-cuobjclient.h"
#include "rdma-httplib.h"
#include "signer.h"
#include "utils.h"

// SHA256 hash of empty string (for RDMA requests with no body)
inline constexpr const char* kEmptySha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
inline constexpr const char* kUnsignedPayload = "UNSIGNED-PAYLOAD";

// S3 RDMA Protocol Headers (AWS S3 RDMA spec)
inline constexpr const char* kAmzRDMAToken = "x-amz-rdma-token";
inline constexpr const char* kAmzRDMAReply = "x-amz-rdma-reply";
inline constexpr const char* kAmzRDMABytesTransferred =
    "x-amz-rdma-bytes-transferred";

// RDMA Reply Status Codes (aligned with HTTP status codes)
inline constexpr int kRDMAReplySuccess = 200;
inline constexpr int kRDMAReplyNoContent = 204;
inline constexpr int kRDMAReplyPartialContent = 206;
inline constexpr int kRDMAReplyNotImplemented = 501;

// Return codes for rdmaPut/rdmaGet
inline constexpr ssize_t kRDMANotSupported = -2;

inline static ssize_t rdmaPut(s3_rdma_client_ctx_t* sctx, const char* token,
                              const void* buf, size_t size) {
  char rdma_token[256];
  snprintf(rdma_token, sizeof(rdma_token), "%s:%016lx:%016lx", token,
           (uint64_t)buf, (uint64_t)size);

  minio::utils::UtcTime date = minio::utils::UtcTime::Now();
  minio::creds::Credentials creds = sctx->provider->Fetch();
  minio::utils::Multimap query_params;
  minio::http::Url url;
  const std::string& region = sctx->region;

  if (!sctx->uploadId.empty()) {
    query_params.Add("uploadId", sctx->uploadId);
    if (sctx->partNumber == 0 || sctx->partNumber > 10000) {
      return -1;
    }
    query_params.Add("partNumber", std::to_string(sctx->partNumber));
  }

  if (minio::error::Error err =
          sctx->url.BuildUrl(url, minio::http::Method::kPut, region,
                             query_params, sctx->bucket, sctx->object)) {
    return -1;
  }

  std::string host = url.HostHeaderValue();

  minio::utils::Multimap sign_headers;
  sign_headers.Add("Host", host);
  sign_headers.Add("x-amz-date", date.ToAmzDate());
  sign_headers.Add("x-amz-content-sha256", kUnsignedPayload);
  sign_headers.Add(kAmzRDMAToken, rdma_token);
  sign_headers.Add("Content-Type", "application/octet-stream");
  sign_headers.Add("Content-Length", "0");

  if (!sctx->checksum.empty()) {
    sign_headers.Add("x-amz-checksum-crc64nvme", sctx->checksum);
  }

  if (!creds.session_token.empty()) {
    sign_headers.Add("X-Amz-Security-Token", creds.session_token);
  }

  minio::signer::SignV4S3(minio::http::Method::kPut, url.path, region,
                          sign_headers, query_params, creds.access_key,
                          creds.secret_key, kUnsignedPayload, date);

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
  std::string full_path =
      query_string.empty() ? path : path + "?" + query_string;

  url.path = "";
  url.query_string = "";
  httplib::Client cli(url.String());
  cli.set_connection_timeout(5);
  cli.set_read_timeout(10);

  auto res = cli.Put(full_path, headers, "", "");
  if (res.error() != httplib::Error::Success) {
    return -1;
  }

  std::string etag = res->get_header_value("ETag");
  if (res->status == 200 && !etag.empty()) {
    sctx->etag = minio::utils::Trim(etag, '"');
    return static_cast<ssize_t>(size);
  }

  std::string rdma_reply = res->get_header_value(kAmzRDMAReply);
  if (rdma_reply.empty() || rdma_reply == "501") {
    return kRDMANotSupported;
  }

  try {
    int reply_code = std::stoi(rdma_reply);
    if (reply_code != kRDMAReplySuccess && reply_code != kRDMAReplyNoContent) {
      return -1;
    }
  } catch (const std::exception&) {
    return -1;
  }

  std::string resp_checksum =
      res->get_header_value("x-amz-checksum-crc64nvme");
  if (!resp_checksum.empty()) {
    sctx->checksum = resp_checksum;
  }

  sctx->etag = minio::utils::Trim(etag, '"');
  return static_cast<ssize_t>(size);
}

inline static ssize_t rdmaGet(s3_rdma_client_ctx_t* sctx, const char* token,
                              const void* buf, size_t size) {
  char rdma_token[256];
  snprintf(rdma_token, sizeof(rdma_token), "%s:%016lx:%016lx", token,
           (uint64_t)buf, (uint64_t)size);

  minio::utils::UtcTime date = minio::utils::UtcTime::Now();
  minio::creds::Credentials creds = sctx->provider->Fetch();
  minio::utils::Multimap query_params;
  minio::http::Url url;
  const std::string& region = sctx->region;

  if (minio::error::Error err =
          sctx->url.BuildUrl(url, minio::http::Method::kGet, region,
                             query_params, sctx->bucket, sctx->object)) {
    return -1;
  }

  std::string host = url.HostHeaderValue();

  minio::utils::Multimap sign_headers;
  sign_headers.Add("Host", host);
  sign_headers.Add("x-amz-date", date.ToAmzDate());
  sign_headers.Add("x-amz-content-sha256", kUnsignedPayload);
  sign_headers.Add(kAmzRDMAToken, rdma_token);

  if (!creds.session_token.empty()) {
    sign_headers.Add("X-Amz-Security-Token", creds.session_token);
  }

  minio::signer::SignV4S3(minio::http::Method::kGet, url.path, region,
                          sign_headers, query_params, creds.access_key,
                          creds.secret_key, kUnsignedPayload, date);

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
  cli.set_connection_timeout(5);
  cli.set_read_timeout(10);

  auto res = cli.Get(path, headers);
  if (res.error() != httplib::Error::Success) {
    return -1;
  }

  std::string rdma_reply = res->get_header_value(kAmzRDMAReply);
  if (rdma_reply.empty() || rdma_reply == "501") {
    return kRDMANotSupported;
  }

  try {
    int reply_code = std::stoi(rdma_reply);
    if (reply_code != kRDMAReplySuccess &&
        reply_code != kRDMAReplyPartialContent) {
      return -1;
    }
  } catch (const std::exception&) {
    return -1;
  }

  return static_cast<ssize_t>(size);
}

#endif  // _MINIO_CPP_RDMA_H_INCLUDED
