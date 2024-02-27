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

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <curlpp/Easy.hpp>
#include <curlpp/Multi.hpp>
#include <curlpp/Options.hpp>

#include "utils.h"

namespace minio {
namespace http {
enum class Method { kGet, kHead, kPost, kPut, kDelete };

// MethodToString converts http Method enum to string.
constexpr const char* MethodToString(const Method& method) throw() {
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

/**
 * Url represents HTTP URL and it's components.
 */
struct Url {
  bool https; // PWTODO: assign default value
  std::string host;
  unsigned int port = 0;
  std::string path;
  std::string query_string;

  Url() = default;
  ~Url() = default;

  explicit operator bool() const { return !host.empty(); }

  std::string String() const;
  std::string HostHeaderValue() const;
  static Url Parse(std::string value);
};  // struct Url

struct DataFunctionArgs;

using DataFunction = std::function<bool(DataFunctionArgs)>;

struct ProgressFunctionArgs;

using ProgressFunction = std::function<void(ProgressFunctionArgs)>;

struct Response;

struct DataFunctionArgs {
  curlpp::Easy* handle = NULL;
  Response* response = NULL;
  std::string datachunk;
  void* userdata = NULL;

  DataFunctionArgs() = default;
  ~DataFunctionArgs() = default;
};  // struct DataFunctionArgs

struct ProgressFunctionArgs {
  double download_total_bytes = 0.0;
  double downloaded_bytes = 0.0;
  double upload_total_bytes = 0.0;
  double uploaded_bytes = 0.0;
  double download_speed = 0.0;
  double upload_speed = 0.0;
  void* userdata = NULL;

  ProgressFunctionArgs() = default;
  ~ProgressFunctionArgs() = default;
};  // struct ProgressFunctionArgs

struct Request {
  Method method;
  http::Url url;
  utils::Multimap headers;
  std::string_view body = "";
  DataFunction datafunc = NULL;
  void* userdata = NULL;
  ProgressFunction progressfunc = NULL;
  void* progress_userdata = NULL;
  bool debug = false;
  bool ignore_cert_check = false;
  std::string ssl_cert_file;
  std::string key_file;
  std::string cert_file;

  Request(Method method, Url url);
  ~Request() = default;

  Response Execute();

  explicit operator bool() const {
    if (method < Method::kGet || method > Method::kDelete) return false;
    return static_cast<bool>(url);
  }

 private:
  Response execute();
};  // struct Request

struct Response {
  std::string error;
  DataFunction datafunc = NULL;
  void* userdata = NULL;
  int status_code = 0;
  utils::Multimap headers;
  std::string body;

  Response() = default;
  ~Response() = default;

  size_t ResponseCallback(curlpp::Multi* requests, curlpp::Easy* request,
                          char* buffer, size_t size, size_t length);

  explicit operator bool() const {
    return error.empty() && status_code >= 200 && status_code <= 299;
  }

  error::Error Error() const {
    if (!error.empty()) return error::Error(error);
    if (status_code && (status_code < 200 || status_code > 299)) {
      return error::Error("failed with HTTP status code " +
                          std::to_string(status_code));
    }
    return error::SUCCESS;
  }

 private:
  std::string response_;
  bool continue100_ = false;
  bool status_code_read_ = false;
  bool headers_read_ = false;

  error::Error ReadStatusCode();
  error::Error ReadHeaders();
};  // struct Response
}  // namespace http
}  // namespace minio
#endif  // #ifndef _MINIO_HTTP_H
