// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2026 MinIO, Inc.
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

#ifndef MINIO_CPP_RDMA_CLIENT_H_INCLUDED
#define MINIO_CPP_RDMA_CLIENT_H_INCLUDED

// RDMA transport for the SDK, over libs3rdma (vendored under vendor/s3rdma/).
//
// The SDK is the RDMA passive side: it pins the caller's buffer, mints an
// x-amz-rdma-token describing it, and sends that token on the ordinary HTTP
// request; the server performs the one-sided transfer.
//
// Supports any memory ibv_reg_mr accepts — page-aligned host memory,
// hugepages, and GPU device memory (cudaMalloc / cuMemAlloc) — over RoCE or
// native InfiniBand. It mints DC tokens, so it needs a DC-capable HCA (mlx5,
// ConnectX-4 and later); on a device without DC, Client::Ready() is false and
// every transfer takes the ordinary HTTP path.
//
// CUDA remains strictly an application concern: libminiocpp
// links no CUDA library and calls no CUDA symbol. An application that hands
// GPU buffers to PutObjectRDMAArgs / GetObjectRDMAArgs links CUDA itself; one
// that passes pinned host memory needs no CUDA at all.

#include <s3rdma.h>

#include <cstddef>
#include <utility>

namespace minio::rdma {

/// What kind of memory backs a pointer.
enum class MemoryType {
  kSystem = S3RDMA_MEM_SYSTEM,
  kCudaManaged = S3RDMA_MEM_CUDA_MANAGED,
  kCudaDevice = S3RDMA_MEM_CUDA_DEVICE,
  kUnknown = S3RDMA_MEM_UNKNOWN,
};

/// A minted x-amz-rdma-token, released back to libs3rdma on destruction.
class Token {
 public:
  Token() = default;
  explicit Token(char* raw) : raw_(raw) {}
  ~Token() { Reset(); }

  Token(const Token&) = delete;
  Token& operator=(const Token&) = delete;
  Token(Token&& o) noexcept : raw_(std::exchange(o.raw_, nullptr)) {}
  Token& operator=(Token&& o) noexcept {
    if (this != &o) {
      Reset();
      raw_ = std::exchange(o.raw_, nullptr);
    }
    return *this;
  }

  const char* c_str() const { return raw_; }
  explicit operator bool() const { return raw_ != nullptr; }

 private:
  void Reset() {
    if (raw_ != nullptr) {
      s3rdma_client_free_token(raw_);
      raw_ = nullptr;
    }
  }

  char* raw_ = nullptr;
};

/// An RDMA device context and its DCT endpoint. One per process; see
/// minio::rdma::Shared() below.
class Client {
 public:
  Client() : handle_(s3rdma_client_init(nullptr, nullptr, 0)) {}
  ~Client() {
    if (handle_ != nullptr) s3rdma_client_free(handle_);
  }

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  /// Whether this host has a usable RDMA device, so a transfer is worth
  /// attempting. DC is connectionless, so this says nothing about a
  /// particular server: one that will not serve RDMA declines per request
  /// with x-amz-rdma-reply: 501, and the caller falls back to HTTP.
  bool Ready() const { return s3rdma_client_ready(handle_) != 0; }

  /// Pin `buf` for RDMA. Reference counted per address, so nesting a part
  /// registration inside an object registration costs one ibv_reg_mr.
  bool Register(void* buf, size_t size) {
    return s3rdma_client_register(handle_, buf, size) == 0;
  }
  bool Deregister(void* buf) {
    return s3rdma_client_deregister(handle_, buf) == 0;
  }

  /// Mint a token for `size` bytes at `offset` within the pinned region at
  /// `buf`. Returns an empty Token on failure.
  Token GetToken(void* buf, size_t size, size_t offset = 0) {
    char* raw = nullptr;
    if (s3rdma_client_get_token(handle_, buf, size, offset, &raw) != 0) {
      return Token{};
    }
    return Token{raw};
  }

  static MemoryType GetMemoryType(const void* ptr) {
    return static_cast<MemoryType>(s3rdma_client_memory_type(ptr));
  }

 private:
  S3RdmaClientHandle handle_ = nullptr;
};

/// The process-wide client. Meyers singleton — thread-safe per C++11
/// [stmt.dcl]/4, so the first caller opens the device and the rest wait for
/// it rather than each opening one of their own. It owns an RDMA device
/// context and a DCT endpoint, and its buffer registrations are reference
/// counted, so sharing one keeps a buffer pinned exactly once no matter how
/// many callers touch it.
inline Client& Shared() {
  static Client client;
  return client;
}

}  // namespace minio::rdma

#endif  // MINIO_CPP_RDMA_CLIENT_H_INCLUDED
