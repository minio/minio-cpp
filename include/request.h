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

#ifndef _MINIO_REQUEST_H
#define _MINIO_REQUEST_H

#include "creds.h"
#include "signer.h"

namespace minio {
namespace s3 {
struct Request {
  http::Method method;
  std::string region;
  http::BaseUrl& base_url;

  std::string user_agent;

  utils::Multimap headers;
  utils::Multimap query_params;

  std::string bucket_name;
  std::string object_name;

  std::string_view body = "";

  http::DataCallback data_callback = NULL;
  void* user_arg = NULL;

  std::string sha256;
  utils::Time date;

  bool debug = false;
  bool ignore_cert_check = false;

  Request(http::Method httpmethod, std::string regionvalue,
          http::BaseUrl& baseurl);
  http::Request ToHttpRequest(creds::Provider* provider = NULL);

 private:
  void BuildHeaders(utils::Url& url, creds::Provider* provider);
};  // struct Request
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_REQUEST_H
