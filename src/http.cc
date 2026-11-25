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

#include "miniocpp/http.h"

#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "miniocpp/error.h"
#include "miniocpp/utils.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <ws2def.h>    // NOTE needed for AF_INET6
#include <ws2ipdef.h>  // NOTE needed for sockaddr_in6
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace minio::http {

// MethodToString converts http Method enum to string.
const char* MethodToString(Method method) noexcept {
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
  return nullptr;
}

std::string Url::String() const {
  if (host.empty()) return {};

  std::string url = (https ? "https://" : "http://") + host;
  if (port) url += ":" + std::to_string(port);
  if (!path.empty()) {
    if (path.front() != '/') url += '/';
    url += path;
  }
  if (!query_string.empty()) url += "?" + query_string;

  return url;
}

std::string Url::HostHeaderValue() const {
  if (!port) {
    return host;
  }
  return host + ":" + std::to_string(port);
}

Url Url::Parse(std::string value) {
  std::string scheme;
  size_t pos = value.find("://");
  if (pos != std::string::npos) {
    scheme = value.substr(0, pos);
    value.erase(0, pos + 3);
  }
  scheme = utils::ToLower(scheme);

  if (!scheme.empty() && scheme != "http" && scheme != "https") return Url{};

  bool https = (scheme.empty() || scheme == "https");

  std::string host;
  std::string path;
  std::string query_string;
  pos = value.find("/");
  if (pos != std::string::npos) {
    host = value.substr(0, pos);
    value.erase(0, pos + 1);

    pos = value.find("?");
    if (pos != std::string::npos) {
      path = value.substr(0, pos);
      value.erase(0, pos + 1);
      query_string = value;
    } else {
      path = value;
    }
  } else {
    pos = value.find("?");
    if (pos != std::string::npos) {
      host = value.substr(0, pos);
      value.erase(0, pos + 1);
      query_string = value;
    } else {
      host = value;
    }
  }

  if (host.empty()) {
    return Url{};
  }

  unsigned int port = 0;
  struct sockaddr_in6 dst;
  if (inet_pton(AF_INET6, host.c_str(), &(dst.sin6_addr)) <= 0) {
    if (host.front() != '[' || host.back() != ']') {
      std::stringstream ss(host);
      std::string portstr;
      while (std::getline(ss, portstr, ':')) {
      }

      if (host.find(':') != std::string::npos && !portstr.empty()) {
        try {
          port = static_cast<unsigned>(std::stoi(portstr));
          host = host.substr(0, host.rfind(":" + portstr));
        } catch (const std::invalid_argument&) {
          port = 0;
        }
      }
    }
  } else {
    host = "[" + host + "]";
  }

  if (!https && port == 80) port = 0;
  if (https && port == 443) port = 0;

  return Url(https, std::move(host), port, std::move(path),
             std::move(query_string));
}

error::Error Response::Error() const {
  if (!error.empty()) return error::Error(error);
  if (status_code && (status_code < 200 || status_code > 299)) {
    return error::Error("failed with HTTP status code " +
                        std::to_string(status_code));
  }
  return error::SUCCESS;
}

Request::Request(Method method, Url url) {
  this->method = method;
  this->url = url;
  std::string ssl_cert_file;
  if (url.https && utils::GetEnv(ssl_cert_file, "SSL_CERT_FILE")) {
    this->ssl_cert_file = ssl_cert_file;
  }
}

}  // namespace minio::http
