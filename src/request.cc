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

#include "request.h"

#define EMPTY_SHA256 \
  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

minio::s3::BaseUrl::BaseUrl(std::string host, bool https) {
  http::Url url = http::Url::Parse(host);
  if (!url.path.empty() || !url.query_string.empty()) {
    this->err_ = error::Error(
        "host value must contain only hostname and optional port number");
    return;
  }

  this->https = https;
  this->host = url.host;
  this->port = url.port;

  this->accelerate_host = utils::StartsWith(url.host, "s3-accelerate.");
  this->aws_host =
      ((utils::StartsWith(url.host, "s3.") || this->accelerate_host) &&
       (utils::EndsWith(url.host, ".amazonaws.com") ||
        utils::EndsWith(url.host, ".amazonaws.com.cn")));
  this->virtual_style =
      this->aws_host || utils::EndsWith(url.host, "aliyuncs.com");

  if (this->aws_host) {
    std::string aws_domain = "amazonaws.com";
    this->region = extractRegion(url.host);

    bool is_aws_china_host = utils::EndsWith(url.host, ".cn");
    if (is_aws_china_host) {
      aws_domain += ".cn";
      if (this->region.empty()) {
        this->err_ = error::Error(
            "region must be provided in Amazon S3 China endpoint " + url.host);
        return;
      }
    }

    this->dualstack_host = utils::Contains(url.host, ".dualstack.");

    this->host = aws_domain;
  } else {
    this->accelerate_host = false;
  }
}

minio::error::Error minio::s3::BaseUrl::BuildUrl(http::Url& url,
                                                 http::Method method,
                                                 std::string region,
                                                 utils::Multimap query_params,
                                                 std::string bucket_name,
                                                 std::string object_name) {
  if (err_) return err_;

  if (bucket_name.empty() && !object_name.empty()) {
    return error::Error("empty bucket name for object name " + object_name);
  }

  std::string hostvalue = host;

  if (bucket_name.empty()) {
    if (aws_host) hostvalue = "s3." + region + "." + hostvalue;
    url = http::Url{https, hostvalue, port, "/"};
    return error::SUCCESS;
  }

  bool enforce_path_style = (
      // CreateBucket API requires path style in Amazon AWS S3.
      (method == http::Method::kPut && object_name.empty() && !query_params) ||

      // GetBucketLocation API requires path style in Amazon AWS S3.
      query_params.Contains("location") ||

      // Use path style for bucket name containing '.' which causes
      // SSL certificate validation error.
      (utils::Contains(bucket_name, '.') && https));

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
    hostvalue = s3_domain + hostvalue;
  }

  std::string path;
  if (enforce_path_style || !virtual_style) {
    path = "/" + bucket_name;
  } else {
    hostvalue = bucket_name + "." + hostvalue;
  }

  if (!object_name.empty()) {
    if (object_name.front() != '/') path += '/';
    path += utils::EncodePath(object_name);
  }

  url = http::Url{https, hostvalue, port, path, query_params.ToQueryString()};

  return error::SUCCESS;
}

minio::s3::Request::Request(http::Method method, std::string region,
                            BaseUrl& baseurl, utils::Multimap extra_headers,
                            utils::Multimap extra_query_params)
    : base_url(baseurl) {
  this->method = method;
  this->region = region;
  this->headers = extra_headers;
  this->query_params = extra_query_params;
}

void minio::s3::Request::BuildHeaders(http::Url& url,
                                      creds::Provider* provider) {
  headers.Add("Host", url.host);
  headers.Add("User-Agent", user_agent);

  bool md5sum_added = headers.Contains("Content-MD5");
  std::string md5sum;

  switch (method) {
    case http::Method::kPut:
    case http::Method::kPost:
      headers.Add("Content-Length", std::to_string(body.size()));
      if (!headers.Contains("Content-Type")) {
        headers.Add("Content-Type", "application/octet-stream");
      }
      if (provider != NULL) {
        sha256 = utils::Sha256Hash(body);
      } else if (!md5sum_added) {
        md5sum = utils::Md5sumHash(body);
      }
      break;
    default:
      if (provider != NULL) sha256 = EMPTY_SHA256;
  }

  if (!md5sum.empty()) headers.Add("Content-MD5", md5sum);
  if (!sha256.empty()) headers.Add("x-amz-content-sha256", sha256);

  date = utils::Time::Now();
  headers.Add("x-amz-date", date.ToAmzDate());

  if (provider != NULL) {
    creds::Credentials creds = provider->Fetch();
    if (!creds.session_token.empty()) {
      headers.Add("X-Amz-Security-Token", creds.session_token);
    }

    signer::SignV4S3(method, url.path, region, headers, query_params,
                     creds.access_key, creds.secret_key, sha256, date);
  }
}

minio::http::Request minio::s3::Request::ToHttpRequest(
    creds::Provider* provider) {
  http::Url url;
  if (error::Error err = base_url.BuildUrl(url, method, region, query_params,
                                           bucket_name, object_name)) {
    std::cerr << "failed to build url. error=" << err
              << ". This should not happen" << std::endl;
    std::terminate();
  }
  BuildHeaders(url, provider);

  http::Request request(method, url);
  request.body = body;
  request.headers = headers;
  request.datafunc = datafunc;
  request.userdata = userdata;
  request.debug = debug;
  request.ignore_cert_check = ignore_cert_check;
  request.ssl_cert_file = ssl_cert_file;

  return request;
}
