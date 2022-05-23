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

#include "creds.h"

minio::creds::Credentials::Credentials(const Credentials& creds) {
  access_key_ = creds.access_key_;
  secret_key_ = creds.secret_key_;
  session_token_ = creds.session_token_;
  expiration_ = creds.expiration_;
}

minio::creds::Credentials::Credentials(std::string_view access_key,
                                       std::string_view secret_key,
                                       std::string_view session_token,
                                       unsigned int expiration) {
  access_key_ = access_key;
  secret_key_ = secret_key;
  session_token_ = session_token;
  expiration_ = expiration;
}

std::string minio::creds::Credentials::AccessKey() {
  return std::string(access_key_);
}

std::string minio::creds::Credentials::SecretKey() {
  return std::string(secret_key_);
}

std::string minio::creds::Credentials::SessionToken() {
  return std::string(session_token_);
}

bool minio::creds::Credentials::IsExpired() { return expiration_ != 0; }

minio::creds::StaticProvider::StaticProvider(std::string_view access_key,
                                             std::string_view secret_key,
                                             std::string_view session_token) {
  creds_ = new Credentials(access_key, secret_key, session_token);
}

minio::creds::StaticProvider::~StaticProvider() { delete creds_; }

minio::creds::Credentials minio::creds::StaticProvider::Fetch() {
  return *creds_;
}
