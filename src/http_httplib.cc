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

// cpp-httplib backend.
//
// Limitations of the library, not of this wrapper - a caller that needs any of
// these picks the curl (libcurl) backend instead:
//   - blocking socket I/O: one thread per in-flight request, no event loop;
//   - HTTP/1.1 only: no HTTP/2 or HTTP/3, so concurrent requests are not
//     multiplexed onto one connection;
//   - no proxy configured from the environment (HTTPS_PROXY and friends);
//   - no libcurl-style low-speed limit: a stalled transfer is bounded by the
//     read/write timeout below instead.

#include "miniocpp/http.h"

// cpp-httplib's OpenSSL backend is enabled once, target-wide, via
// target_compile_definitions(miniocpp PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT): the
// header-only library's class layout depends on that macro, so every
// translation unit must agree on it.
#include <httplib.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "miniocpp/error.h"
#include "miniocpp/utils.h"

namespace minio::http {

namespace {

// Abort a transfer that makes no progress for this long. Guards against a
// connection that drops mid-transfer without a clean close (TCP never RSTs),
// which would otherwise keep the request alive indefinitely. cpp-httplib
// enforces it as a read/write timeout, since it has no low-speed limit.
constexpr long kStallTimeoutSecs = 60;

}  // namespace

// httplib::Client is bound to one endpoint and its configuration is set on the
// client, not per request, so clients are pooled per configuration and leased
// to one request at a time. Reusing them is what carries the keep-alive
// connection over to the next S3 call; leasing keeps concurrent requests from
// the *Async overloads off each other's sockets.
struct Client::Impl {
  using Pool =
      std::map<std::string, std::vector<std::unique_ptr<httplib::Client>>>;

  Response Perform(Request& req);

  std::unique_ptr<httplib::Client> Acquire(
      const std::string& key,
      const std::function<std::unique_ptr<httplib::Client>()>& create) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = idle_.find(key);
    if (it != idle_.end() && !it->second.empty()) {
      std::unique_ptr<httplib::Client> client = std::move(it->second.back());
      it->second.pop_back();
      return client;
    }
    return create();
  }

  void Release(const std::string& key,
               std::unique_ptr<httplib::Client> client) {
    std::lock_guard<std::mutex> lock(mutex_);
    idle_[key].push_back(std::move(client));
  }

  std::mutex mutex_;
  Pool idle_;
};

Response Client::Impl::Perform(Request& req) {
  Response response;
  response.datafunc = req.datafunc;
  response.userdata = req.userdata;

  std::string endpoint = (req.url.https ? "https://" : "http://") + req.url.host;
  if (req.url.port) endpoint += ":" + std::to_string(req.url.port);

  // Everything set on the client rather than per request belongs in the key:
  // a lease must come back configured the same way it went out.
  std::string key = endpoint + "|" + req.cert_file + "|" + req.key_file + "|" +
                    req.nic_interface + "|" + req.ssl_cert_file + "|" +
                    (req.ignore_cert_check ? "1" : "0") + "|" +
                    std::to_string(req.connect_timeout_secs) + "|" +
                    std::to_string(req.timeout_secs);

  std::unique_ptr<httplib::Client> lease = Acquire(key, [&req, &endpoint] {
    return std::make_unique<httplib::Client>(endpoint, req.cert_file,
                                             req.key_file);
  });
  struct Releaser {
    Impl* impl;
    const std::string* key;
    std::unique_ptr<httplib::Client>* lease;
    ~Releaser() { impl->Release(*key, std::move(*lease)); }
  } releaser{this, &key, &lease};

  if (!lease->is_valid()) {
    response.error = "unable to create HTTP client for " + endpoint;
    return response;
  }
  httplib::Client& cli = *lease;

  // httplib's default 5s read/write timeout is too short for S3 transfers;
  // use the 60s stall guard unless the caller set an explicit total timeout.
  cli.set_keep_alive(true);
  cli.set_follow_location(false);
  // Paths are pre-encoded by the caller (EncodePath).
  cli.set_path_encode(false);
  if (req.connect_timeout_secs > 0) {
    cli.set_connection_timeout(req.connect_timeout_secs, 0);
  }
  if (req.timeout_secs > 0) {
    // Total transfer deadline, like CURLOPT_TIMEOUT in the curl backend.
    cli.set_max_timeout(static_cast<time_t>(req.timeout_secs) * 1000);
  } else {
    cli.set_read_timeout(kStallTimeoutSecs, 0);
    cli.set_write_timeout(kStallTimeoutSecs, 0);
  }
  if (req.debug) {
    cli.set_logger(
        [](const httplib::Request& hreq, const httplib::Response& hres) {
          std::cerr << hreq.method << " " << hreq.path << " -> " << hres.status
                    << std::endl;
        });
  }
  if (!req.nic_interface.empty()) cli.set_interface(req.nic_interface);
  if (req.url.https) {
    // An explicit CA bundle overrides IGNORE_CERT_CHECK, matching the curl
    // backend: verification is enabled against the given CA file.
    if (!req.ssl_cert_file.empty()) {
      cli.set_ca_cert_path(req.ssl_cert_file);
      cli.enable_server_certificate_verification(true);
    } else {
      cli.enable_server_certificate_verification(!req.ignore_cert_check);
    }
  }

  httplib::Headers request_headers;
  for (const auto& key : req.headers.Keys()) {
    for (const auto& value : req.headers.Get(key)) {
      request_headers.insert({key, value});
    }
  }
  // httplib sets Host itself when absent and derives Content-Length from the
  // body; the SigV4-signed Host must be sent verbatim.  An empty Expect
  // disables the 100-continue handshake, as the curl backend does.
  request_headers.erase("Content-Length");
  request_headers.emplace("Expect", "");
  std::string content_type = req.headers.GetFront("Content-Type");
  request_headers.erase("Content-Type");

  std::string path = req.url.path;
  if (path.empty()) {
    path = "/";
  } else if (path.front() != '/') {
    path = "/" + path;
  }
  if (!req.url.query_string.empty()) path += "?" + req.url.query_string;

  // Track bytes and elapsed time to report average speeds in the final
  // progress call, as the curl backend does.
  auto start_time = std::chrono::steady_clock::now();
  size_t bytes_downloaded = 0;
  size_t bytes_uploaded = 0;
  auto report_speed = [&req, start_time, &bytes_downloaded,
                       &bytes_uploaded]() {
    if (req.progressfunc == nullptr) return;
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      start_time)
            .count();
    ProgressFunctionArgs args;
    if (elapsed > 0) {
      args.download_speed = static_cast<double>(bytes_downloaded) / elapsed;
      args.upload_speed = static_cast<double>(bytes_uploaded) / elapsed;
    }
    args.userdata = req.progress_userdata;
    req.progressfunc(args);
  };

  auto download_progress = [&req, &bytes_downloaded](size_t current,
                                                     size_t total) -> bool {
    bytes_downloaded = current;
    if (req.progressfunc == nullptr) return true;
    ProgressFunctionArgs args;
    args.download_total_bytes = static_cast<double>(total);
    args.downloaded_bytes = static_cast<double>(current);
    args.userdata = req.progress_userdata;
    return req.progressfunc(args);
  };
  auto upload_progress = [&req, &bytes_uploaded](size_t current,
                                                 size_t total) -> bool {
    bytes_uploaded = current;
    if (req.progressfunc == nullptr) return true;
    ProgressFunctionArgs args;
    args.upload_total_bytes = static_cast<double>(total);
    args.uploaded_bytes = static_cast<double>(current);
    args.userdata = req.progress_userdata;
    return req.progressfunc(args);
  };

  // Stream the response body to the data function; a false return aborts the
  // transfer, recorded so a caller-initiated abort is not reported as an
  // error below (e.g. ListenBucketNotification stops once it has its records).
  // Non-2xx bodies are buffered instead so error payloads reach the caller
  // (the GET response handler gates this on the status; the POST overloads
  // have no response handler, so Select always streams and surfaces errors
  // through the event stream).
  bool datafunc_canceled = false;
  bool stream_to_datafunc = true;
  httplib::ContentReceiver content_receiver =
      [&req, &response, &datafunc_canceled,
       &stream_to_datafunc](const char* data, size_t length) -> bool {
    if (!stream_to_datafunc) {
      response.body.append(data, length);
      return true;
    }
    DataFunctionArgs args(&response, std::string(data, length),
                          req.userdata);
    const bool cont = req.datafunc(args);
    if (!cont) datafunc_canceled = true;
    return cont;
  };

  httplib::Result res;
  httplib::ResponseHandler response_handler =
      [&response, &stream_to_datafunc](const httplib::Response& hres) -> bool {
    // Status is known here, before any body is streamed, so a caller-
    // initiated cancel still yields a response with the correct status.
    response.status_code = hres.status;
    stream_to_datafunc = hres.status >= 200 && hres.status <= 299;
    return true;
  };
  switch (req.method) {
    case Method::kGet:
      if (req.datafunc != nullptr) {
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
      if (req.datafunc != nullptr) {
        // Stream the response body to the data function (e.g. the S3 Select
        // event stream); the request body here is small, so a copy is fine.
        res = cli.Post(path, request_headers,
                       std::string(req.body.data(), req.body.size()),
                       content_type, content_receiver, download_progress);
      } else {
        res = cli.Post(path, request_headers, req.body.data(), req.body.size(),
                       content_type, upload_progress);
      }
      break;
    case Method::kPut:
      res = cli.Put(path, request_headers, req.body.data(), req.body.size(),
                    content_type, upload_progress);
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
      report_speed();
      return response;
    }
    response.error = httplib::to_string(res.error());
    report_speed();
    return response;
  }

  response.status_code = res->status;
  for (const auto& [key, value] : res->headers) {
    response.headers.Add(key, value);
  }
  // With a data function the body is streamed to it; otherwise keep the
  // buffered response body (including error payloads for non-2xx statuses).
  if (req.datafunc == nullptr) response.body = res->body;

  report_speed();
  return response;
}

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() = default;

Client::Client(Client&&) noexcept = default;

Client& Client::operator=(Client&&) noexcept = default;

Response Client::Execute(Request& request) {
  try {
    return impl_->Perform(request);
  } catch (const std::exception& e) {
    Response response;
    response.error = std::string("HTTP error: ") + e.what();
    return response;
  }
}

}  // namespace minio::http
