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

#include "http.h"

#include <system_error>
#include <thread>
#include <chrono>

minio::error::Error minio::http::Response::ReadStatusCode() {
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

minio::error::Error minio::http::Response::ReadHeaders() {
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

size_t minio::http::Response::ResponseCallback(curlpp::Multi *requests,
                                               curlpp::Easy *request,
                                               char *buffer, size_t size,
                                               size_t length) {
  size_t realsize = size * length;

  // If error occurred previously, just cancel the request.
  if (!error.empty()) {
    requests->remove(request);
    return realsize;
  }

  if (!status_code_read_ || !headers_read_) {
    response_ += std::string(buffer, length);
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
    if (datafunc != NULL && status_code >= 200 && status_code <= 299) {
      DataFunctionArgs args{request, this, response_, userdata};
      if (!datafunc(args)) requests->remove(request);
    } else {
      body = response_;
    }

    return realsize;
  }

  // If data function is set and the request is successful, send data.
  if (datafunc != NULL && status_code >= 200 && status_code <= 299) {
    DataFunctionArgs args{request, this, std::string(buffer, length), userdata};
    if (!datafunc(args)) requests->remove(request);
  } else {
    body += std::string(buffer, length);
  }

  return realsize;
}

minio::http::Request::Request(Method method, Url url) {
  this->method = method;
  this->url = url;
  std::string ssl_cert_file;
  if (url.https && utils::GetEnv(ssl_cert_file, "SSL_CERT_FILE")) {
    this->ssl_cert_file = ssl_cert_file;
  }
}

minio::http::Response minio::http::Request::execute() {
  curlpp::Cleanup cleaner;
  curlpp::Easy request;
  curlpp::Multi requests;

  // Request settings.
  request.setOpt(
      new curlpp::options::CustomRequest{http::MethodToString(method)});
  std::string urlstring = url.String();
  request.setOpt(new curlpp::Options::Url(urlstring));
  if (debug) request.setOpt(new curlpp::Options::Verbose(true));
  if (ignore_cert_check) {
    request.setOpt(new curlpp::Options::SslVerifyPeer(false));
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

  utils::CharBuffer charbuf((char *)body.data(), body.size());
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
      request.setOpt(new curlpp::Options::InfileSize(body.size()));
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

  int left = 0;
  requests.add(&request);

  // Execute.
  while (!requests.perform(&left)) {
  }
  while (left) {
    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcep;
    int maxfd = 0;
    int rc; /* select() return code */
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);

    requests.fdset(&fdread, &fdwrite, &fdexcep, &maxfd);

    // 2022-9-22 16:49:35, fix error use timeout NULL arg for select, then cause current thread always waiting, if 'maxfd' equal -1.
    // see https://github.com/curl/curl/blob/5ccddf64398c1186deb5769dac086d738e150e09/docs/examples/multi-legacy.c
    if (maxfd == -1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    else
    {
        /* Note that on some platforms 'timeout' may be modified by select().
          If you need access to the original value save a copy beforehand. */
        rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
    }

    switch(rc) {
    case -1:
      /* select error */
        throw std::system_error(std::error_code(rc, std::generic_category()),
                                "unexpected error code -1 while invoking select()");
    case 0: /* timeout */
    default: /* action */
          while (!requests.perform(&left)) {}
    }
  }

  return response;
}

minio::http::Response minio::http::Request::Execute() {
  try {
    return execute();
  } catch (curlpp::LogicError &e) {
    Response response;
    response.error = std::string("curlpp::LogicError: ") + e.what();
    return response;
  } catch (curlpp::RuntimeError &e) {
    Response response;
    response.error = std::string("curlpp::RuntimeError: ") + e.what();
    return response;
  }
}
