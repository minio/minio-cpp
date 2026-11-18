// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2024 MinIO, Inc.
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
//
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "miniocpp/providers.h"

#include <INIReader.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <algorithm>
#include <fstream>
#include <iosfwd>
#include <list>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "miniocpp/credentials.h"
#include "miniocpp/error.h"
#include "miniocpp/http.h"
#include "miniocpp/signer.h"
#include "miniocpp/utils.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <ws2def.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace minio::creds {

error::Error checkLoopbackHost(const std::string& host) {
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int status;
  struct addrinfo* res = nullptr;
  if ((status = getaddrinfo(host.c_str(), nullptr, &hints, &res)) != 0) {
    return error::Error(std::string("getaddrinfo: ") + gai_strerror(status));
  }

  for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
    std::string ip(inet_ntoa(((struct sockaddr_in*)ai->ai_addr)->sin_addr));
    if (!utils::StartsWith(ip, "127.")) {
      return error::Error(host + " is not loopback only host");
    }
  }

  freeaddrinfo(res);  // free the linked list

  return error::SUCCESS;
}

Provider::~Provider() {}

ChainedProvider::~ChainedProvider() {}

Credentials ChainedProvider::Fetch() {
  std::lock_guard<std::mutex> lock(fetch_mutex_);
  if (err_) return Credentials{err_};

  if (creds_) return creds_;

  if (provider_ != nullptr) {
    creds_ = provider_->Fetch();
    if (creds_) return creds_;
  }

  for (auto provider : providers_) {
    provider_ = provider;
    creds_ = provider_->Fetch();
    if (creds_) return creds_;
  }

  return Credentials{error::Error("All providers fail to fetch credentials")};
}

StaticProvider::StaticProvider(std::string access_key, std::string secret_key,
                               std::string session_token) {
  this->creds_ = Credentials(error::SUCCESS, std::move(access_key),
                             std::move(secret_key), std::move(session_token));
}

StaticProvider::~StaticProvider() {}

Credentials StaticProvider::Fetch() { return creds_; }

EnvAwsProvider::EnvAwsProvider() {
  std::string access_key;
  std::string secret_key;
  std::string session_token;

  if (!utils::GetEnv(access_key, "AWS_ACCESS_KEY_ID")) {
    utils::GetEnv(access_key, "AWS_ACCESS_KEY");
  }
  if (!utils::GetEnv(secret_key, "AWS_SECRET_ACCESS_KEY")) {
    utils::GetEnv(secret_key, "AWS_SECRET_KEY");
  }
  utils::GetEnv(session_token, "AWS_SESSION_TOKEN");

  this->creds_ = Credentials(error::SUCCESS, std::move(access_key),
                             std::move(secret_key), std::move(session_token));
}

EnvAwsProvider::~EnvAwsProvider() {}

Credentials EnvAwsProvider::Fetch() { return creds_; }

EnvMinioProvider::EnvMinioProvider() {
  std::string access_key;
  std::string secret_key;

  utils::GetEnv(access_key, "MINIO_ACCESS_KEY");
  utils::GetEnv(secret_key, "MINIO_SECRET_KEY");
  this->creds_ =
      Credentials(error::SUCCESS, std::move(access_key), std::move(secret_key));
}

EnvMinioProvider::~EnvMinioProvider() {}

Credentials EnvMinioProvider::Fetch() { return creds_; }

AwsConfigProvider::AwsConfigProvider(std::string filename,
                                     std::string profile) {
  if (filename.empty()) {
    if (!utils::GetEnv(filename, "AWS_SHARED_CREDENTIALS_FILE")) {
      filename = utils::GetHomeDir() + "/aws/credentials";
    }
  }

  if (profile.empty()) {
    if (!utils::GetEnv(profile, "AWS_PROFILE")) profile = "default";
  }

  INIReader reader(filename);
  if (reader.ParseError() < 0) {
    this->creds_ = Credentials{error::Error("unable to read " + filename)};
  } else {
    this->creds_ = Credentials{error::SUCCESS,
                               reader.Get(profile, "aws_access_key_id", ""),
                               reader.Get(profile, "aws_secret_access_key", ""),
                               reader.Get(profile, "aws_session_token", "")};
  }
}

AwsConfigProvider::~AwsConfigProvider() {}

Credentials AwsConfigProvider::Fetch() { return creds_; }

MinioClientConfigProvider::MinioClientConfigProvider(std::string filename,
                                                     std::string alias) {
  if (filename.empty()) filename = utils::GetHomeDir() + "/.mc/config.json";

  if (alias.empty()) {
    if (!utils::GetEnv(alias, "MINIO_ALIAS")) alias = "s3";
  }

  std::ifstream ifs(filename);
  nlohmann::json json = nlohmann::json::parse(ifs);
  ifs.close();

  nlohmann::json aliases;
  if (json.contains("hosts")) {
    aliases = json["hosts"];
  } else if (json.contains("aliases")) {
    aliases = json["aliases"];
  } else {
    this->creds_ =
        Credentials{error::Error("invalid configuration in file " + filename)};
    return;
  }

  if (!aliases.contains(alias)) {
    this->creds_ = Credentials{error::Error(
        "alias " + alias + " not found in MinIO client configuration file " +
        filename)};
    return;
  }

  this->creds_ = Credentials{error::SUCCESS, aliases[alias]["accessKey"],
                             aliases[alias]["secretKey"]};
}

MinioClientConfigProvider::~MinioClientConfigProvider() {}

Credentials MinioClientConfigProvider::Fetch() { return creds_; }

AssumeRoleProvider::AssumeRoleProvider(
    http::Url sts_endpoint, std::string access_key, std::string secret_key,
    unsigned int duration_seconds, std::string policy, std::string region,
    std::string role_arn, std::string role_session_name,
    std::string external_id, std::string token_revoke_type) {
  this->sts_endpoint_ = sts_endpoint;
  if (this->sts_endpoint_.path.empty()) this->sts_endpoint_.path = "/";
  this->access_key_ = access_key;
  this->secret_key_ = secret_key;
  this->region_ = region;

  if (duration_seconds < DEFAULT_DURATION_SECONDS) {
    duration_seconds = DEFAULT_DURATION_SECONDS;
  }

  utils::Multimap map;
  map.Add("Action", "AssumeRole");
  map.Add("Version", "2011-06-15");
  map.Add("DurationSeconds", std::to_string(duration_seconds));
  if (!role_arn.empty()) map.Add("RoleArn", role_arn);
  if (!role_session_name.empty()) {
    map.Add("RoleSessionName", role_session_name);
  }
  if (!policy.empty()) map.Add("Policy", policy);
  if (!external_id.empty()) map.Add("ExternalId", external_id);
  if (!token_revoke_type.empty()) {
    map.Add("TokenRevokeType", token_revoke_type);
  }

  this->body_ = map.ToQueryString();
  this->content_sha256_ = utils::Sha256Hash(body_);
}

AssumeRoleProvider::~AssumeRoleProvider() {}

Credentials AssumeRoleProvider::Fetch() {
  if (err_) return Credentials{err_};

  if (creds_) return creds_;

  utils::UtcTime date = utils::UtcTime::Now();
  utils::Multimap headers;
  headers.Add("Content-Type", "application/x-www-form-urlencoded");
  headers.Add("Host", sts_endpoint_.host);
  headers.Add("X-Amz-Date", date.ToAmzDate());

  http::Method method = http::Method::kPost;
  signer::SignV4STS(method, sts_endpoint_.path, region_, headers,
                    utils::Multimap(), access_key_, secret_key_,
                    content_sha256_, date);

  http::Request req(method, sts_endpoint_);
  req.headers = headers;
  req.body = body_;
  http::Response resp = req.Execute();
  if (!resp) {
    creds_ = Credentials{resp.Error()};
  } else {
    auto parse_res =
        Credentials::ParseXML(resp.body, "AssumeRoleResponse/AssumeRoleResult");
    if (parse_res) {
      creds_ = std::move(*parse_res);
    } else {
      creds_ = Credentials{parse_res.error()};
    }
  }

  return creds_;
}

WebIdentityClientGrantsProvider::WebIdentityClientGrantsProvider(
    JwtFunction jwtfunc, http::Url sts_endpoint, unsigned int duration_seconds,
    std::string policy, std::string role_arn, std::string role_session_name,
    std::string token_revoke_type, std::string extra_ca_file) {
  this->jwtfunc_ = jwtfunc;
  this->sts_endpoint_ = sts_endpoint;
  this->duration_seconds_ = duration_seconds;
  this->policy_ = policy;
  this->role_arn_ = role_arn;
  this->role_session_name_ = role_session_name;
  this->token_revoke_type_ = token_revoke_type;
  this->extra_ca_file_ = extra_ca_file;
}

WebIdentityClientGrantsProvider::~WebIdentityClientGrantsProvider() {}

unsigned int WebIdentityClientGrantsProvider::getDurationSeconds(
    unsigned int expiry) const {
  if (duration_seconds_) expiry = duration_seconds_;
  if (expiry > MAX_DURATION_SECONDS) return MAX_DURATION_SECONDS;
  if (expiry == 0) return expiry;
  if (expiry < MIN_DURATION_SECONDS) return MIN_DURATION_SECONDS;
  return expiry;
}

Credentials WebIdentityClientGrantsProvider::Fetch() {
  if (creds_) return creds_;

  Jwt jwt = jwtfunc_();

  utils::Multimap map;
  map.Add("Version", "2011-06-15");
  unsigned int duration_seconds = getDurationSeconds(jwt.expiry);
  if (duration_seconds) {
    map.Add("DurationSeconds", std::to_string(duration_seconds));
  }
  if (!policy_.empty()) map.Add("Policy", policy_);
  if (!token_revoke_type_.empty()) {
    map.Add("TokenRevokeType", token_revoke_type_);
  }

  if (IsWebIdentity()) {
    map.Add("Action", "AssumeRoleWithWebIdentity");
    map.Add("WebIdentityToken", jwt.token);
    if (!jwt.access_token.empty()) {
      map.Add("WebIdentityAccessToken", jwt.access_token);
    }
    if (!jwt.refresh_token.empty()) {
      map.Add("WebIdentityRefreshToken", jwt.refresh_token);
    }
    if (!role_arn_.empty()) {
      map.Add("RoleArn", role_arn_);
      if (!role_session_name_.empty()) {
        map.Add("RoleSessionName", role_session_name_);
      } else {
        map.Add("RoleSessionName", utils::UtcTime::Now().ToISO8601UTC());
      }
    }
  } else {
    map.Add("Action", "AssumeRoleWithClientGrants");
    map.Add("Token", jwt.token);
  }

  http::Url url = sts_endpoint_;
  url.query_string = map.ToQueryString();
  http::Request req(http::Method::kPost, url);
  req.extra_ca_file = extra_ca_file_;
  http::Response resp = req.Execute();
  if (!resp) {
    creds_ = Credentials{resp.Error()};
  } else {
    auto parse_res = Credentials::ParseXML(
        resp.body, IsWebIdentity() ? "AssumeRoleWithWebIdentityResponse/"
                                     "AssumeRoleWithWebIdentityResult"
                                   : "AssumeRoleWithClientGrantsResponse/"
                                     "AssumeRoleWithClientGrantsResult");
    if (parse_res) {
      creds_ = std::move(*parse_res);
    } else {
      creds_ = Credentials{parse_res.error()};
    }
  }
  return creds_;
}

ClientGrantsProvider::ClientGrantsProvider(
    JwtFunction jwtfunc, http::Url sts_endpoint, unsigned int duration_seconds,
    std::string policy, std::string role_arn, std::string role_session_name,
    std::string token_revoke_type)
    : WebIdentityClientGrantsProvider(jwtfunc, sts_endpoint, duration_seconds,
                                      policy, role_arn, role_session_name,
                                      token_revoke_type) {}

ClientGrantsProvider::~ClientGrantsProvider() {}

bool ClientGrantsProvider::IsWebIdentity() const { return false; }

WebIdentityProvider::WebIdentityProvider(
    JwtFunction jwtfunc, http::Url sts_endpoint, unsigned int duration_seconds,
    std::string policy, std::string role_arn, std::string role_session_name,
    std::string token_revoke_type, std::string extra_ca_file)
    : WebIdentityClientGrantsProvider(jwtfunc, sts_endpoint, duration_seconds,
                                      policy, role_arn, role_session_name,
                                      token_revoke_type, extra_ca_file) {}

WebIdentityProvider::~WebIdentityProvider() {}

bool WebIdentityProvider::IsWebIdentity() const { return true; }

namespace {
// hasValidPort reports whether the authority of value has no port, or a port
// that is all digits, nonzero and at most 65535. http::Url::Parse reads only a
// numeric prefix of the port, so "host:443extra" would otherwise be accepted.
bool hasValidPort(std::string value) {
  size_t pos = value.find("://");
  if (pos != std::string::npos) value.erase(0, pos + 3);
  // Cut the authority where http::Url::Parse does: at the first '/' when
  // there is one, even after a '?', and otherwise at the first '?'.
  pos = value.find('/');
  value = value.substr(0, pos != std::string::npos ? pos : value.find('?'));

  std::string port;
  if (!value.empty() && value.front() == '[') {
    pos = value.find(']');
    if (pos == std::string::npos) return false;
    if (pos + 1 == value.size()) return true;
    if (value[pos + 1] != ':') return false;
    port = value.substr(pos + 2);
  } else {
    if (std::count(value.begin(), value.end(), ':') != 1) {
      return std::count(value.begin(), value.end(), ':') == 0;
    }
    port = value.substr(value.find(':') + 1);
  }
  if (port.empty() || !std::all_of(port.begin(), port.end(), [](char c) {
        return c >= '0' && c <= '9';
      })) {
    return false;
  }
  // Leading zeros are fine ("008443" is 8443), but port 0 means "no port" to
  // Request and would silently select the default port.
  port.erase(0, port.find_first_not_of('0'));
  return !port.empty() && port.size() <= 5 && std::stoi(port) <= 65535;
}
}  // namespace

error::Error ResolveStsEndpoint(http::Url& out, bool& from_env,
                                const http::Url& custom,
                                const std::string& region) {
  from_env = false;
  if (custom) {
    out = custom;
    return error::SUCCESS;
  }

  std::string sts_url, ignore_configured;
  utils::GetEnv(sts_url, "AWS_ENDPOINT_URL_STS");
  utils::GetEnv(ignore_configured, "AWS_IGNORE_CONFIGURED_ENDPOINT_URLS");
  if (!sts_url.empty() && utils::ToLower(ignore_configured) != "true") {
    // Check the port first: Url::Parse throws on one that overflows.
    if (!hasValidPort(sts_url)) {
      return error::Error("invalid STS endpoint " + sts_url +
                          " from environment");
    }
    try {
      out = http::Url::Parse(sts_url);
    } catch (const std::exception&) {
      out = http::Url();
    }
    if (!out) {
      return error::Error("invalid STS endpoint " + sts_url +
                          " from environment");
    }
    // Url::Parse drops the first '/' of the path and Request adds one back
    // only when it is missing; restore it so "//a" stays "//a".
    if (!out.path.empty() && out.path.front() == '/') out.path.insert(0, "/");
    from_env = true;
    return error::SUCCESS;
  }

  out = http::Url();
  out.https = true;
  out.host = "sts.amazonaws.com";
  if (!region.empty()) {
    out.host = "sts." + region + ".amazonaws.com";
    if (region.compare(0, 3, "cn-") == 0) out.host += ".cn";
  }
  return error::SUCCESS;
}

error::Error CheckCaBundle(const std::string& path) {
  BIO* bio = BIO_new_file(path.c_str(), "r");
  if (bio == nullptr) {
    return error::Error("unable to read AWS_CA_BUNDLE " + path);
  }
  // The _AUX reader also accepts "TRUSTED CERTIFICATE" blocks, like the
  // store loader the transport uses.
  size_t count = 0;
  while (X509* cert = PEM_read_bio_X509_AUX(bio, nullptr, nullptr, nullptr)) {
    X509_free(cert);
    count++;
  }
  BIO_free(bio);
  ERR_clear_error();
  if (count == 0) {
    return error::Error("AWS_CA_BUNDLE " + path +
                        " contains no PEM certificates");
  }
  return error::SUCCESS;
}

IamAwsProvider::IamAwsProvider(http::Url custom_endpoint) {
  this->custom_endpoint_ = custom_endpoint;
  utils::GetEnv(this->token_file_, "AWS_WEB_IDENTITY_TOKEN_FILE");
  utils::GetEnv(this->aws_region_, "AWS_REGION");
  utils::GetEnv(this->role_arn_, "AWS_ROLE_ARN");
  utils::GetEnv(this->role_session_name_, "AWS_ROLE_SESSION_NAME");
  utils::GetEnv(this->relative_uri_, "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
  if (!this->relative_uri_.empty() && this->relative_uri_.front() != '/') {
    this->relative_uri_ = "/" + this->relative_uri_;
  }
  utils::GetEnv(this->full_uri_, "AWS_CONTAINER_CREDENTIALS_FULL_URI");
}

IamAwsProvider::~IamAwsProvider() {}

Credentials IamAwsProvider::Fetch() {
  if (creds_) return creds_;

  http::Url url = custom_endpoint_;
  if (!token_file_.empty()) {
    // The environment is read at retrieval time and only on this path.
    std::string ca_bundle;
    bool from_env = false;
    if (error::Error err =
            ResolveStsEndpoint(url, from_env, custom_endpoint_, aws_region_)) {
      creds_ = Credentials{err};
      return creds_;
    }
    // AWS_CA_BUNDLE belongs to the AWS_ENDPOINT_URL_STS opt-in: with an
    // explicit endpoint or the AWS default it is not read at all.
    if (from_env) utils::GetEnv(ca_bundle, "AWS_CA_BUNDLE");
    if (!ca_bundle.empty()) {
      if (error::Error err = CheckCaBundle(ca_bundle)) {
        creds_ = Credentials{err};
        return creds_;
      }
    }

    WebIdentityProvider provider = WebIdentityProvider(
        [&token_file = token_file_]() -> Jwt {
          std::ifstream ifs(token_file);
          nlohmann::json json = nlohmann::json::parse(ifs);
          ifs.close();
          return Jwt(std::move(json["access_token"]), json["expires_in"]);
        },
        url, 0, "", role_arn_, role_session_name_, "", ca_bundle);
    creds_ = provider.Fetch();
    // The transport reports a bundle that vanished after CheckCaBundle().
    if (!ca_bundle.empty() &&
        creds_.err.String() == "unable to load CA file " + ca_bundle) {
      creds_ = Credentials{
          error::Error("unable to load AWS_CA_BUNDLE " + ca_bundle)};
    }
    return creds_;
  }

  if (!relative_uri_.empty()) {
    if (!url) {
      url.https = true;
      url.host = "169.254.170.2";
      url.path = relative_uri_;
    }
  } else if (!full_uri_.empty()) {
    if (!url) url = http::Url::Parse(full_uri_);
    if (error::Error err = checkLoopbackHost(url.host)) {
      creds_ = Credentials{err};
      return creds_;
    }
  } else {
    if (!url) {
      url.https = true;
      url.host = "169.254.169.254";
      url.path = "/latest/meta-data/iam/security-credentials/";
    }

    std::string role_name;
    if (error::Error err = getRoleName(role_name, url)) {
      creds_ = Credentials{err};
      return creds_;
    }

    url.path += "/" + role_name;
  }

  creds_ = fetch(url);
  return creds_;
}

Credentials IamAwsProvider::fetch(http::Url url) {
  http::Request req(http::Method::kGet, url);
  http::Response resp = req.Execute();
  if (!resp) return Credentials{resp.Error()};

  nlohmann::json json = nlohmann::json::parse(resp.body);
  std::string code = json.value("Code", "Success");
  if (code != "Success") {
    return Credentials{error::Error(url.String() + " failed with code " + code +
                                    " and message " +
                                    json.value("Message", ""))};
  }

  std::string expiration = json["Expiration"];
  return Credentials{error::SUCCESS, json["AccessKeyId"],
                     json["SecretAccessKey"], json["Token"],
                     utils::UtcTime::FromISO8601UTC(expiration.c_str())};
}

error::Error IamAwsProvider::getRoleName(std::string& role_name,
                                         http::Url url) const {
  http::Request req(http::Method::kGet, url);
  http::Response resp = req.Execute();
  if (!resp) return resp.Error();

  std::list<std::string> role_names;
  std::string lines = resp.body;
  size_t pos;
  while ((pos = lines.find("\n")) != std::string::npos) {
    role_names.push_back(lines.substr(0, pos));
    lines.erase(0, pos + 1);
  }
  if (!lines.empty()) role_names.push_back(lines);

  if (role_names.empty()) {
    return error::Error(std::string("no IAM roles attached to EC2 service ") +
                        url.String());
  }

  role_name = utils::Trim(role_names.front(), '\r');
  return error::SUCCESS;
}

LdapIdentityProvider::LdapIdentityProvider(http::Url sts_endpoint,
                                           std::string ldap_username,
                                           std::string ldap_password,
                                           std::string policy,
                                           unsigned int duration_seconds,
                                           std::string token_revoke_type,
                                           std::string config_name) {
  this->sts_endpoint_ = sts_endpoint;
  utils::Multimap map;
  map.Add("Action", "AssumeRoleWithLDAPIdentity");
  map.Add("Version", "2011-06-15");
  map.Add("LDAPUsername", ldap_username);
  map.Add("LDAPPassword", ldap_password);
  if (!policy.empty()) map.Add("Policy", policy);
  if (duration_seconds > 0) {
    map.Add("DurationSeconds", std::to_string(duration_seconds));
  }
  if (!token_revoke_type.empty()) {
    map.Add("TokenRevokeType", token_revoke_type);
  }
  if (!config_name.empty()) map.Add("ConfigName", config_name);
  this->sts_endpoint_.query_string = map.ToQueryString();
}

LdapIdentityProvider::~LdapIdentityProvider() {}

Credentials LdapIdentityProvider::Fetch() {
  if (creds_) return creds_;

  http::Request req(http::Method::kPost, sts_endpoint_);
  http::Response resp = req.Execute();
  if (!resp) return Credentials{resp.Error()};

  auto parse_res = Credentials::ParseXML(
      resp.body,
      "AssumeRoleWithLDAPIdentityResponse/AssumeRoleWithLDAPIdentityResult");
  if (parse_res) {
    creds_ = std::move(*parse_res);
  } else {
    creds_ = Credentials{parse_res.error()};
  }
  return creds_;
}

CertificateIdentityProvider::CertificateIdentityProvider(
    http::Url sts_endpoint, std::string key_file, std::string cert_file,
    std::string ssl_cert_file, unsigned int duration_seconds,
    std::string token_revoke_type) {
  if (!sts_endpoint.https) {
    this->err_ = error::Error("sts endpoint scheme must be HTTPS");
    return;
  }

  if (key_file.empty() || cert_file.empty()) {
    this->err_ = error::Error("client key and certificate must be provided");
    return;
  }

  unsigned int expiry = duration_seconds;
  if (duration_seconds < DEFAULT_DURATION_SECONDS) {
    expiry = DEFAULT_DURATION_SECONDS;
  }

  utils::Multimap map;
  map.Add("Action", "AssumeRoleWithCertificate");
  map.Add("Version", "2011-06-15");
  map.Add("DurationSeconds", std::to_string(expiry));
  if (!token_revoke_type.empty()) {
    map.Add("TokenRevokeType", token_revoke_type);
  }

  sts_endpoint_ = sts_endpoint;
  sts_endpoint_.query_string = map.ToQueryString();
  key_file_ = key_file;
  cert_file_ = cert_file;
  ssl_cert_file_ = ssl_cert_file;
}

CertificateIdentityProvider::~CertificateIdentityProvider() {}

Credentials CertificateIdentityProvider::Fetch() {
  if (err_) return Credentials{err_};

  if (creds_) return creds_;

  http::Request req(http::Method::kPost, sts_endpoint_);
  req.ssl_cert_file = ssl_cert_file_;
  req.key_file = key_file_;
  req.cert_file = cert_file_;

  http::Response resp = req.Execute();
  if (!resp) return Credentials{resp.Error()};

  auto parse_res = Credentials::ParseXML(
      resp.body,
      "AssumeRoleWithCertificateResponse/AssumeRoleWithCertificateResult");
  if (parse_res) {
    creds_ = std::move(*parse_res);
  } else {
    creds_ = Credentials{parse_res.error()};
  }
  return creds_;
}

CustomTokenIdentityProvider::CustomTokenIdentityProvider(
    http::Url sts_endpoint, std::string role_arn, std::string token,
    unsigned int duration_seconds, std::string token_revoke_type) {
  if (sts_endpoint.host.empty()) {
    this->err_ = error::Error("STS endpoint unknown");
    return;
  }
  this->sts_endpoint_ = sts_endpoint;
  this->role_arn_ = role_arn;
  this->token_ = token;
  this->duration_seconds_ = duration_seconds;
  this->token_revoke_type_ = token_revoke_type;
}

CustomTokenIdentityProvider::~CustomTokenIdentityProvider() {}

Credentials CustomTokenIdentityProvider::Fetch() {
  if (err_) return Credentials{err_};

  if (creds_) return creds_;

  utils::Multimap map;
  map.Add("Action", "AssumeRoleWithCustomToken");
  map.Add("Version", "2011-06-15");
  map.Add("RoleArn", role_arn_);
  map.Add("Token", token_);
  if (duration_seconds_ > 0) {
    map.Add("DurationSeconds", std::to_string(duration_seconds_));
  }
  if (!token_revoke_type_.empty()) {
    map.Add("TokenRevokeType", token_revoke_type_);
  }

  http::Url url = sts_endpoint_;
  url.query_string = map.ToQueryString();
  http::Request req(http::Method::kPost, url);
  http::Response resp = req.Execute();
  if (!resp) {
    creds_ = Credentials{resp.Error()};
  } else {
    auto parse_res = Credentials::ParseXML(
        resp.body,
        "AssumeRoleWithCustomTokenResponse/AssumeRoleWithCustomTokenResult");
    if (parse_res) {
      creds_ = std::move(*parse_res);
    } else {
      creds_ = Credentials{parse_res.error()};
    }
  }
  return creds_;
}

}  // namespace minio::creds
