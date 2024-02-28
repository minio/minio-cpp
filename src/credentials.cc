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

#include <pugixml.hpp>
#include "credentials.h"

bool minio::creds::expired(const utils::Time& expiration) {
  if (!expiration) return false;
  utils::Time now = utils::Time::Now();
  now.Add(10);
  return expiration < now;
}

minio::creds::Credentials minio::creds::Credentials::ParseXML(std::string_view data, const std::string& root) {
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
