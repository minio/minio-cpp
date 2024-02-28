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

#ifndef _MINIO_REQUEST_H
#define _MINIO_REQUEST_H

#include "credentials.h"
#include "providers.h"
#include "signer.h"

namespace minio {
namespace s3 {
const std::string AWS_S3_PREFIX =
    "^(((bucket\\.|accesspoint\\.)"
    "vpce(-(?!_)[a-z_\\d]+)+\\.s3\\.)|"
    "((?!s3)(?!-)(?!_)[a-z_\\d-]{1,63}\\.)"
    "s3-control(-(?!_)[a-z_\\d]+)*\\.|"
    "(s3(-(?!_)[a-z_\\d]+)*\\.))";
const std::regex HOSTNAME_REGEX(
    "^((?!-)(?!_)[a-z_\\d-]{1,63}\\.)*"
    "((?!_)(?!-)[a-z_\\d-]{1,63})$",
    std::regex_constants::icase);
const std::regex AWS_ENDPOINT_REGEX(".*\\.amazonaws\\.com(|\\.cn)$",
                                    std::regex_constants::icase);
const std::regex AWS_S3_ENDPOINT_REGEX(
    AWS_S3_PREFIX + "((?!s3)(?!-)(?!_)[a-z_\\d-]{1,63}\\.)*" +
        "amazonaws\\.com(|\\.cn)$",
    std::regex_constants::icase);
const std::regex AWS_ELB_ENDPOINT_REGEX(
    "^(?!-)(?!_)[a-z_\\d-]{1,63}\\."
    "(?!-)(?!_)[a-z_\\d-]{1,63}\\."
    "elb\\.amazonaws\\.com$",
    std::regex_constants::icase);
const std::regex AWS_S3_PREFIX_REGEX(AWS_S3_PREFIX,
                                     std::regex_constants::icase);
const std::regex REGION_REGEX("^((?!_)(?!-)[a-z_\\d-]{1,63})$",
                              std::regex_constants::icase);

bool awsRegexMatch(std::string_view value, const std::regex& regex);

error::Error getAwsInfo(const std::string& host, bool https, std::string& region,
                        std::string& aws_s3_prefix,
                        std::string& aws_domain_suffix, bool& dualstack);

std::string extractRegion(const std::string& host);

struct BaseUrl {
  bool https = true;
  std::string host;
  unsigned int port = 0;
  std::string region;
  std::string aws_s3_prefix;
  std::string aws_domain_suffix;
  bool dualstack = false;
  bool virtual_style = false;

  BaseUrl() = default;
  BaseUrl(std::string host, bool https = true, std::string region = ""); // PWTODO: make it explicit
  ~BaseUrl() = default;

  error::Error BuildUrl(http::Url& url, http::Method method, const std::string& region,
                        const utils::Multimap& query_params,
                        const std::string& bucket_name,
                        const std::string& object_name);

  explicit operator bool() const { return !err_ && !host.empty(); }

  error::Error Error() const {
    if (host.empty() && !err_) return error::Error("empty host");
    return err_;
  }

 private:
  error::Error err_;

  error::Error BuildAwsUrl(http::Url& url, const std::string& bucket_name,
                           bool enforce_path_style, const std::string& region);
  void BuildListBucketsUrl(http::Url& url, const std::string& region);
};  // struct Url

struct Request {
  http::Method method;
  std::string region;
  BaseUrl& base_url;

  std::string user_agent;

  utils::Multimap headers;
  utils::Multimap query_params;

  std::string bucket_name;
  std::string object_name;

  std::string_view body = "";

  http::DataFunction datafunc = nullptr;
  void* userdata = nullptr;

  http::ProgressFunction progressfunc = nullptr;
  void* progress_userdata = nullptr;

  std::string sha256;
  utils::Time date;

  bool debug = false;
  bool ignore_cert_check = false;
  std::string ssl_cert_file;

  Request(http::Method method, std::string region, BaseUrl& baseurl,
          utils::Multimap extra_headers, utils::Multimap extra_query_params);

  ~Request() = default;

  http::Request ToHttpRequest(creds::Provider* const provider = nullptr);

 private:
  void BuildHeaders(http::Url& url, creds::Provider* provider);
};  // struct Request
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_REQUEST_H
