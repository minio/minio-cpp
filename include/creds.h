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

#ifndef _MINIO_CREDS_H
#define _MINIO_CREDS_H

#include <string>

namespace minio {
namespace creds {
/**
 * Credentials contains access key and secret key with optional session token
 * and expiration.
 */
class Credentials {
 private:
  std::string_view access_key_;
  std::string_view secret_key_;
  std::string_view session_token_;
  unsigned int expiration_;

 public:
  Credentials(const Credentials& creds);
  Credentials(std::string_view access_key, std::string_view secret_key,
              std::string_view session_token = "", unsigned int expiration = 0);
  std::string AccessKey();
  std::string SecretKey();
  std::string SessionToken();
  bool IsExpired();
};  // class Credentials

/**
 * Credential provider interface.
 */
class Provider {
 public:
  Provider() {}
  virtual ~Provider() {}
  virtual Credentials Fetch() = 0;
};  // class Provider

/**
 * Static credential provider.
 */
class StaticProvider : public Provider {
 private:
  Credentials* creds_ = NULL;

 public:
  StaticProvider(std::string_view access_key, std::string_view secret_key,
                 std::string_view session_token = "");
  ~StaticProvider();
  Credentials Fetch();
};  // class StaticProvider
}  // namespace creds
}  // namespace minio

#endif  // #ifndef _MINIO_CREDS_H
