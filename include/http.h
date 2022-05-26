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

#ifndef _MINIO_HTTP_H
#define _MINIO_HTTP_H

#include <arpa/inet.h>

#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>

#include "utils.h"

namespace minio {
namespace http {
enum class Method { kGet, kHead, kPost, kPut, kDelete };

// MethodToString converts http Method enum to string.
constexpr const char* MethodToString(Method& method) throw() {
  switch (method) {
    case Method::kGet:
      return "GET";
    case Method::kHead:
      return "HEAD";
    case Method::kPost:
      return "POST";
    case Method::kPut:
      return "PUT";
    case Method::kDelete:
      return "DELETE";
    default: {
      std::cerr << "ABORT: Unimplemented HTTP method. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

// ExtractRegion extracts region value from AWS S3 host string.
std::string ExtractRegion(std::string host);

struct BaseUrl {
  std::string host;
  bool is_https = true;
  unsigned int port = 0;
  std::string region;
  bool aws_host = false;
  bool accelerate_host = false;
  bool dualstack_host = false;
  bool virtual_style = false;

  error::Error SetHost(std::string hostvalue);
  std::string GetHostHeaderValue();
  error::Error BuildUrl(utils::Url& url, Method method, std::string region,
                        utils::Multimap query_params,
                        std::string bucket_name = "",
                        std::string object_name = "");
  operator bool() const { return !host.empty(); }
};  // struct BaseUrl

struct DataCallbackArgs;

using DataCallback = std::function<size_t(DataCallbackArgs)>;

struct Response;

struct DataCallbackArgs {
  curlpp::Easy* handle = NULL;
  Response* response = NULL;
  char* buffer = NULL;
  size_t size = 0;
  size_t length = 0;
  void* user_arg = NULL;
};  // struct DataCallbackArgs

struct Request {
  Method method;
  utils::Url url;
  utils::Multimap headers;
  std::string_view body = "";
  DataCallback data_callback = NULL;
  void* user_arg = NULL;
  bool debug = false;
  bool ignore_cert_check = false;

  Request(Method httpmethod, utils::Url httpurl);
  Response Execute();
  operator bool() const {
    if (method < Method::kGet || method > Method::kDelete) return false;
    return url;
  }

 private:
  Response execute();
};  // struct Request

struct Response {
  std::string error;
  DataCallback data_callback = NULL;
  void* user_arg = NULL;
  int status_code = 0;
  utils::Multimap headers;
  std::string body;

  size_t ResponseCallback(curlpp::Easy* handle, char* buffer, size_t size,
                          size_t length);
  operator bool() const {
    return error.empty() && status_code >= 200 && status_code <= 299;
  }

 private:
  std::string response_;
  bool continue100_ = false;
  bool status_code_read_ = false;
  bool headers_read_ = false;

  size_t ReadStatusCode(char* buffer, size_t size, size_t length);
  size_t ReadHeaders(curlpp::Easy* handle, char* buffer, size_t size,
                     size_t length);
};  // struct Response
}  // namespace http
}  // namespace minio
#endif  // #ifndef _MINIO_HTTP_H
