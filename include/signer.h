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

#ifndef _MINIO_SIGNER_H
#define _MINIO_SIGNER_H

#include <openssl/hmac.h>

#include "http.h"

namespace minio {
namespace signer {
std::string GetScope(utils::Time& time, std::string_view region,
                     std::string_view service_name);
std::string GetCanonicalRequestHash(std::string_view method,
                                    std::string_view uri,
                                    std::string_view query_string,
                                    std::string_view headers,
                                    std::string_view signed_headers,
                                    std::string_view content_sha256);
std::string GetStringToSign(utils::Time& date, std::string_view scope,
                            std::string_view canonical_request_hash);
std::string HmacHash(std::string_view key, std::string_view data);
std::string GetSigningKey(std::string_view secret_key, utils::Time& date,
                          std::string_view region,
                          std::string_view service_name);
std::string GetSignature(std::string_view signing_key,
                         std::string_view string_to_sign);
std::string GetAuthorization(std::string_view access_key,
                             std::string_view scope,
                             std::string_view signed_headers,
                             std::string_view signature);
utils::Multimap& SignV4(std::string_view service_name, http::Method& method,
                        std::string_view uri, std::string_view region,
                        utils::Multimap& headers, utils::Multimap& query_params,
                        std::string_view access_key,
                        std::string_view secret_key,
                        std::string_view content_sha256, utils::Time& date);
utils::Multimap& SignV4S3(http::Method& method, std::string_view uri,
                          std::string_view region, utils::Multimap& headers,
                          utils::Multimap& query_params,
                          std::string_view access_key,
                          std::string_view secret_key,
                          std::string_view content_sha256, utils::Time& date);
utils::Multimap& SignV4STS(http::Method& method, std::string_view uri,
                           std::string_view region, utils::Multimap& headers,
                           utils::Multimap& query_params,
                           std::string_view access_key,
                           std::string_view secret_key,
                           std::string_view content_sha256, utils::Time& date);
}  // namespace signer
}  // namespace minio
#endif  // #ifndef __MINIO_SIGNER_H
