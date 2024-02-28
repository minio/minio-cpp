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

#ifndef _MINIO_CREDS_CREDENTIALS_H
#define _MINIO_CREDS_CREDENTIALS_H

#include "utils.h"

namespace minio {
namespace creds {
  bool expired(const utils::Time& expiration);


/**
 * Credentials contains access key and secret key with optional session token
 * and expiration.
 */
struct Credentials {
  error::Error err;
  std::string access_key;
  std::string secret_key;
  std::string session_token;
  utils::Time expiration;

  Credentials() = default;
  ~Credentials() = default;

  bool IsExpired() const { return expired(expiration); }

  explicit operator bool() const {
    return !err && !access_key.empty() && expired(expiration);
  }

  static Credentials ParseXML(std::string_view data, const std::string& root);
};  // class Credentials
}  // namespace creds
}  // namespace minio

#endif  // #ifndef _MINIO_CREDS_CREDENTIALS_H
