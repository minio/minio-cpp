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
 protected:
  utils::Multimap headers_;
  utils::Multimap copy_headers_;

 public:
  Sse();
  virtual ~Sse();

  utils::Multimap Headers() const;
  utils::Multimap CopyHeaders() const;

  virtual bool TlsRequired() const = 0;
};  // class Sse

class SseCustomerKey : public Sse {
 public:
  SseCustomerKey(std::string_view key);
  virtual ~SseCustomerKey();

  virtual bool TlsRequired() const override;
};  // class SseCustomerKey

class SseKms : public Sse {
 public:
  SseKms(std::string_view key, std::string_view context);
  virtual ~SseKms();

  virtual bool TlsRequired() const override;
};  // class SseKms

class SseS3 : public Sse {
 public:
  SseS3();
  virtual ~SseS3();

  virtual bool TlsRequired() const override;
};  // class SseS3
}  // namespace s3
}  // namespace minio

#endif  // #ifndef __MINIO_S3_SSE_H
