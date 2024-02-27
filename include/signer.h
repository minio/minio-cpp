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
std::string GetScope(const utils::Time& time, const std::string& region,
                     const std::string& service_name);
std::string GetCanonicalRequestHash(const std::string& method, const std::string& uri,
                                    const std::string& query_string,
                                    const std::string& headers,
                                    const std::string& signed_headers,
                                    const std::string& content_sha256);
std::string GetStringToSign(const utils::Time& date, const std::string& scope,
                            const std::string& canonical_request_hash);
std::string HmacHash(std::string_view key, std::string_view data);
std::string GetSigningKey(const std::string& secret_key, const utils::Time& date,
                          std::string_view region,
                          std::string_view service_name);
std::string GetSignature(std::string_view signing_key,
                         std::string_view string_to_sign);
std::string GetAuthorization(const std::string& access_key, const std::string& scope,
                             const std::string& signed_headers,
                             const std::string& signature);
utils::Multimap SignV4(const std::string& service_name, const http::Method& method,
                        const std::string& uri, const std::string& region,
                        utils::Multimap& headers, utils::Multimap query_params,
                        const std::string& access_key, const std::string& secret_key,
                        const std::string& content_sha256, const utils::Time& date);
utils::Multimap SignV4S3(const http::Method& method, const std::string& uri,
                          const std::string& region, utils::Multimap& headers,
                          utils::Multimap query_params, const std::string& access_key,
                          const std::string& secret_key, const std::string& content_sha256,
                          const utils::Time& date);
utils::Multimap SignV4STS(const http::Method& method, const std::string& uri,
                           const std::string& region, utils::Multimap& headers,
                           utils::Multimap query_params,
                           const std::string& access_key, const std::string& secret_key,
                           const std::string& content_sha256, const utils::Time& date);
utils::Multimap PresignV4(const http::Method& method, const std::string& host,
                           const std::string& uri, const std::string& region,
                           utils::Multimap query_params,
                           const std::string& access_key, const std::string& secret_key,
                           const utils::Time& date, unsigned int expires);
std::string PostPresignV4(const std::string& data, const std::string& secret_key,
                          const utils::Time& date, const std::string& region);
}  // namespace signer
}  // namespace minio
#endif  // #ifndef __MINIO_SIGNER_H
