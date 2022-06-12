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

#include "credentials.h"
#include "providers.h"
#include "signer.h"

namespace minio {
namespace s3 {
static std::string extractRegion(std::string& host) {
  std::stringstream str_stream(host);
  std::string token;
  std::vector<std::string> tokens;
  while (std::getline(str_stream, token, '.')) tokens.push_back(token);

  token = tokens[1];

  // If token is "dualstack", then region might be in next token.
  if (token == "dualstack") token = tokens[2];

  // If token is equal to "amazonaws", region is not passed in the host.
  if (token == "amazonaws") return "";

  // Return token as region.
  return token;
}

struct BaseUrl {
  bool https = true;
  std::string host;
  unsigned int port = 0;
  std::string region;
  bool aws_host = false;
  bool accelerate_host = false;
  bool dualstack_host = false;
  bool virtual_style = false;

  BaseUrl() {}
  BaseUrl(std::string host, bool https = true);
  error::Error BuildUrl(http::Url& url, http::Method method, std::string region,
                        utils::Multimap query_params,
                        std::string bucket_name = "",
                        std::string object_name = "");
  operator bool() const { return !err_ && !host.empty(); }
  error::Error Error() {
    if (host.empty() && !err_) return error::Error("empty host");
    return err_;
  }

 private:
  error::Error err_;
};  // struct Url

struct Request {
  http::Method method;
  std::string region;
  BaseUrl& base_url;

  std::string user_agent;

  utils::Multimap headers;
  utils::Multimap query_params;

  std::string bucket_name;
  std::string object_name;

  std::string_view body = "";

  http::DataFunction datafunc = NULL;
  void* userdata = NULL;

  std::string sha256;
  utils::Time date;

  bool debug = false;
  bool ignore_cert_check = false;
  std::string ssl_cert_file;

  Request(http::Method method, std::string region, BaseUrl& baseurl,
          utils::Multimap extra_headers, utils::Multimap extra_query_params);
  http::Request ToHttpRequest(creds::Provider* provider = NULL);

 private:
  void BuildHeaders(http::Url& url, creds::Provider* provider);
};  // struct Request
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_REQUEST_H
