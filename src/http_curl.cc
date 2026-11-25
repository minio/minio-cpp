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

#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "miniocpp/error.h"

namespace minio::http {

namespace {

// Abort a transfer that makes no progress for this long. Guards against a
// connection that drops mid-transfer without a clean close (TCP never RSTs),
// which would otherwise keep the request alive indefinitely.
constexpr long kStallTimeoutSecs = 60;

// How long a waiting request blocks in curl_multi_poll() before looking at the
// completion state again. The poll runs under mutex_, so this is also the
// worst-case wait for another thread's curl_multi_add_handle() while every
// connection is idle - curl_multi_wakeup() cannot help there, because it is
// only sent after the add, which needs the lock back first. Keep it short
// enough that admission onto a keep-alive connection does not cost a visible
// delay; the cost is a cheap perform pass per timeout.
constexpr long kPollTimeoutMs = 100;

// Idle easy handles kept for reuse. Handles are cheap next to a transfer; the
// cap only stops a burst of concurrency from pinning memory forever. The
// connections themselves are pooled by the multi handle, not here.
constexpr size_t kMaxIdleHandles = 16;

// curl_global_init() is documented as not thread-safe and is expensive
// (OpenSSL init etc). Run it exactly once per process via a function-local
// static (Meyers singleton; C++11 [stmt.dcl]/4 guarantees thread-safe
// initialization), instead of paying the cost - and the race - on every
// request.
struct GlobalCurlInit {
  GlobalCurlInit() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      std::cerr << "curl_global_init failed" << std::endl;
      std::terminate();
    }
  }
  ~GlobalCurlInit() { curl_global_cleanup(); }
  GlobalCurlInit(const GlobalCurlInit&) = delete;
  GlobalCurlInit& operator=(const GlobalCurlInit&) = delete;
};

void EnsureGlobalCurlInit() {
  static const GlobalCurlInit kInit;
  (void)kInit;
}

// One easy handle: the per-request workspace. Connections are not owned here.
// The multi handle owns the connection cache, so a handle can be leased to any
// thread - libcurl allows an easy handle in different threads as long as it is
// not used by two at once - and curl_easy_reset() drops the previous request's
// options (including pointers into the stack frame that set them).
class EasyHandle {
 public:
  EasyHandle() : handle_(curl_easy_init()) {
    if (handle_ == nullptr) {
      std::cerr << "curl_easy_init failed" << std::endl;
      std::terminate();
    }
  }
  ~EasyHandle() { curl_easy_cleanup(handle_); }
  EasyHandle(const EasyHandle&) = delete;
  EasyHandle& operator=(const EasyHandle&) = delete;

  CURL* get() const { return handle_; }
  void Reset() { curl_easy_reset(handle_); }

 private:
  CURL* handle_;
};

class HeaderList {
 public:
  HeaderList() = default;
  ~HeaderList() {
    if (list_ != nullptr) curl_slist_free_all(list_);
  }
  HeaderList(const HeaderList&) = delete;
  HeaderList& operator=(const HeaderList&) = delete;

  void Append(const std::string& header) {
    curl_slist* list = curl_slist_append(list_, header.c_str());
    if (list != nullptr) list_ = list;
  }

  curl_slist* get() const { return list_; }

 private:
  curl_slist* list_ = nullptr;
};

// libcurl hands the write callback the status line and headers interleaved
// with the body, so the raw bytes are parsed here until the header block is
// complete; after that, body bytes go to the data function or are buffered.
//
// This state used to live in http::Response itself, which put libcurl's
// personality into the installed header. It is backend-private now.
class CurlTransferState {
 public:
  explicit CurlTransferState(Response& response) : response_(response) {}

  size_t Write(const char* buffer, size_t size, size_t length);

  bool aborted() const { return aborted_; }

 private:
  error::Error ReadStatusCode();
  error::Error ReadHeaders();

  // Returning less than the byte count aborts the transfer with
  // CURLE_WRITE_ERROR; the response keeps the specific error instead.
  size_t Abort(const error::Error& err) {
    response_.error = err.String();
    aborted_ = true;
    return 0;
  }
  size_t AbortOnDataFunctionFailure() {
    aborted_ = true;
    return 0;
  }

  Response& response_;
  std::string pending_;
  bool continue100_ = false;
  bool status_code_read_ = false;
  bool headers_read_ = false;
  bool aborted_ = false;
};

error::Error CurlTransferState::ReadStatusCode() {
  size_t pos = pending_.find("\r\n");
  if (pos == std::string::npos) {
    // Not yet received the first line.
    return error::SUCCESS;
  }

  std::string line = pending_.substr(0, pos);
  pending_.erase(0, pos + 2);

  if (continue100_) {
    if (!line.empty()) {
      // After '100 Continue', next line must be empty new line.
      return error::Error("invalid HTTP response");
    }

    continue100_ = false;

    pos = pending_.find("\r\n");
    if (pos == std::string::npos) {
      // Not yet received the first line after '100 Continue'.
      return error::SUCCESS;
    }

    line = pending_.substr(0, pos);
    pending_.erase(0, pos + 2);
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
  response_.status_code = std::stoi(code, &st);
  if (st == std::string::npos) {
    // Code must be a number.
    return error::Error("invalid HTTP response code " + code);
  }

  if (response_.status_code == 100) {
    continue100_ = true;
  } else {
    status_code_read_ = true;
  }

  return error::SUCCESS;
}

error::Error CurlTransferState::ReadHeaders() {
  size_t pos = pending_.find("\r\n\r\n");
  if (pos == std::string::npos) {
    // Not yet received the headers.
    return error::SUCCESS;
  }

  headers_read_ = true;

  std::string lines = pending_.substr(0, pos);
  pending_.erase(0, pos + 4);

  auto add_header = [&headers = response_.headers](std::string line)
      -> error::Error {
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

size_t CurlTransferState::Write(const char* buffer, size_t size,
                                size_t length) {
  size_t realsize = size * length;

  // If error occurred previously, just cancel the request.
  if (aborted_) return 0;

  if (!status_code_read_ || !headers_read_) {
    pending_.append(buffer, length);
  }

  if (!status_code_read_) {
    if (error::Error err = ReadStatusCode()) return Abort(err);

    if (!status_code_read_) return realsize;
  }

  if (!headers_read_) {
    if (error::Error err = ReadHeaders()) return Abort(err);

    if (!headers_read_ || pending_.empty()) return realsize;

    // If data function is set and the request is successful, send data.
    if (response_.datafunc != nullptr && response_.status_code >= 200 &&
        response_.status_code <= 299) {
      DataFunctionArgs args(&response_, std::string(pending_),
                            response_.userdata);
      if (!response_.datafunc(args)) return AbortOnDataFunctionFailure();
    } else {
      response_.body = pending_;
    }

    return realsize;
  }

  // If data function is set and the request is successful, send data.
  if (response_.datafunc != nullptr && response_.status_code >= 200 &&
      response_.status_code <= 299) {
    DataFunctionArgs args(&response_, std::string(buffer, length),
                          response_.userdata);
    if (!response_.datafunc(args)) return AbortOnDataFunctionFailure();
  } else {
    response_.body.append(buffer, length);
  }

  return realsize;
}

size_t CurlWrite(char* buffer, size_t size, size_t length, void* userdata) {
  return static_cast<CurlTransferState*>(userdata)->Write(buffer, size, length);
}

struct UploadState {
  const char* data;
  size_t size;
  size_t offset;
};

size_t CurlRead(char* buffer, size_t size, size_t length, void* userdata) {
  auto* upload = static_cast<UploadState*>(userdata);
  size_t room = size * length;
  size_t left = upload->size - upload->offset;
  size_t count = left < room ? left : room;
  if (count != 0) {
    std::memcpy(buffer, upload->data + upload->offset, count);
    upload->offset += count;
  }
  return count;
}

// libcurl retries a request whose reused connection died before any response
// byte arrived, but only if it can rewind the request body; without this the
// retry fails with CURLE_SEND_FAIL_REWIND. The body is a memory range, so
// rewinding is just the offset.
int CurlSeek(void* userdata, curl_off_t offset, int origin) {
  auto* upload = static_cast<UploadState*>(userdata);
  if (origin != SEEK_SET || offset < 0 ||
      static_cast<size_t>(offset) > upload->size) {
    return CURL_SEEKFUNC_CANTSEEK;
  }
  upload->offset = static_cast<size_t>(offset);
  return CURL_SEEKFUNC_OK;
}

int CurlProgress(void* userdata, curl_off_t dltotal, curl_off_t dlnow,
                 curl_off_t ultotal, curl_off_t ulnow) {
  auto* req = static_cast<Request*>(userdata);
  ProgressFunctionArgs args;
  args.download_total_bytes = static_cast<double>(dltotal);
  args.downloaded_bytes = static_cast<double>(dlnow);
  args.upload_total_bytes = static_cast<double>(ultotal);
  args.uploaded_bytes = static_cast<double>(ulnow);
  args.userdata = req->progress_userdata;
  if (req->progressfunc(args)) return CURL_PROGRESSFUNC_CONTINUE;
  return 1;
}

}  // namespace

// The connection pool is the multi handle's connection cache: every transfer
// this client runs adds its easy handle to the same CURLM, so an idle
// connection established for one request is reused by whichever request needs
// it next - across threads, which is the reason the cache has to live in the
// multi. Sharing a connection cache through CURLSH is not an alternative:
// libcurl documents that as unsupported between concurrent threads (an HTTP/2
// connection carries several logical streams, so the cache is not the only
// shared state), and it crashed this project before.
//
// All curl_multi_* calls are serialized by mutex_; a request that finds no
// completion yet sleeps in curl_multi_poll() and another thread's
// curl_multi_wakeup() cuts that short when it adds a transfer.
struct Client::Impl {
  Impl() : multi_(curl_multi_init()) {
    EnsureGlobalCurlInit();
    if (multi_ == nullptr) {
      std::cerr << "curl_multi_init failed" << std::endl;
      std::terminate();
    }
  }

  ~Impl() { curl_multi_cleanup(multi_); }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  std::unique_ptr<EasyHandle> Acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (idle_.empty()) return std::make_unique<EasyHandle>();
    std::unique_ptr<EasyHandle> easy = std::move(idle_.back());
    idle_.pop_back();
    easy->Reset();
    return easy;
  }

  void Release(std::unique_ptr<EasyHandle> easy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (idle_.size() >= kMaxIdleHandles) return;  // cleaned up by the caller
    idle_.push_back(std::move(easy));
  }

  Response Perform(Request& req);

  std::mutex mutex_;
  CURLM* multi_;
  std::vector<std::unique_ptr<EasyHandle>> idle_;
  // Completion results handed out by curl_multi_info_read(), keyed by handle
  // so that the thread owning a transfer only picks up its own result.
  std::map<CURL*, CURLcode> finished_;
};

Response Client::Impl::Perform(Request& req) {
  std::unique_ptr<EasyHandle> lease = Acquire();
  struct Releaser {
    Impl* impl;
    std::unique_ptr<EasyHandle>* lease;
    ~Releaser() { impl->Release(std::move(*lease)); }
  } releaser{this, &lease};

  CURL* const handle = lease->get();

  // Connections belong to the multi handle, so keep the kernel from dropping
  // the pooled sockets across idle gaps between S3 calls.
  curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
  // Timeouts and DNS must not go through signals: requests run on caller
  // threads, including the *Async ones.
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

  // Fail a stalled transfer instead of hanging forever. Skipped when the
  // caller set an explicit total timeout (RDMA control plane) - that already
  // bounds it.
  if (req.timeout_secs <= 0) {
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, kStallTimeoutSecs);
  }

  // Request settings. The strings below must outlive the transfer.
  std::string method = MethodToString(req.method);
  std::string urlstring = req.url.String();
  curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(handle, CURLOPT_URL, urlstring.c_str());
  if (req.debug) curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
  if (req.ignore_cert_check) {
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if (req.url.https) {
    if (!req.ssl_cert_file.empty()) {
      curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(handle, CURLOPT_CAINFO, req.ssl_cert_file.c_str());
    }
    if (!req.key_file.empty()) {
      curl_easy_setopt(handle, CURLOPT_SSLKEY, req.key_file.c_str());
    }
    if (!req.cert_file.empty()) {
      curl_easy_setopt(handle, CURLOPT_SSLCERT, req.cert_file.c_str());
    }
  }

  if (!req.nic_interface.empty()) {
    curl_easy_setopt(handle, CURLOPT_INTERFACE, req.nic_interface.c_str());
  }
  if (req.connect_timeout_secs > 0) {
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, req.connect_timeout_secs);
  }
  if (req.timeout_secs > 0) {
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, req.timeout_secs);
  }

  UploadState upload{req.body.data(), req.body.size(), 0};

  switch (req.method) {
    case Method::kDelete:
    case Method::kGet:
      break;
    case Method::kHead:
      curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
      break;
    case Method::kPut:
    case Method::kPost:
      if (!req.headers.Contains("Content-Length")) {
        req.headers.Add("Content-Length", std::to_string(req.body.size()));
      }
      curl_easy_setopt(handle, CURLOPT_READFUNCTION, CurlRead);
      curl_easy_setopt(handle, CURLOPT_READDATA, &upload);
      curl_easy_setopt(handle, CURLOPT_SEEKFUNCTION, CurlSeek);
      curl_easy_setopt(handle, CURLOPT_SEEKDATA, &upload);
      // CURLOPT_INFILESIZE_LARGE (curl_off_t), not CURLOPT_INFILESIZE
      // (long): the latter is documented to be capped at 2 GiB and silently
      // truncates the upload for larger single-request bodies (e.g. a
      // >4 GiB buffer that could not be RDMA-registered and falls back to a
      // single PUT).
      curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE,
                       static_cast<curl_off_t>(req.body.size()));
      curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
      break;
  }

  HeaderList headerlist;
  for (const std::string& header : req.headers.ToHttpHeaders()) {
    headerlist.Append(header);
  }
  headerlist.Append("Expect:");  // Disable 100 continue from server.
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headerlist.get());

  // Response settings.
  curl_easy_setopt(handle, CURLOPT_HEADER, 1L);

  Response response;
  response.datafunc = req.datafunc;
  response.userdata = req.userdata;

  CurlTransferState state(response);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, CurlWrite);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);

  if (req.progressfunc != nullptr) {
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, CurlProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &req);
  }

  char errbuf[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (curl_multi_add_handle(multi_, handle) != CURLM_OK) {
      response.error = "failed to add the transfer to the connection pool";
      return response;
    }
  }
  // Whoever is sleeping in curl_multi_poll() has to pick this transfer up.
  curl_multi_wakeup(multi_);

  CURLcode code = CURLE_OK;
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      int running = 0;
      CURLMcode mres = curl_multi_perform(multi_, &running);
      if (mres != CURLM_OK) {
        response.error = curl_multi_strerror(mres);
        break;
      }

      CURLMsg* msg = nullptr;
      int queued = 0;
      while ((msg = curl_multi_info_read(multi_, &queued)) != nullptr) {
        if (msg->msg == CURLMSG_DONE) {
          finished_[msg->easy_handle] = msg->data.result;
        }
      }

      auto it = finished_.find(handle);
      if (it != finished_.end()) {
        code = it->second;
        finished_.erase(it);
        break;
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      curl_multi_poll(multi_, nullptr, 0, kPollTimeoutMs, nullptr);
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    curl_multi_remove_handle(multi_, handle);
  }

  // If the transfer aborted before a single byte was delivered (e.g. the
  // low-speed limit or connect timeout fired on a dropped connection), the
  // write callback never ran and set neither status_code nor error. Surface
  // the libcurl diagnostic instead of a silently-empty failure.
  if (response.error.empty() && code != CURLE_OK && !state.aborted()) {
    response.error = errbuf[0] != '\0' ? errbuf : curl_easy_strerror(code);
  }
  if (response.error.empty() && response.status_code == 0) {
    response.error =
        "transfer ended without a response (connection dropped, timed out, "
        "or was aborted before any data was received)";
  }

  if (req.progressfunc != nullptr) {
    ProgressFunctionArgs args;
    args.userdata = req.progress_userdata;
    curl_off_t upload_speed = 0;
    curl_off_t download_speed = 0;
    curl_easy_getinfo(handle, CURLINFO_SPEED_UPLOAD_T, &upload_speed);
    curl_easy_getinfo(handle, CURLINFO_SPEED_DOWNLOAD_T, &download_speed);
    args.upload_speed = static_cast<double>(upload_speed);
    args.download_speed = static_cast<double>(download_speed);
    req.progressfunc(args);
  }

  return response;
}

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() = default;

Client::Client(Client&&) noexcept = default;

Client& Client::operator=(Client&&) noexcept = default;

Response Client::Execute(Request& request) { return impl_->Perform(request); }

}  // namespace minio::http
