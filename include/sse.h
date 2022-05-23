// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022 MinIO, Inc.
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

#ifndef _MINIO_S3_SSE_H
#define _MINIO_S3_SSE_H

#include "utils.h"

namespace minio {
namespace s3 {
class Sse {
 public:
  utils::Multimap empty_;

 public:
  Sse() {}
  virtual ~Sse() {}
  bool TlsRequired() { return true; }
  utils::Multimap CopyHeaders() { return empty_; }
  virtual utils::Multimap Headers() = 0;
};  // class Sse

class SseCustomerKey : public Sse {
 private:
  utils::Multimap headers;
  utils::Multimap copy_headers;

 public:
  SseCustomerKey(std::string_view key);
  utils::Multimap Headers() { return headers; }
  utils::Multimap CopyHeaders() { return copy_headers; }
};  // class SseCustomerKey

class SseKms : public Sse {
 private:
  utils::Multimap headers;

 public:
  SseKms(std::string_view key, std::string_view context);
  utils::Multimap Headers() { return headers; }
};  // class SseKms

class SseS3 : public Sse {
 private:
  utils::Multimap headers;

 public:
  SseS3();
  utils::Multimap Headers() { return headers; }
  bool TlsRequired() { return false; }
};  // class SseS3
}  // namespace s3
}  // namespace minio

#endif  // #ifndef __MINIO_S3_SSE_H
