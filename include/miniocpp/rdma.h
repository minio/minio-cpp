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

// CUDA dependency model
// ---------------------
// minio-cpp does NOT depend on the CUDA Toolkit (cudart / nvcc / cuda_runtime).
// The SDK links only against libcufile + libcuobjclient (vendored), and its
// headers pull in cuda.h solely for type declarations (CUdeviceptr, etc.)
// that appear in cuFile / cuObj API signatures — no CUDA driver or runtime
// symbols are called from within minio-cpp itself. The vendored copy of
// cuda.h under vendor/cuobj/include/ satisfies this type-only include, so
// the SDK compiles and runs on hosts without the CUDA Toolkit.
//
// CUDA is strictly an APPLICATION concern: if your application allocates
// GPU buffers (cudaMalloc / cuMemAlloc) and hands them to PutObjectRDMAArgs
// or GetObjectRDMAArgs, *your* application links against CUDA. Applications
// that pass pinned host memory (posix_memalign / aligned_alloc) don't need
// CUDA at all — cuFile detects host pointers via cuFileGetMemoryType and
// skips the GPU codepath.
#include <cstdio>
#include <cstring>
#include <string>

#include "credentials.h"
#include "error.h"
#include "http.h"
#include "nvidia-cufile.h"
#include "nvidia-cuobjclient.h"
#include "request.h"
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

// RDMA control-plane timeouts (seconds). The HTTP exchange carries only
// the token and a few headers — keep them aggressive so a dead NIC surfaces
// fast and the retry path can pick up the failover NIC.
inline constexpr long kRDMAConnectTimeoutSecs = 5;
inline constexpr long kRDMATimeoutSecs = 10;

// Extract the client NIC IP from the 81-char RDMA token. libcuobjclient
// 1.2.0+ encodes the source NIC's GID in the last 32 hex chars of the
// descriptor as an IPv4-mapped IPv6 suffix ("...ffffAABBCCDD"). Binding
// the outbound HTTP socket to that address (via CURLOPT_INTERFACE) keeps
// the TCP session and the RDMA peer on the same NIC, so the server's
// RDMA_READ back to the client hits the same HCA that delivered the HTTP
// request. Returns empty string if the token doesn't follow the expected
// layout (older client, non-multipath).
inline static std::string parseClientNICFromToken(const char* token) {
  if (token == nullptr) return {};
  size_t n = strlen(token);
  if (n < 32) return {};
  const char* tail = token + n - 32;
  for (int i = 0; i < 20; ++i) {
    if (tail[i] != '0') return {};
  }
  if (tail[20] != 'f' || tail[21] != 'f' || tail[22] != 'f' ||
      tail[23] != 'f') {
    return {};
  }
  unsigned int a, b, c, d;
  if (std::sscanf(tail + 24, "%2x%2x%2x%2x", &a, &b, &c, &d) != 4) return {};
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a, b, c, d);
  return std::string(buf);
}

// Maximum attempts for NIC-failover aware retry. Two attempts is sufficient:
// the first failure is what surfaces the bad NIC to libcuobjclient's
// async-event + health-check threads; the second mint will route to the
// backup NIC when rdma_multipath_enabled=true and a healthy backup exists.
inline constexpr int kRDMAMaxAttempts = 2;

// parseRDMAReply maps the server's x-amz-rdma-reply header to a transfer
// outcome. Returns:
//   >0  reply code (200/204/206) — caller should treat as success
//    0  reply absent or unparseable — caller should treat as -1 failure
//   -2  reply explicitly says 501 (server declined RDMA, fall back to HTTP)
inline static int parseRDMAReply(const std::string& rdma_reply) {
  if (rdma_reply.empty() || rdma_reply == "501") {
    return static_cast<int>(kRDMANotSupported);
  }
  try {
    return std::stoi(rdma_reply);
  } catch (const std::exception&) {
    return 0;
  }
}

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

  url.query_string = query_params.ToQueryString();

  minio::http::Request req(minio::http::Method::kPut, url);
  req.headers = sign_headers;
  req.connect_timeout_secs = kRDMAConnectTimeoutSecs;
  req.timeout_secs = kRDMATimeoutSecs;

  // Pin the TCP source address to the same NIC whose GID is embedded in
  // the RDMA token. Without this, multipath can pick a backup NIC for
  // the token while the kernel sends HTTP out the primary NIC, and the
  // server's RDMA_READ has no healthy path back to the token's peer.
  std::string client_nic = parseClientNICFromToken(token);
  if (!client_nic.empty()) {
    req.nic_interface = client_nic;
  }

  minio::http::Response res = req.Execute();
  if (!res.error.empty()) {
    return -1;
  }

  std::string etag = res.headers.GetFront("etag");
  if (res.status_code == 200 && !etag.empty()) {
    sctx->etag = minio::utils::Trim(etag, '"');
    return static_cast<ssize_t>(size);
  }

  int reply_code = parseRDMAReply(res.headers.GetFront(kAmzRDMAReply));
  if (reply_code == static_cast<int>(kRDMANotSupported)) {
    return kRDMANotSupported;
  }
  if (reply_code != kRDMAReplySuccess && reply_code != kRDMAReplyNoContent) {
    return -1;
  }

  std::string resp_checksum = res.headers.GetFront("x-amz-checksum-crc64nvme");
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

  minio::http::Request req(minio::http::Method::kGet, url);
  req.headers = sign_headers;
  req.connect_timeout_secs = kRDMAConnectTimeoutSecs;
  req.timeout_secs = kRDMATimeoutSecs;

  // Pin TCP source to the token's NIC — see rdmaPut above for rationale.
  std::string client_nic = parseClientNICFromToken(token);
  if (!client_nic.empty()) {
    req.nic_interface = client_nic;
  }

  minio::http::Response res = req.Execute();
  if (!res.error.empty()) {
    return -1;
  }

  int reply_code = parseRDMAReply(res.headers.GetFront(kAmzRDMAReply));
  if (reply_code == static_cast<int>(kRDMANotSupported)) {
    return kRDMANotSupported;
  }
  if (reply_code != kRDMAReplySuccess &&
      reply_code != kRDMAReplyPartialContent) {
    return -1;
  }

  // Trust the server's reported transferred byte count. The protocol uses
  // Content-Length: 0 on the HTTP body (the data went over RDMA), and the
  // actual transferred size is communicated via x-amz-rdma-bytes-transferred.
  // For ranged/partial GETs this can be less than the caller-requested size.
  std::string bytes_hdr = res.headers.GetFront(kAmzRDMABytesTransferred);
  if (!bytes_hdr.empty()) {
    try {
      long long n = std::stoll(bytes_hdr);
      if (n < 0) return -1;
      return static_cast<ssize_t>(n);
    } catch (const std::exception&) {
      return -1;
    }
  }

  // Header absent (older server). Assume full transfer for backward compat.
  return static_cast<ssize_t>(size);
}

// rdmaPutWithRetry mints a fresh RDMA token, issues rdmaPut, releases the
// token, and retries once on transient RDMA failure. On the second attempt
// libcuobjclient's multipath state will route the mint to the backup NIC
// if the primary has been flagged unhealthy; this turns what would have
// been a fail-and-fall-back-to-HTTP into a successful RDMA op across the
// NIC transition.
//
// Caller must have already registered the buffer via cuMemObjGetDescriptor.
//
// Returns:
//   >0                 bytes transferred (success)
//   kRDMANotSupported  server sent x-amz-rdma-reply: 501 (fall back to HTTP)
//   -1                 exhausted retries (fall back to HTTP)
inline static ssize_t rdmaPutWithRetry(cuObjClient* rdmaclient,
                                       s3_rdma_client_ctx_t* sctx, void* buf,
                                       size_t size) {
  ssize_t ret = -1;
  for (int attempt = 0; attempt < kRDMAMaxAttempts; ++attempt) {
    char* token = nullptr;
    cuObjErr_t terr =
        rdmaclient->cuMemObjGetRDMAToken(buf, size, 0, CUOBJ_PUT, &token);
    if (terr != CU_OBJ_SUCCESS || token == nullptr) {
      return -1;
    }
    ret = rdmaPut(sctx, token, buf, size);
    rdmaclient->cuMemObjPutRDMAToken(token);
    if (ret > 0 || ret == kRDMANotSupported) {
      return ret;
    }
  }
  return ret;
}

// rdmaGetWithRetry is the GET counterpart to rdmaPutWithRetry. Same
// contract: caller registers the buffer, this helper handles token
// lifecycle and one retry for NIC failover.
inline static ssize_t rdmaGetWithRetry(cuObjClient* rdmaclient,
                                       s3_rdma_client_ctx_t* sctx, void* buf,
                                       size_t size) {
  ssize_t ret = -1;
  for (int attempt = 0; attempt < kRDMAMaxAttempts; ++attempt) {
    char* token = nullptr;
    cuObjErr_t terr =
        rdmaclient->cuMemObjGetRDMAToken(buf, size, 0, CUOBJ_GET, &token);
    if (terr != CU_OBJ_SUCCESS || token == nullptr) {
      return -1;
    }
    ret = rdmaGet(sctx, token, buf, size);
    rdmaclient->cuMemObjPutRDMAToken(token);
    if (ret > 0 || ret == kRDMANotSupported) {
      return ret;
    }
  }
  return ret;
}

#endif  // _MINIO_CPP_RDMA_H_INCLUDED
