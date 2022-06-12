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

#ifndef _MINIO_CREDS_CREDENTIALS_H
#define _MINIO_CREDS_CREDENTIALS_H

#include <pugixml.hpp>

#include "utils.h"

namespace minio {
namespace creds {
static bool expired(utils::Time expiration) {
  if (!expiration) return false;
  utils::Time now = utils::Time::Now();
  now.Add(10);
  return expiration < now;
}

/**
 * Credentials contains access key and secret key with optional session token
 * and expiration.
 */
struct Credentials {
  error::Error err;
  std::string access_key;
  std::string secret_key;
  std::string session_token;
  utils::Time expiration;

  bool IsExpired() { return expired(expiration); }

  operator bool() const {
    return !err && !access_key.empty() && expired(expiration);
  }

  static Credentials ParseXML(std::string_view data, std::string root) {
    pugi::xml_document xdoc;
    pugi::xml_parse_result result = xdoc.load_string(data.data());
    if (!result) return Credentials{error::Error("unable to parse XML")};

    auto credentials = xdoc.select_node((root + "/Credentials").c_str());

    auto text = credentials.node().select_node("AccessKeyId/text()");
    std::string access_key = text.node().value();

    text = credentials.node().select_node("SecretAccessKey/text()");
    std::string secret_key = text.node().value();

    text = credentials.node().select_node("SessionToken/text()");
    std::string session_token = text.node().value();

    text = credentials.node().select_node("Expiration/text()");
    auto expiration = utils::Time::FromISO8601UTC(text.node().value());

    return Credentials{error::SUCCESS, access_key, secret_key, session_token,
                       expiration};
  }
};  // class Credentials
}  // namespace creds
}  // namespace minio

#endif  // #ifndef _MINIO_CREDS_CREDENTIALS_H
