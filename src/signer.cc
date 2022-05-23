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

#include "signer.h"

const char* SIGN_V4_ALGORITHM = "AWS4-HMAC-SHA256";
const std::regex MULTI_SPACE_REGEX("( +)");

std::string minio::signer::GetScope(utils::Time& time, std::string_view region,
                                    std::string_view service_name) {
  return time.ToSignerDate() + "/" + std::string(region) + "/" +
         std::string(service_name) + "/aws4_request";
}

std::string minio::signer::GetCanonicalRequestHash(
    std::string_view method, std::string_view uri,
    std::string_view query_string, std::string_view headers,
    std::string_view signed_headers, std::string_view content_sha256) {
  // CanonicalRequest =
  //   HTTPRequestMethod + '\n' +
  //   CanonicalURI + '\n' +
  //   CanonicalQueryString + '\n' +
  //   CanonicalHeaders + '\n\n' +
  //   SignedHeaders + '\n' +
  //   HexEncode(Hash(RequestPayload))
  std::string canonical_request =
      std::string(method) + "\n" + std::string(uri) + "\n" +
      std::string(query_string) + "\n" + std::string(headers) + "\n\n" +
      std::string(signed_headers) + "\n" + std::string(content_sha256);
  return utils::Sha256Hash(canonical_request);
}

std::string minio::signer::GetStringToSign(
    utils::Time& date, std::string_view scope,
    std::string_view canonical_request_hash) {
  return "AWS4-HMAC-SHA256\n" + date.ToAmzDate() + "\n" + std::string(scope) +
         "\n" + std::string(canonical_request_hash);
}

std::string minio::signer::HmacHash(std::string_view key,
                                    std::string_view data) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> hash;
  unsigned int hash_len;

  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<unsigned char const*>(data.data()),
       static_cast<int>(data.size()), hash.data(), &hash_len);

  return std::string{reinterpret_cast<char const*>(hash.data()), hash_len};
}

std::string minio::signer::GetSigningKey(std::string_view secret_key,
                                         utils::Time& date,
                                         std::string_view region,
                                         std::string_view service_name) {
  std::string date_key =
      HmacHash("AWS4" + std::string(secret_key), date.ToSignerDate());
  std::string date_regionkey = HmacHash(date_key, region);
  std::string date_regionservice_key = HmacHash(date_regionkey, service_name);
  return HmacHash(date_regionservice_key, "aws4_request");
}

std::string minio::signer::GetSignature(std::string_view signing_key,
                                        std::string_view string_to_sign) {
  std::string hash = HmacHash(signing_key, string_to_sign);
  std::string signature;
  char buf[3];
  for (int i = 0; i < hash.size(); ++i) {
    sprintf(buf, "%02x", (unsigned char)hash[i]);
    signature += buf;
  }
  return signature;
}

std::string minio::signer::GetAuthorization(std::string_view access_key,
                                            std::string_view scope,
                                            std::string_view signed_headers,
                                            std::string_view signature) {
  return "AWS4-HMAC-SHA256 Credential=" + std::string(access_key) + "/" +
         std::string(scope) + ", " +
         "SignedHeaders=" + std::string(signed_headers) + ", " +
         "Signature=" + std::string(signature);
}

minio::utils::Multimap& minio::signer::SignV4(
    std::string_view service_name, http::Method& method, std::string_view uri,
    std::string_view region, utils::Multimap& headers,
    utils::Multimap& query_params, std::string_view access_key,
    std::string_view secret_key, std::string_view content_sha256,
    utils::Time& date) {
  std::string scope = GetScope(date, region, service_name);

  std::string signed_headers;
  std::string canonical_headers;
  headers.GetCanonicalHeaders(signed_headers, canonical_headers);

  std::string canonical_query_string = query_params.GetCanonicalQueryString();

  std::string canonical_request_hash = GetCanonicalRequestHash(
      http::MethodToString(method), uri, canonical_query_string,
      canonical_headers, signed_headers, content_sha256);

  std::string string_to_sign =
      GetStringToSign(date, scope, canonical_request_hash);

  std::string signing_key =
      GetSigningKey(secret_key, date, region, service_name);

  std::string signature = GetSignature(signing_key, string_to_sign);

  std::string authorization =
      GetAuthorization(access_key, scope, signed_headers, signature);

  headers.Add("Authorization", authorization);
  return headers;
}

minio::utils::Multimap& minio::signer::SignV4S3(
    http::Method& method, std::string_view uri, std::string_view region,
    utils::Multimap& headers, utils::Multimap& query_params,
    std::string_view access_key, std::string_view secret_key,
    std::string_view content_sha256, utils::Time& date) {
  return SignV4("s3", method, uri, region, headers, query_params, access_key,
                secret_key, content_sha256, date);
}

minio::utils::Multimap& minio::signer::SignV4STS(
    http::Method& method, std::string_view uri, std::string_view region,
    utils::Multimap& headers, utils::Multimap& query_params,
    std::string_view access_key, std::string_view secret_key,
    std::string_view content_sha256, utils::Time& date) {
  return SignV4("sts", method, uri, region, headers, query_params, access_key,
                secret_key, content_sha256, date);
}
