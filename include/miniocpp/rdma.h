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
// minio-cpp does NOT depend on the CUDA Toolkit (cudart / nvcc / cuda_runtime),
// nor on any CUDA header. The SDK links only against libs3rdma (vendored under
// vendor/s3rdma/), which is plain IBTA verbs, and calls no CUDA symbol. The SDK
// therefore compiles and runs on hosts with no CUDA Toolkit and no NVIDIA
// driver installed.
//
// CUDA is strictly an APPLICATION concern: if your application allocates
// GPU buffers (cudaMalloc / cuMemAlloc) and hands them to PutObjectRDMAArgs
// or GetObjectRDMAArgs, *your* application links against CUDA. Applications
// that pass pinned host memory (posix_memalign / aligned_alloc) don't need
// CUDA at all — libs3rdma classifies a pointer through dlopen("libcuda.so.1")
// when a driver is present, and reports plain host memory when it is not.
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "credentials.h"
#include "error.h"
#include "http.h"
#include "rdma_client.h"
#include "request.h"
#include "signer.h"
#include "utils.h"

namespace minio::rdma {

// Per-request state the RDMA control plane needs to build and sign the S3
// request that carries the token.
struct ClientCtx {
  // All members carry explicit in-class defaults so designated-initializer
  // construction (e.g. `ClientCtx{.bucket=...}`) does not trip
  // -Wmissing-field-initializers for the std::string fields we leave
  // unspecified at single-shot Put/Get call sites (uploadId/partNumber for
  // non-multipart paths, etag/checksum for fields populated by the callee).
  minio::creds::Provider* const provider = nullptr;
  std::string bucket = {};
  std::string object = {};
  std::string uploadId = {};
  std::optional<size_t> partNumber = std::nullopt;
  std::string etag = {};
  minio::s3::BaseUrl url = {};
  std::string region = {};
  std::string checksum = {};
};

}  // namespace minio::rdma

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

// The call never reached the fabric: a bad argument, or a URL that would not
// build. Distinct from -1 because no rail carried this attempt, so charging one
// a failure would take healthy hardware out of rotation over a client-side
// mistake -- and the same error would repeat on the next rail anyway. Callers
// treat it like any other failure and fall back to HTTP.
inline constexpr ssize_t kRDMALocalError = -3;

// Largest transfer a single RDMA descriptor can describe: the
// x-amz-rdma-token carries the window size in a 32-bit field. RDMA is only
// attempted for buffers up to this size; a larger buffer cannot be named to
// the server at all, so PutObject/GetObject transfer it over a single ordinary
// HTTP request instead (the buffer is already resident, and AIStor accepts a
// single PUT up to kMaxObjectSize == 5 TiB — far beyond this limit and beyond
// anything a client can pin or allocate). Sizing the buffer is the caller's
// responsibility; the SDK does not chunk registrations.
inline constexpr size_t kRDMAMaxMemoryRegSize = 0xFFFFFFFFULL;

// RDMA control-plane timeouts (seconds). The HTTP exchange carries only
// the token and a few headers — keep them aggressive so a dead NIC surfaces
// fast and the caller can fall back to HTTP without a long stall.
inline constexpr long kRDMAConnectTimeoutSecs = 5;
inline constexpr long kRDMATimeoutSecs = 10;

// Extract the client NIC IP from the 81-char RDMA token, whose last 32 hex
// chars are the source NIC's GID. Binding the outbound HTTP socket to that
// address (via CURLOPT_INTERFACE) keeps the TCP session and the RDMA peer on
// the same NIC, so the server's RDMA_READ back to the client hits the same HCA
// that delivered the HTTP request. Returns empty string unless the GID is an
// IPv4-mapped IPv6 address ("...ffffAABBCCDD"), which is what a RoCEv2 GID
// over IPv4 looks like — an InfiniBand or IPv6 GID names no address to bind.
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

// Attempts per transfer. A second attempt mints a fresh token, which is what
// recovers a transfer whose first token was rejected or whose queue pair hit a
// transient completion error; anything persistent fails again and falls back
// to HTTP rather than stalling the caller.
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

inline static ssize_t rdmaPut(minio::rdma::ClientCtx* sctx, const char* token,
                              size_t size) {
  // The token is the RDMA descriptor verbatim; its leading fields already
  // carry the buffer address and transfer size, so it is sent as-is.
  minio::utils::UtcTime date = minio::utils::UtcTime::Now();
  minio::creds::Credentials creds = sctx->provider->Fetch();
  minio::utils::Multimap query_params;
  minio::http::Url url;
  const std::string& region = sctx->region;

  if (!sctx->uploadId.empty()) {
    query_params.Add("uploadId", sctx->uploadId);
    // An uploadId without a part number is not a request we can send, and a
    // part number outside 1..10000 is not one S3 accepts.
    if (!sctx->partNumber || *sctx->partNumber == 0 ||
        *sctx->partNumber > 10000) {
      return kRDMALocalError;
    }
    query_params.Add("partNumber", std::to_string(*sctx->partNumber));
  }

  if (minio::error::Error err =
          sctx->url.BuildUrl(url, minio::http::Method::kPut, region,
                             query_params, sctx->bucket, sctx->object)) {
    return kRDMALocalError;
  }

  std::string host = url.HostHeaderValue();

  minio::utils::Multimap sign_headers;
  sign_headers.Add("Host", host);
  sign_headers.Add("x-amz-date", date.ToAmzDate());
  sign_headers.Add("x-amz-content-sha256", kUnsignedPayload);
  sign_headers.Add(kAmzRDMAToken, token);
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

// range_offset < 0 reads the whole object; range_offset >= 0 reads size bytes
// starting at that object offset. The object offset travels in a Range header
// (the server derives its rangeBase from it) and is independent of the buffer
// address carried in the RDMA token — see AIStor rdmaTransferBounds().
inline static ssize_t rdmaGet(minio::rdma::ClientCtx* sctx, const char* token,
                              size_t size, int64_t range_offset = -1) {
  // The token is the RDMA descriptor verbatim; its leading fields already
  // carry the buffer address and transfer size, so it is sent as-is.
  minio::utils::UtcTime date = minio::utils::UtcTime::Now();
  minio::creds::Credentials creds = sctx->provider->Fetch();
  minio::utils::Multimap query_params;
  minio::http::Url url;
  const std::string& region = sctx->region;

  if (minio::error::Error err =
          sctx->url.BuildUrl(url, minio::http::Method::kGet, region,
                             query_params, sctx->bucket, sctx->object)) {
    return kRDMALocalError;
  }

  std::string host = url.HostHeaderValue();

  minio::utils::Multimap sign_headers;
  sign_headers.Add("Host", host);
  sign_headers.Add("x-amz-date", date.ToAmzDate());
  sign_headers.Add("x-amz-content-sha256", kUnsignedPayload);
  sign_headers.Add(kAmzRDMAToken, token);

  // A byte-range request; added before signing so the server accepts the
  // SignedHeaders. bytes=<offset>-<offset+size-1> selects the object range;
  // the server replies 206 (kRDMAReplyPartialContent) for it.
  if (range_offset >= 0) {
    // A zero-length range would emit bytes=X-(X-1); never sent.
    if (size == 0) return kRDMALocalError;
    char range_hdr[64];
    snprintf(
        range_hdr, sizeof(range_hdr), "bytes=%lld-%lld",
        static_cast<long long>(range_offset),
        static_cast<long long>(range_offset + static_cast<int64_t>(size) - 1));
    sign_headers.Add("Range", range_hdr);
  }

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
// token, and retries once on transient RDMA failure.
//
// Caller must have already registered the buffer via Client::Register.
//
// Returns:
//   >0                 bytes transferred (success)
//   kRDMANotSupported  server sent x-amz-rdma-reply: 501 (fall back to HTTP)
//   -1                 exhausted retries (fall back to HTTP)
inline static ssize_t rdmaPutWithRetry(minio::rdma::Client* rdmaclient,
                                       minio::rdma::ClientCtx* sctx, void* buf,
                                       size_t size) {
  ssize_t ret = -1;
  for (int attempt = 0; attempt < kRDMAMaxAttempts; ++attempt) {
    minio::rdma::Token token = rdmaclient->GetToken(buf, size);
    if (!token) return -1;
    ret = rdmaPut(sctx, token.c_str(), size);
    if (ret > 0 || ret == kRDMANotSupported || ret == kRDMALocalError) {
      return ret;
    }
    // The transfer failed on the wire. Charge it to the rail this token named
    // so the next request skips that rail rather than round-robinning back
    // onto it. Two results return above instead: a 501, because the server
    // declining RDMA says nothing about the rail, and a local error, because
    // nothing was ever sent.
    //
    // A server-side fault marks every rail in turn, which is safe -- libs3rdma
    // clears all marks once no rail is left usable, so a fault that was never
    // the fabric's heals itself.
    rdmaclient->ReportTokenFailure(token.c_str());
  }
  return ret;
}

// rdmaGetWithRetry is the GET counterpart to rdmaPutWithRetry. Same
// contract: caller registers the buffer, this helper handles token
// lifecycle and the retry.
inline static ssize_t rdmaGetWithRetry(minio::rdma::Client* rdmaclient,
                                       minio::rdma::ClientCtx* sctx, void* buf,
                                       size_t size, int64_t range_offset = -1) {
  ssize_t ret = -1;
  for (int attempt = 0; attempt < kRDMAMaxAttempts; ++attempt) {
    minio::rdma::Token token = rdmaclient->GetToken(buf, size);
    if (!token) return -1;
    ret = rdmaGet(sctx, token.c_str(), size, range_offset);
    if (ret > 0 || ret == kRDMANotSupported || ret == kRDMALocalError) {
      return ret;
    }
    // See rdmaPutWithRetry: take the failing rail out of rotation.
    // A local error returns above; no rail carried it.
    rdmaclient->ReportTokenFailure(token.c_str());
  }
  return ret;
}

#endif  // _MINIO_CPP_RDMA_H_INCLUDED
