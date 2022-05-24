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

#include "client.h"

minio::s3::Request::Request(http::Method httpmethod, std::string regionvalue,
                            http::BaseUrl& baseurl)
    : method(httpmethod), region(regionvalue), base_url(baseurl) {}

void minio::s3::Request::BuildHeaders(utils::Url& url,
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
  }

  // MD5 hash of zero length byte array.
  // public static final String ZERO_MD5_HASH = "1B2M2Y8AsgTpgAmY7PhCfg==";

  if (provider != NULL) {
    if (url.is_https) {
      sha256 = "UNSIGNED-PAYLOAD";
      switch (method) {
        case http::Method::kPut:
        case http::Method::kPost:
          if (!md5sum_added) {
            md5sum = utils::Md5sumHash(body);
          }
      }
    } else {
      switch (method) {
        case http::Method::kPut:
        case http::Method::kPost:
          sha256 = utils::Sha256Hash(body);
          break;
        default:
          sha256 =
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85"
              "5";
      }
    }
  } else {
    switch (method) {
      case http::Method::kPut:
      case http::Method::kPost:
        if (!md5sum_added) {
          md5sum = utils::Md5sumHash(body);
        }
    }
  }

  if (!md5sum.empty()) headers.Add("Content-MD5", md5sum);
  if (!sha256.empty()) headers.Add("x-amz-content-sha256", sha256);

  date = utils::Time::Now();
  headers.Add("x-amz-date", date.ToAmzDate());

  if (provider != NULL) {
    creds::Credentials creds = provider->Fetch();
    if (!creds.SessionToken().empty()) {
      headers.Add("X-Amz-Security-Token", creds.SessionToken());
    }

    std::string access_key = creds.AccessKey();
    std::string secret_key = creds.SecretKey();
    signer::SignV4S3(method, url.path, region, headers, query_params,
                     access_key, secret_key, sha256, date);
  }
}

minio::http::Request minio::s3::Request::ToHttpRequest(
    creds::Provider* provider) {
  utils::Url url;
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
  request.data_callback = data_callback;
  request.user_arg = user_arg;
  request.debug = debug;
  request.ignore_cert_check = ignore_cert_check;

  return request;
}
