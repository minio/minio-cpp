// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2026 MinIO, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0

#ifdef MINIO_CPP_RDMA

#include "miniocpp/c_api.h"

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <istream>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>

#include "miniocpp/args.h"
#include "miniocpp/client.h"
#include "miniocpp/credentials.h"
#include "miniocpp/http.h"
#include "miniocpp/nvidia-cuobjclient.h"
#include "miniocpp/providers.h"
#include "miniocpp/response.h"

namespace {

thread_local std::string g_last_error;

void SetLastError(const std::string& msg) { g_last_error = msg; }

// Adapter so a miniocpp_read_cb can drive a std::istream-based upload.
class ReadCbStreamBuf : public std::streambuf {
 public:
  ReadCbStreamBuf(miniocpp_read_cb cb, void* userdata)
      : cb_(cb), userdata_(userdata) {}

 protected:
  int_type underflow() override {
    if (cb_ == nullptr) return traits_type::eof();
    ssize_t n = cb_(userdata_, buf_, sizeof(buf_));
    if (n <= 0) return traits_type::eof();
    setg(buf_, buf_, buf_ + n);
    return traits_type::to_int_type(buf_[0]);
  }

 private:
  miniocpp_read_cb cb_;
  void* userdata_;
  char buf_[64 * 1024];
};

struct ClientHolder {
  minio::s3::BaseUrl base_url;
  std::unique_ptr<minio::creds::StaticProvider> provider;
  std::unique_ptr<minio::s3::Client> client;
};

}  // namespace

extern "C" {

miniocpp_client* miniocpp_client_new(const char* endpoint, const char* region,
                                     const char* access_key,
                                     const char* secret_key,
                                     const char* session_token, int use_https) {
  if (endpoint == nullptr || access_key == nullptr || secret_key == nullptr) {
    SetLastError("endpoint, access_key, and secret_key are required");
    return nullptr;
  }
  try {
    auto holder = std::make_unique<ClientHolder>();
    holder->base_url = minio::s3::BaseUrl(endpoint, use_https != 0,
                                          region != nullptr ? region : "");
    holder->provider = std::make_unique<minio::creds::StaticProvider>(
        access_key, secret_key, session_token != nullptr ? session_token : "");
    holder->client = std::make_unique<minio::s3::Client>(
        holder->base_url, holder->provider.get());
    return reinterpret_cast<miniocpp_client*>(holder.release());
  } catch (const std::exception& e) {
    SetLastError(std::string("client construction failed: ") + e.what());
    return nullptr;
  }
}

void miniocpp_client_free(miniocpp_client* c) {
  delete reinterpret_cast<ClientHolder*>(c);
}

ssize_t miniocpp_put_object(miniocpp_client* c, const char* bucket,
                            const char* object, void* buf, size_t size,
                            miniocpp_read_cb read_cb, void* userdata,
                            char etag_out[64], char checksum_out[64]) {
  if (c == nullptr || bucket == nullptr || object == nullptr) {
    SetLastError("client, bucket, object are required");
    return MINIOCPP_ERR_INVALID_ARG;
  }
  if (buf == nullptr && read_cb == nullptr) {
    SetLastError("either buf or read_cb must be provided");
    return MINIOCPP_ERR_INVALID_ARG;
  }
  auto* holder = reinterpret_cast<ClientHolder*>(c);

  minio::s3::PutObjectArgs args;
  args.bucket = bucket;
  args.object = object;
  args.region = holder->base_url.region;

  std::unique_ptr<ReadCbStreamBuf> sbuf;
  std::unique_ptr<std::istream> sis;
  if (buf != nullptr) {
    args.buf = static_cast<char*>(buf);
    args.size = size;
    args.object_size = static_cast<long>(size);
    args.part_size = 16 * 1024 * 1024L;
  } else {
    sbuf = std::make_unique<ReadCbStreamBuf>(read_cb, userdata);
    sis = std::make_unique<std::istream>(sbuf.get());
    args.stream = sis.get();
    args.object_size = static_cast<long>(size);
    args.part_size = 16 * 1024 * 1024L;
  }

  auto resp = holder->client->PutObject(args);
  if (!resp) {
    SetLastError(resp.error().String());
    return MINIOCPP_ERR_GENERIC;
  }
  if (etag_out != nullptr) {
    std::strncpy(etag_out, resp->etag.c_str(), 63);
    etag_out[63] = '\0';
  }
  if (checksum_out != nullptr) {
    std::strncpy(checksum_out, resp->checksum_crc64nvme.c_str(), 63);
    checksum_out[63] = '\0';
  }
  return static_cast<ssize_t>(size);
}

ssize_t miniocpp_get_object(miniocpp_client* c, const char* bucket,
                            const char* object, void* buf, size_t size,
                            miniocpp_write_cb write_cb, void* userdata) {
  if (c == nullptr || bucket == nullptr || object == nullptr) {
    SetLastError("client, bucket, object are required");
    return MINIOCPP_ERR_INVALID_ARG;
  }
  if (buf == nullptr && write_cb == nullptr) {
    SetLastError("either buf or write_cb must be provided");
    return MINIOCPP_ERR_INVALID_ARG;
  }
  auto* holder = reinterpret_cast<ClientHolder*>(c);

  minio::s3::GetObjectArgs args;
  args.bucket = bucket;
  args.object = object;
  args.region = holder->base_url.region;

  ssize_t bytes_seen = 0;

  if (buf != nullptr) {
    args.buf = static_cast<char*>(buf);
    args.size = size;
  } else {
    args.datafunc = [write_cb, userdata,
                     &bytes_seen](minio::http::DataFunctionArgs a) -> bool {
      ssize_t n = write_cb(userdata, a.datachunk.data(), a.datachunk.size());
      if (n < 0 || static_cast<size_t>(n) != a.datachunk.size()) return false;
      bytes_seen += n;
      return true;
    };
  }

  auto resp = holder->client->GetObject(args);
  if (!resp) {
    SetLastError(resp.error().String());
    return MINIOCPP_ERR_GENERIC;
  }
  return buf != nullptr ? static_cast<ssize_t>(size) : bytes_seen;
}

void* miniocpp_alloc_aligned(size_t size) {
  void* p = nullptr;
  if (posix_memalign(&p, static_cast<size_t>(getpagesize()), size) != 0) {
    return nullptr;
  }
  return p;
}

void miniocpp_free_aligned(void* p) { std::free(p); }

int miniocpp_rdma_available(void) {
  try {
    CUObjIOOps ops{};
    cuObjClient probe(ops, CUOBJ_PROTO_RDMA_DC_V1);
    return probe.isConnected() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

const char* miniocpp_last_error(void) {
  return g_last_error.empty() ? nullptr : g_last_error.c_str();
}

}  // extern "C"

#endif  // MINIO_CPP_RDMA
