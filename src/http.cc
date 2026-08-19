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

// cpp-httplib's OpenSSL backend is enabled once, target-wide, via
// target_compile_definitions(miniocpp PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT): the
// header-only library's class layout depends on that macro, so every
// translation unit must agree on it.
#include <httplib.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <iosfwd>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

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

namespace {

// Abort a transfer that makes no progress for this long. Guards against a
// connection that drops mid-transfer without a clean close (TCP never RSTs),
// which would otherwise keep the request alive indefinitely. cpp-httplib
// enforces it as a read/write timeout, since it has no low-speed limit.
constexpr long kStallTimeoutSecs = 60;

}  // namespace

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

Response Request::execute() {
  Response response;
  response.datafunc = datafunc;
  response.userdata = userdata;

  // httplib::Client is bound to one endpoint and not thread-safe; build a
  // fresh one per request.
  std::string endpoint = (url.https ? "https://" : "http://") + url.host;
  if (url.port) endpoint += ":" + std::to_string(url.port);

  auto client =
      std::make_unique<httplib::Client>(endpoint, cert_file, key_file);
  if (!client->is_valid()) {
    response.error = "unable to create HTTP client for " + endpoint;
    return response;
  }
  httplib::Client& cli = *client;

  // httplib's default 5s read/write timeout is too short for S3 transfers;
  // use the 60s stall guard unless the caller set an explicit timeout.
  cli.set_keep_alive(true);
  cli.set_follow_location(false);
  // Paths are pre-encoded by the caller (EncodePath).
  cli.set_path_encode(false);
  if (connect_timeout_secs > 0) {
    cli.set_connection_timeout(connect_timeout_secs, 0);
  }
  if (timeout_secs > 0) {
    cli.set_read_timeout(timeout_secs, 0);
    cli.set_write_timeout(timeout_secs, 0);
  } else {
    cli.set_read_timeout(kStallTimeoutSecs, 0);
    cli.set_write_timeout(kStallTimeoutSecs, 0);
  }
  if (!nic_interface.empty()) cli.set_interface(nic_interface);
  if (url.https) {
    cli.enable_server_certificate_verification(!ignore_cert_check);
    if (!ssl_cert_file.empty()) cli.set_ca_cert_path(ssl_cert_file);
  }

  httplib::Headers request_headers;
  for (const auto& key : headers.Keys()) {
    for (const auto& value : headers.Get(key)) {
      request_headers.insert({key, value});
    }
  }
  // httplib sets Host itself when absent and derives Content-Length from the
  // body; the SigV4-signed Host must be sent verbatim.
  request_headers.erase("Content-Length");
  std::string content_type = headers.GetFront("Content-Type");
  request_headers.erase("Content-Type");

  std::string path = url.path;
  if (path.empty()) {
    path = "/";
  } else if (path.front() != '/') {
    path = "/" + path;
  }
  if (!url.query_string.empty()) path += "?" + url.query_string;

  auto download_progress = [this](size_t current, size_t total) -> bool {
    if (progressfunc == nullptr) return true;
    ProgressFunctionArgs args;
    args.download_total_bytes = total;
    args.downloaded_bytes = current;
    args.userdata = progress_userdata;
    return progressfunc(args);
  };
  auto upload_progress = [this](size_t current, size_t total) -> bool {
    if (progressfunc == nullptr) return true;
    ProgressFunctionArgs args;
    args.upload_total_bytes = total;
    args.uploaded_bytes = current;
    args.userdata = progress_userdata;
    return progressfunc(args);
  };

  // Stream the response body to the data function; a false return aborts the
  // transfer, recorded so a caller-initiated abort is not reported as an
  // error below (e.g. ListenBucketNotification stops once it has its records).
  bool datafunc_canceled = false;
  httplib::ContentReceiver content_receiver =
      [this, &response, &datafunc_canceled](const char* data,
                                            size_t length) -> bool {
    DataFunctionArgs args(nullptr, &response, std::string(data, length),
                          userdata);
    const bool cont = datafunc(args);
    if (!cont) datafunc_canceled = true;
    return cont;
  };

  httplib::Result res;
  std::string body_str(body.data(), body.size());
  httplib::ResponseHandler response_handler =
      [&response](const httplib::Response& res) -> bool {
    // Status is known here, before any body is streamed, so a caller-
    // initiated cancel still yields a response with the correct status.
    response.status_code = res.status;
    return true;
  };
  switch (method) {
    case Method::kGet:
      if (datafunc != nullptr) {
        res = cli.Get(path, request_headers, response_handler, content_receiver,
                      download_progress);
      } else {
        res = cli.Get(path, request_headers, download_progress);
      }
      break;
    case Method::kHead:
      res = cli.Head(path, request_headers);
      break;
    case Method::kPost:
      if (datafunc != nullptr) {
        // Stream the response body to the data function (e.g. the S3 Select
        // event stream); without this the buffered body is dropped.
        res = cli.Post(path, request_headers, body_str, content_type,
                       content_receiver, download_progress);
      } else {
        res = cli.Post(path, request_headers, body_str, content_type,
                       upload_progress);
      }
      break;
    case Method::kPut:
      res = cli.Put(path, request_headers, body_str, content_type,
                    upload_progress);
      break;
    case Method::kDelete:
      res = cli.Delete(path, request_headers, download_progress);
      break;
  }

  if (!res) {
    // A false return from the data function cancels the transfer; for
    // streaming callers that is a normal completion, not an error.  GET
    // captures the status via the response handler; httplib discards the
    // response when a POST receiver cancels, so report success there since
    // the caller ended the transfer itself.
    if (res.error() == httplib::Error::Canceled && datafunc_canceled) {
      if (response.status_code == 0) response.status_code = 200;
      return response;
    }
    response.error = httplib::to_string(res.error());
    return response;
  }

  response.status_code = res->status;
  for (const auto& [key, value] : res->headers) {
    response.headers.Add(key, value);
  }
  // With a data function the body is streamed to it; otherwise keep the
  // buffered response body (including error payloads for non-2xx statuses).
  if (datafunc == nullptr) response.body = res->body;

  return response;
}

Response Request::Execute() {
  try {
    return execute();
  } catch (const std::exception& e) {
    Response response;
    response.error = std::string("HTTP error: ") + e.what();
    return response;
  }
}

}  // namespace minio::http
