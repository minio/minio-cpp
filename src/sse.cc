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

#include "sse.h"

minio::s3::Sse::Sse() {}

minio::s3::Sse::~Sse() {}

minio::utils::Multimap minio::s3::Sse::Headers() const { return headers_; }

minio::utils::Multimap minio::s3::Sse::CopyHeaders() const { return copy_headers_; }

minio::s3::SseCustomerKey::SseCustomerKey(std::string_view key) {
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

minio::s3::SseCustomerKey::~SseCustomerKey() {}

bool minio::s3::SseCustomerKey::TlsRequired() const { return true; }

minio::s3::SseKms::SseKms(std::string_view key, std::string_view context) {
  this->headers_.Add("X-Amz-Server-Side-Encryption-Aws-Kms-Key-Id",
                     std::string(key));
  this->headers_.Add("X-Amz-Server-Side-Encryption", "aws:kms");
  if (!context.empty()) {
    this->headers_.Add("X-Amz-Server-Side-Encryption-Context",
                       utils::Base64Encode(context));
  }
}

minio::s3::SseKms::~SseKms() {}

bool minio::s3::SseKms::TlsRequired() const { return true; }

minio::s3::SseS3::SseS3() {
  this->headers_.Add("X-Amz-Server-Side-Encryption", "AES256");
}

minio::s3::SseS3::~SseS3() {}

bool minio::s3::SseS3::TlsRequired() const { return false; }
