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

// Private to the implementation: the definition behind BaseClient::d_. Not
// installed, and only included by the translation units that implement
// BaseClient and its subclasses.

#ifndef MINIO_CPP_BASECLIENT_IMPL_H_INCLUDED
#define MINIO_CPP_BASECLIENT_IMPL_H_INCLUDED

#include <map>
#include <shared_mutex>
#include <string>
#include <utility>

#include "miniocpp/baseclient.h"
#include "miniocpp/config.h"
#include "miniocpp/http.h"
#include "miniocpp/providers.h"

namespace minio::s3 {

struct BaseClient::Impl {
  Impl(BaseUrl base_url, creds::Provider* provider)
      : base_url(std::move(base_url)), provider(provider) {}

  BaseUrl base_url;
  creds::Provider* const provider;
  std::map<std::string, std::string> region_map;
  mutable std::shared_mutex region_map_mutex;
  bool debug = false;
  bool ignore_cert_check = false;
  std::string ssl_cert_file;
  std::string user_agent = DEFAULT_USER_AGENT;
  // Owns the HTTP backend, so it has somewhere to keep state that outlives a
  // request (the curl backend's connection cache). Requests may run
  // concurrently, so the backend is never shared between them without
  // synchronization.
  http::Client http_client;
};  // struct BaseClient::Impl

}  // namespace minio::s3

#endif  // MINIO_CPP_BASECLIENT_IMPL_H_INCLUDED
