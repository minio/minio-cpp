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

#include <curl/curl.h>

#include <cerrno>
#include <chrono>
#include <curlpp/Easy.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Multi.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <exception>
#include <functional>
#include <iosfwd>
#include <iostream>
#include <list>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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
// which would otherwise keep the request alive indefinitely.
constexpr long kStallTimeoutSecs = 60;

// curl_global_init() is documented as not thread-safe and is expensive
// (OpenSSL init etc). Run it exactly once per process via a function-local
// static (Meyers singleton; C++11 [stmt.dcl]/4 guarantees thread-safe
// initialization), instead of paying the cost — and the race — on every
// request via a stack-local curlpp::Cleanup.
void EnsureGlobalCurlInit() {
  static const curlpp::Cleanup kCleanup;
  (void)kCleanup;
}

// One mutex per CURL_LOCK_DATA_* slot. A single global mutex deadlocks
// because libcurl can hold one slot's lock (e.g. SSL_SESSION) while
// acquiring another (e.g. CONNECT) on the same thread.
constexpr int kCurlShareSlots = CURL_LOCK_DATA_LAST;
std::mutex* CurlShareMutexes() {
  static std::mutex m[kCurlShareSlots];
  return m;
}

void CurlShareLockCb(CURL*, curl_lock_data data, curl_lock_access, void*) {
  if (data >= 0 && data < kCurlShareSlots) CurlShareMutexes()[data].lock();
}

void CurlShareUnlockCb(CURL*, curl_lock_data data, void*) {
  if (data >= 0 && data < kCurlShareSlots) CurlShareMutexes()[data].unlock();
}

// Process-wide CURLSH that keeps the connection cache, DNS cache, and TLS
// session cache alive across requests. Without this, every Request::execute()
// constructs a fresh curlpp::Easy whose private caches are discarded on
// destruction — forcing a full TLS handshake on every signed S3 call.
CURLSH* GlobalCurlShare() {
  static CURLSH* const share = [] {
    EnsureGlobalCurlInit();
    CURLSH* s = curl_share_init();
    if (s == nullptr) {
      std::cerr << "curl_share_init failed" << std::endl;
      std::terminate();
    }
    curl_share_setopt(s, CURLSHOPT_LOCKFUNC, &CurlShareLockCb);
    curl_share_setopt(s, CURLSHOPT_UNLOCKFUNC, &CurlShareUnlockCb);
    curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    return s;
  }();
  return share;
}

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

      if (!portstr.empty()) {
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

error::Error Response::ReadStatusCode() {
  size_t pos = response_.find("\r\n");
  if (pos == std::string::npos) {
    // Not yet received the first line.
    return error::SUCCESS;
  }

  std::string line = response_.substr(0, pos);
  response_.erase(0, pos + 2);

  if (continue100_) {
    if (!line.empty()) {
      // After '100 Continue', next line must be empty new line.
      return error::Error("invalid HTTP response");
    }

    continue100_ = false;

    pos = response_.find("\r\n");
    if (pos == std::string::npos) {
      // Not yet received the first line after '100 Continue'.
      return error::SUCCESS;
    }

    line = response_.substr(0, pos);
    response_.erase(0, pos + 2);
  }

  // Skip HTTP/1.x.
  pos = line.find(" ");
  if (pos == std::string::npos) {
    // First token must be HTTP/1.x
    return error::Error("invalid HTTP response");
  }
  line = line.substr(pos + 1);

  // Read status code.
  pos = line.find(" ");
  if (pos == std::string::npos) {
    // The line must contain second token.
    return error::Error("invalid HTTP response");
  }
  std::string code = line.substr(0, pos);
  std::string::size_type st;
  status_code = std::stoi(code, &st);
  if (st == std::string::npos) {
    // Code must be a number.
    return error::Error("invalid HTTP response code " + code);
  }

  if (status_code == 100) {
    continue100_ = true;
  } else {
    status_code_read_ = true;
  }

  return error::SUCCESS;
}

error::Error Response::Error() const {
  if (!error.empty()) return error::Error(error);
  if (status_code && (status_code < 200 || status_code > 299)) {
    return error::Error("failed with HTTP status code " +
                        std::to_string(status_code));
  }
  return error::SUCCESS;
}

error::Error Response::ReadHeaders() {
  size_t pos = response_.find("\r\n\r\n");
  if (pos == std::string::npos) {
    // Not yet received the headers.
    return error::SUCCESS;
  }

  headers_read_ = true;

  std::string lines = response_.substr(0, pos);
  response_.erase(0, pos + 4);

  auto add_header = [&headers = headers](std::string line) -> error::Error {
    size_t pos = line.find(": ");
    if (pos != std::string::npos) {
      headers.Add(line.substr(0, pos), line.substr(pos + 2));
      return error::SUCCESS;
    }

    return error::Error("invalid HTTP header: " + line);
  };

  while ((pos = lines.find("\r\n")) != std::string::npos) {
    std::string line = lines.substr(0, pos);
    lines.erase(0, pos + 2);
    if (error::Error err = add_header(line)) return err;
  }

  if (!lines.empty()) {
    if (error::Error err = add_header(lines)) return err;
  }

  return error::SUCCESS;
}

size_t Response::ResponseCallback(curlpp::Multi* const requests,
                                  curlpp::Easy* const request,
                                  const char* const buffer, size_t size,
                                  size_t length) {
  size_t realsize = size * length;

  // If error occurred previously, just cancel the request.
  if (!error.empty()) {
    requests->remove(request);
    return realsize;
  }

  if (!status_code_read_ || !headers_read_) {
    response_.append(buffer, length);
  }

  if (!status_code_read_) {
    if (error::Error err = ReadStatusCode()) {
      error = err.String();
      requests->remove(request);
      return realsize;
    }

    if (!status_code_read_) return realsize;
  }

  if (!headers_read_) {
    if (error::Error err = ReadHeaders()) {
      error = err.String();
      requests->remove(request);
      return realsize;
    }

    if (!headers_read_ || response_.empty()) return realsize;

    // If data function is set and the request is successful, send data.
    if (datafunc != nullptr && status_code >= 200 && status_code <= 299) {
      DataFunctionArgs args(request, this, std::string(this->response_),
                            userdata);
      if (!datafunc(args)) requests->remove(request);
    } else {
      body = response_;
    }

    return realsize;
  }

  // If data function is set and the request is successful, send data.
  if (datafunc != nullptr && status_code >= 200 && status_code <= 299) {
    DataFunctionArgs args(request, this, std::string(buffer, length), userdata);
    if (!datafunc(args)) requests->remove(request);
  } else {
    body.append(buffer, length);
  }

  return realsize;
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
  EnsureGlobalCurlInit();
  curlpp::Easy request;
  curlpp::Multi requests;

  // Attach the process-wide share so connections, DNS resolutions, and TLS
  // sessions survive past this Easy handle's lifetime. Also enable TCP
  // keep-alive so the kernel keeps pooled sockets healthy across idle gaps
  // between S3 calls. curlpp doesn't wrap either option, so set via libcurl.
  CURL* const raw_handle = request.getHandle();
  curl_easy_setopt(raw_handle, CURLOPT_SHARE, GlobalCurlShare());
  curl_easy_setopt(raw_handle, CURLOPT_TCP_KEEPALIVE, 1L);

  // Fail a stalled transfer instead of hanging forever. Skipped when the caller
  // set an explicit total timeout (RDMA control plane) — that already bounds
  // it.
  if (timeout_secs <= 0) {
    curl_easy_setopt(raw_handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(raw_handle, CURLOPT_LOW_SPEED_TIME, kStallTimeoutSecs);
  }

  // Request settings.
  request.setOpt(new curlpp::options::CustomRequest{MethodToString(method)});
  std::string urlstring = url.String();
  request.setOpt(new curlpp::Options::Url(urlstring));
  if (debug) request.setOpt(new curlpp::Options::Verbose(true));
  if (ignore_cert_check) {
    request.setOpt(new curlpp::Options::SslVerifyPeer(false));
    request.setOpt(new curlpp::Options::SslVerifyHost(0L));
  }

  if (url.https) {
    if (!ssl_cert_file.empty()) {
      request.setOpt(new curlpp::Options::SslVerifyPeer(true));
      request.setOpt(new curlpp::Options::CaInfo(ssl_cert_file));
    }
    if (!key_file.empty()) {
      request.setOpt(new curlpp::Options::SslKey(key_file));
    }
    if (!cert_file.empty()) {
      request.setOpt(new curlpp::Options::SslCert(cert_file));
    }
  }

  if (!nic_interface.empty()) {
    request.setOpt(new curlpp::Options::Interface(nic_interface));
  }
  if (connect_timeout_secs > 0) {
    request.setOpt(new curlpp::Options::ConnectTimeout(connect_timeout_secs));
  }
  if (timeout_secs > 0) {
    request.setOpt(new curlpp::Options::Timeout(timeout_secs));
  }

  utils::CharBuffer charbuf((char*)body.data(), body.size());
  std::istream body_stream(&charbuf);

  switch (method) {
    case Method::kDelete:
    case Method::kGet:
      break;
    case Method::kHead:
      request.setOpt(new curlpp::options::NoBody(true));
      break;
    case Method::kPut:
    case Method::kPost:
      if (!headers.Contains("Content-Length")) {
        headers.Add("Content-Length", std::to_string(body.size()));
      }
      request.setOpt(new curlpp::Options::ReadStream(&body_stream));
      request.setOpt(
          new curlpp::Options::InfileSize(static_cast<long>(body.size())));
      request.setOpt(new curlpp::Options::Upload(true));
      break;
  }

  std::list<std::string> headerlist = headers.ToHttpHeaders();
  headerlist.push_back("Expect:");  // Disable 100 continue from server.
  request.setOpt(new curlpp::Options::HttpHeader(headerlist));

  // Response settings.
  request.setOpt(new curlpp::options::Header(true));

  Response response;
  response.datafunc = datafunc;
  response.userdata = userdata;

  using namespace std::placeholders;
  request.setOpt(new curlpp::options::WriteFunction(
      std::bind(&Response::ResponseCallback, &response, &requests, &request, _1,
                _2, _3)));

  auto progress =
      [&progressfunc = progressfunc, &progress_userdata = progress_userdata](
          double dltotal, double dlnow, double ultotal, double ulnow) -> int {
    ProgressFunctionArgs args;
    args.download_total_bytes = dltotal;
    args.downloaded_bytes = dlnow;
    args.upload_total_bytes = ultotal;
    args.uploaded_bytes = ulnow;
    args.userdata = progress_userdata;
    if (progressfunc(args)) {
      return CURL_PROGRESSFUNC_CONTINUE;
    }
    return 1;
  };
  if (progressfunc != nullptr) {
    request.setOpt(new curlpp::options::NoProgress(false));
    request.setOpt(new curlpp::options::ProgressFunction(progress));
  }

  int left = 0;
  requests.add(&request);

  // Execute.
  while (!requests.perform(&left)) {
  }
  while (left) {
    fd_set fdread{};
    fd_set fdwrite{};
    fd_set fdexcep{};
    int maxfd = -1;

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);

    requests.fdset(&fdread, &fdwrite, &fdexcep, &maxfd);

    // Bound the wait so the loop keeps pumping libcurl even when no socket ever
    // becomes ready — otherwise a dropped/stalled connection blocks select()
    // forever and this (synchronous) call hangs the calling thread. The bounded
    // poll lets libcurl enforce its own timeouts (e.g. the low-speed limit set
    // above) and abort the dead transfer.
    if (maxfd < 0) {
      // libcurl has no fd to wait on yet; select() with empty sets errors out
      // on Windows, so just poll again shortly.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      timeval timeout{};
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
      if (select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout) < 0) {
#ifndef _WIN32
        if (errno == EINTR) continue;  // interrupted by a signal; retry
#endif
        std::cerr << "select() failed; this should not happen" << std::endl;
        std::terminate();
      }
    }
    while (!requests.perform(&left)) {
    }
  }

  // The loop exits once libcurl has no running transfers left. If the transfer
  // aborted before delivering a single byte (e.g. the low-speed limit or
  // connect timeout fired on a dropped/stalled connection), the write callback
  // never ran, so neither status_code nor error was set. Surface a diagnostic
  // instead of returning a silently-empty failure.
  if (response.error.empty() && response.status_code == 0) {
    response.error =
        "transfer ended without a response (connection dropped, timed out, or "
        "was aborted before any data was received)";
  }

  if (progressfunc != nullptr) {
    ProgressFunctionArgs args;
    args.userdata = progress_userdata;
    curlpp::infos::SpeedUpload::get(request, args.upload_speed);
    curlpp::infos::SpeedDownload::get(request, args.download_speed);
    progressfunc(args);
  }

  return response;
}

Response Request::Execute() {
  try {
    return execute();
  } catch (curlpp::LogicError& e) {
    Response response;
    response.error = std::string("curlpp::LogicError: ") + e.what();
    return response;
  } catch (curlpp::RuntimeError& e) {
    Response response;
    response.error = std::string("curlpp::RuntimeError: ") + e.what();
    return response;
  }
}

}  // namespace minio::http
