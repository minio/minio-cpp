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

std::string minio::http::ExtractRegion(std::string host) {
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

minio::error::Error minio::http::BaseUrl::SetHost(std::string hostvalue) {
  struct sockaddr_in dst;
  if (inet_pton(AF_INET6, hostvalue.c_str(), &(dst.sin_addr)) != 0) {
    hostvalue = "[" + hostvalue + "]";
  } else if (hostvalue.front() != '[' || hostvalue.back() != ']') {
    std::stringstream str_stream(hostvalue);
    std::string portstr;
    while (std::getline(str_stream, portstr, ':')) {
    }
    try {
      port = std::stoi(portstr);
      hostvalue = hostvalue.substr(0, hostvalue.rfind(":" + portstr));
    } catch (std::invalid_argument) {
      port = 0;
    }
  }

  accelerate_host = utils::StartsWith(hostvalue, "s3-accelerate.");
  aws_host = ((utils::StartsWith(hostvalue, "s3.") || accelerate_host) &&
              (utils::EndsWith(hostvalue, ".amazonaws.com") ||
               utils::EndsWith(hostvalue, ".amazonaws.com.cn")));
  virtual_style = aws_host || utils::EndsWith(hostvalue, "aliyuncs.com");

  if (aws_host) {
    std::string awshost;
    bool is_aws_china_host = utils::EndsWith(hostvalue, ".cn");
    awshost = "amazonaws.com";
    if (is_aws_china_host) awshost = "amazonaws.com.cn";
    region = ExtractRegion(hostvalue);

    if (is_aws_china_host && region.empty()) {
      return error::Error("region missing in Amazon S3 China endpoint " +
                          hostvalue);
    }

    dualstack_host = utils::Contains(hostvalue, ".dualstack.");
    hostvalue = awshost;
  } else {
    accelerate_host = false;
  }

  host = hostvalue;

  return error::SUCCESS;
}

std::string minio::http::BaseUrl::GetHostHeaderValue() {
  // ignore port when port and service match i.e HTTP -> 80, HTTPS -> 443
  if (port == 0 || (!is_https && port == 80) || (is_https && port == 443)) {
    return host;
  }
  return host + ":" + std::to_string(port);
}

minio::error::Error minio::http::BaseUrl::BuildUrl(utils::Url &url,
                                                   Method method,
                                                   std::string region,
                                                   utils::Multimap query_params,
                                                   std::string bucket_name,
                                                   std::string object_name) {
  if (bucket_name.empty() && !object_name.empty()) {
    return error::Error("empty bucket name for object name " + object_name);
  }

  std::string host = GetHostHeaderValue();

  if (bucket_name.empty()) {
    if (aws_host) host = "s3." + region + "." + host;
    url = utils::Url{is_https, host, "/"};
    return error::SUCCESS;
  }

  bool enforce_path_style = (
      // CreateBucket API requires path style in Amazon AWS S3.
      (method == Method::kPut && object_name.empty() && !query_params) ||

      // GetBucketLocation API requires path style in Amazon AWS S3.
      query_params.Contains("location") ||

      // Use path style for bucket name containing '.' which causes
      // SSL certificate validation error.
      (utils::Contains(bucket_name, '.') && is_https));

  if (aws_host) {
    std::string s3_domain = "s3.";
    if (accelerate_host) {
      if (utils::Contains(bucket_name, '.')) {
        return error::Error(
            "bucket name '" + bucket_name +
            "' with '.' is not allowed for accelerate endpoint");
      }

      if (!enforce_path_style) s3_domain = "s3-accelerate.";
    }

    if (dualstack_host) s3_domain += "dualstack.";
    if (enforce_path_style || !accelerate_host) {
      s3_domain += region + ".";
    }
    host = s3_domain + host;
  }

  std::string path;
  if (enforce_path_style || !virtual_style) {
    path = "/" + bucket_name;
  } else {
    host = bucket_name + "." + host;
  }

  if (!object_name.empty()) {
    if (*(object_name.begin()) != '/') path += "/";
    path += utils::EncodePath(object_name);
  }

  url = utils::Url{is_https, host, path, query_params.ToQueryString()};

  return error::SUCCESS;
}

size_t minio::http::Response::ReadStatusCode(char *buffer, size_t size,
                                             size_t length) {
  size_t real_size = size * length;

  response_ += std::string(buffer, length);

  size_t pos = response_.find("\r\n");
  if (pos == std::string::npos) return real_size;

  std::string line = response_.substr(0, pos);
  response_ = response_.substr(pos + 2);

  if (continue100_) {
    if (!line.empty()) {
      error = "invalid HTTP response";
      return real_size;
    }

    continue100_ = false;

    pos = response_.find("\r\n");
    if (pos == std::string::npos) return real_size;

    line = response_.substr(0, pos);
    response_ = response_.substr(pos + 2);
  }

  // Skip HTTP/1.x.
  pos = line.find(" ");
  if (pos == std::string::npos) {
    error = "invalid HTTP response";
    return real_size;
  }
  line = line.substr(pos + 1);

  // Read status code.
  pos = line.find(" ");
  if (pos == std::string::npos) {
    error = "invalid HTTP response";
    return real_size;
  }
  std::string code = line.substr(0, pos);
  std::string::size_type st;
  status_code = std::stoi(code, &st);
  if (st == std::string::npos) error = "invalid HTTP response code";

  if (status_code == 100) {
    continue100_ = true;
  } else {
    status_code_read_ = true;
  }

  return real_size;
}

size_t minio::http::Response::ReadHeaders(curlpp::Easy *handle, char *buffer,
                                          size_t size, size_t length) {
  size_t real_size = size * length;

  response_ += std::string(buffer, length);
  size_t pos = response_.find("\r\n\r\n");
  if (pos == std::string::npos) return real_size;

  headers_read_ = true;

  std::string lines = response_.substr(0, pos);
  body = response_.substr(pos + 4);

  while ((pos = lines.find("\r\n")) != std::string::npos) {
    std::string line = lines.substr(0, pos);
    lines.erase(0, pos + 2);

    if ((pos = line.find(": ")) == std::string::npos) {
      error = "invalid HTTP header: " + line;
      return real_size;
    }

    headers.Add(line.substr(0, pos), line.substr(pos + 2));
  }

  if (!lines.empty()) {
    if ((pos = lines.find(": ")) == std::string::npos) {
      error = "invalid HTTP header: " + lines;
      return real_size;
    }

    headers.Add(lines.substr(0, pos), lines.substr(pos + 2));
  }

  if (body.size() == 0 || data_callback == NULL || status_code < 200 ||
      status_code > 299)
    return real_size;

  DataCallbackArgs args = {handle, this, body.data(), 1, body.size(), user_arg};
  size_t written = data_callback(args);
  if (written == body.size()) written = real_size;
  body = "";
  return written;
}

size_t minio::http::Response::ResponseCallback(curlpp::Easy *handle,
                                               char *buffer, size_t size,
                                               size_t length) {
  size_t real_size = size * length;

  // As error occurred previously, just drain the connection.
  if (!error.empty()) return real_size;

  if (!status_code_read_) return ReadStatusCode(buffer, size, length);

  if (!headers_read_) return ReadHeaders(handle, buffer, size, length);

  // Received unsuccessful HTTP response code.
  if (data_callback == NULL || status_code < 200 || status_code > 299) {
    body += std::string(buffer, length);
    return real_size;
  }

  return data_callback(
      DataCallbackArgs{handle, this, buffer, size, length, user_arg});
}

minio::http::Request::Request(Method httpmethod, utils::Url httpurl) {
  method = httpmethod;
  url = httpurl;
}

minio::http::Response minio::http::Request::execute() {
  curlpp::Cleanup cleaner;
  curlpp::Easy request;

  // Request settings.
  request.setOpt(
      new curlpp::options::CustomRequest{http::MethodToString(method)});
  std::string urlstring = url.String();
  request.setOpt(new curlpp::Options::Url(urlstring));
  if (debug) request.setOpt(new curlpp::Options::Verbose(true));
  if (ignore_cert_check) {
    request.setOpt(new curlpp::Options::SslVerifyPeer(false));
  }

  utils::CharBuffer charbuf((char *)body.data(), body.size());
  std::istream body_stream(&charbuf);

  switch (method) {
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
  response.data_callback = data_callback;
  response.user_arg = user_arg;

  using namespace std::placeholders;
  request.setOpt(new curlpp::options::WriteFunction(
      std::bind(&Response::ResponseCallback, &response, &request, _1, _2, _3)));

  // Execute.
  request.perform();

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
