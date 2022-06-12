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
  Sse() {}

  virtual ~Sse() {}

  utils::Multimap Headers() { return headers_; }

  utils::Multimap CopyHeaders() { return copy_headers_; }

  virtual bool TlsRequired() = 0;
};  // class Sse

class SseCustomerKey : public Sse {
 public:
  SseCustomerKey(std::string_view key) {
    std::string b64key = utils::Base64Encode(key);
    std::string md5key = utils::Md5sumHash(key);

    this->headers_.Add("X-Amz-Server-Side-Encryption-Customer-Algorithm",
                       "AES256");
    this->headers_.Add("X-Amz-Server-Side-Encryption-Customer-Key", b64key);
    this->headers_.Add("X-Amz-Server-Side-Encryption-Customer-Key-MD5", md5key);

    this->copy_headers_.Add(
        "X-Amz-Copy-Source-Server-Side-Encryption-Customer-Algorithm",
        "AES256");
    this->copy_headers_.Add(
        "X-Amz-Copy-Source-Server-Side-Encryption-Customer-Key", b64key);
    this->copy_headers_.Add(
        "X-Amz-Copy-Source-Server-Side-Encryption-Customer-Key-MD5", md5key);
  }

  bool TlsRequired() { return true; }
};  // class SseCustomerKey

class SseKms : public Sse {
 public:
  SseKms(std::string_view key, std::string_view context) {
    this->headers_.Add("X-Amz-Server-Side-Encryption-Aws-Kms-Key-Id",
                       std::string(key));
    this->headers_.Add("X-Amz-Server-Side-Encryption", "aws:kms");
    if (!context.empty()) {
      this->headers_.Add("X-Amz-Server-Side-Encryption-Context",
                         utils::Base64Encode(context));
    }
  }

  bool TlsRequired() { return true; }
};  // class SseKms

class SseS3 : public Sse {
 public:
  SseS3() { this->headers_.Add("X-Amz-Server-Side-Encryption", "AES256"); }

  bool TlsRequired() { return false; }
};  // class SseS3
}  // namespace s3
}  // namespace minio

#endif  // #ifndef __MINIO_S3_SSE_H
