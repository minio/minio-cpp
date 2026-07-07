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

#include "miniocpp/baseclient.h"

#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ostream>
#include <pugixml.hpp>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <type_traits>

#include "miniocpp/args.h"
#include "miniocpp/config.h"
#include "miniocpp/credentials.h"
#include "miniocpp/error.h"
#include "miniocpp/http.h"
#include "miniocpp/providers.h"
#include "miniocpp/request.h"
#include "miniocpp/response.h"
#include "miniocpp/select.h"
#include "miniocpp/signer.h"
#include "miniocpp/types.h"
#include "miniocpp/utils.h"

#ifdef MINIO_CPP_RDMA
#include "miniocpp/nvidia-cuobjclient.h"
#include "miniocpp/rdma.h"
#endif

// We want exactly `minio::s3::BaseClient::GetObject()` symbol and nothing else.
#if defined(GetObject)
#undef GetObject
#endif

namespace minio::s3 {

utils::Multimap GetCommonListObjectsQueryParams(
    const std::string& delimiter, const std::string& encoding_type,
    unsigned int max_keys, const std::string& prefix) {
  utils::Multimap query_params;
  query_params.Add("delimiter", delimiter);
  query_params.Add("max-keys", std::to_string(max_keys > 0 ? max_keys : 1000));
  query_params.Add("prefix", prefix);
  if (!encoding_type.empty()) query_params.Add("encoding-type", encoding_type);
  return query_params;
}

BaseClient::BaseClient(BaseUrl base_url, creds::Provider* provider)
    : base_url_(std::move(base_url)), provider_(provider) {
  if (!base_url_) {
    std::cerr << "valid base url must be provided; " << base_url_.Error()
              << std::endl;
    std::terminate();
  }
}

error::Error BaseClient::SetAppInfo(std::string_view app_name,
                                    std::string_view app_version) {
  if (app_name.empty() || app_version.empty()) {
    return error::Error("Application name/version cannot be empty");
  }

  user_agent_ = std::string(DEFAULT_USER_AGENT) + " " + std::string(app_name) +
                "/" + std::string(app_version);
  return error::SUCCESS;
}

void BaseClient::HandleRedirectResponse(std::string& code, std::string& message,
                                        int status_code, http::Method method,
                                        const utils::Multimap& headers,
                                        const std::string& bucket_name,
                                        bool retry) {
  switch (status_code) {
    case 301:
      code = "PermanentRedirect";
      message = "Moved Permanently";
      break;
    case 307:
      code = "Redirect";
      message = "Temporary redirect";
      break;
    case 400:
      code = "BadRequest";
      message = "Bad request";
      break;
    default:
      code.clear();
      message.clear();
      break;
  }

  const std::string region = headers.GetFront("x-amz-bucket-region");

  if (!message.empty() && !region.empty()) {
    message += "; use region " + region;
  }

  if (retry && !region.empty() && method == http::Method::kHead &&
      !bucket_name.empty()) {
    std::shared_lock<std::shared_mutex> lock(region_map_mutex_);
    if (auto it = region_map_.find(bucket_name);
        it != region_map_.end() && !it->second.empty()) {
      code = "RetryHead";
      message.clear();
    }
  }
}

Result<Response> BaseClient::GetErrorResponse(http::Response resp,
                                              std::string_view resource,
                                              http::Method method,
                                              const std::string& bucket_name,
                                              const std::string& object_name) {
  if (!resp.error.empty()) {
    return tl::make_unexpected(error::Error(resp.error));
  }

  if (!resp.body.empty()) {
    std::list<std::string> values = resp.headers.Get("Content-Type");
    for (auto& value : values) {
      if (utils::Contains(utils::ToLower(value), "application/xml")) {
        auto parsed =
            Response::ParseXML(resp.body, resp.status_code, resp.headers);
        if (!parsed) {
          return tl::make_unexpected(parsed.error());
        }
        return tl::make_unexpected(
            error::Error(parsed->code + ": " + parsed->message));
      }
    }

    return error::make<Response>("invalid response received; status code: " +
                                 std::to_string(resp.status_code) +
                                 "; content-type: " + utils::Join(values, ","));
  }

  // Format the error message based on status code.
  switch (resp.status_code) {
    case 301:
    case 307:
    case 400: {
      std::string code;
      std::string message;
      HandleRedirectResponse(code, message, resp.status_code, method,
                             resp.headers, bucket_name, true);
      if (code == "RetryHead") {
        return tl::make_unexpected(error::Error("RetryHead"));
      }
      return tl::make_unexpected(error::Error(code + ": " + message));
    }
    case 403:
      return tl::make_unexpected(error::Error("AccessDenied: Access denied"));
    case 404:
      if (!object_name.empty()) {
        return tl::make_unexpected(
            error::Error("NoSuchKey: Object does not exist"));
      } else if (bucket_name.empty()) {
        return tl::make_unexpected(
            error::Error("NoSuchBucket: Bucket does not exist"));
      } else {
        return tl::make_unexpected(
            error::Error("ResourceNotFound: Request resource not found"));
      }
    case 405:
      return tl::make_unexpected(error::Error(
          "MethodNotAllowed: The specified method is not allowed against "
          "this resource"));
    case 409:
      if (bucket_name.empty()) {
        return tl::make_unexpected(
            error::Error("NoSuchBucket: Bucket does not exist"));
      } else {
        return tl::make_unexpected(
            error::Error("ResourceConflict: Request resource conflicts"));
      }
    case 501:
      return tl::make_unexpected(error::Error(
          "MethodNotAllowed: The specified method is not allowed against "
          "this resource"));
    default:
      return error::make<Response>("server failed with HTTP status code " +
                                   std::to_string(resp.status_code));
  }
}

Result<Response> BaseClient::execute(Request& req) {
  req.user_agent = user_agent_;
  req.ignore_cert_check = ignore_cert_check_;
  if (!ssl_cert_file_.empty()) req.ssl_cert_file = ssl_cert_file_;
  http::Request request = req.ToHttpRequest(provider_);
  request.debug = debug_;
  http::Response response = request.Execute();
  if (response) {
    Response resp;
    resp.status_code = response.status_code;
    resp.headers = response.headers;
    resp.data = response.body;
    return resp;
  }

  auto err = GetErrorResponse(response, request.url.path, req.method,
                              req.bucket_name, req.object_name);
  if (!err) {
    std::string err_str = err.error().String();
    if (err_str.find("NoSuchBucket") != std::string::npos ||
        err_str == "RetryHead") {
      std::unique_lock<std::shared_mutex> lock(region_map_mutex_);
      region_map_.erase(req.bucket_name);
    }
  }
  return err;
}

Result<Response> BaseClient::Execute(Request& req) {
  auto exec_resp = execute(req);
  if (exec_resp) return exec_resp;
  if (exec_resp.error().String() != "RetryHead") return exec_resp;

  // Retry only once on RetryHead error.
  exec_resp = execute(req);
  if (exec_resp) return exec_resp;
  if (exec_resp.error().String() != "RetryHead") return exec_resp;

  return tl::make_unexpected(exec_resp.error());
}

Result<GetRegionResponse> BaseClient::GetRegion(const std::string& bucket_name,
                                                const std::string& region) {
  std::string base_region = base_url_.region;
  if (!region.empty()) {
    if (!base_region.empty() && base_region != region) {
      return error::make<GetRegionResponse>("region must be " + base_region +
                                            ", but passed " + region);
    }

    return GetRegionResponse(region);
  }

  if (!base_region.empty()) {
    return GetRegionResponse(base_region);
  }

  if (bucket_name.empty() || provider_ == nullptr) {
    return GetRegionResponse("us-east-1");
  }

  {
    std::shared_lock<std::shared_mutex> lock(region_map_mutex_);
    if (auto it = region_map_.find(bucket_name);
        it != region_map_.end() && !it->second.empty()) {
      return GetRegionResponse(it->second);
    }
  }
  Request req(http::Method::kGet, "us-east-1", base_url_, utils::Multimap(),
              utils::Multimap());
  req.query_params.Add("location", "");
  req.bucket_name = bucket_name;

  auto exec_gr = Execute(req);
  if (!exec_gr) {
    return tl::make_unexpected(exec_gr.error());
  }

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(exec_gr->data.data());
  if (!result) {
    return error::make<GetRegionResponse>("unable to parse XML");
  }
  auto text = xdoc.select_node("/LocationConstraint/text()");
  std::string value = text.node().value();

  if (value.empty()) {
    value = "us-east-1";
  } else if (value == "EU") {
    if (!base_url_.aws_domain_suffix.empty()) value = "eu-west-1";
  }

  {
    std::unique_lock<std::shared_mutex> lock(region_map_mutex_);
    region_map_[bucket_name] = value;
  }

  return GetRegionResponse(value);
}

Result<AbortMultipartUploadResponse> BaseClient::AbortMultipartUpload(
    AbortMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("uploadId", args.upload_id);

  auto exec_abort = Execute(req);
  if (!exec_abort) return tl::make_unexpected(exec_abort.error());
  return AbortMultipartUploadResponse(std::move(*exec_abort));
}

Result<BucketExistsResponse> BaseClient::BucketExists(BucketExistsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }
  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    if (get_resp.error().String().find("NoSuchBucket") != std::string::npos)
      return BucketExistsResponse(false);
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kHead, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  auto bucket_exec = Execute(req);
  if (bucket_exec) {
    return BucketExistsResponse(true);
  }
  return tl::make_unexpected(bucket_exec.error());
}

Result<CompleteMultipartUploadResponse> BaseClient::CompleteMultipartUpload(
    CompleteMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kPost, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("uploadId", args.upload_id);

  std::stringstream ss;
  ss << "<CompleteMultipartUpload>";
  for (auto& part : args.parts) {
    ss << "<Part>" << "<PartNumber>" << part.number << "</PartNumber>"
       << "<ETag>" << "\"" << part.etag << "\"" << "</ETag>";
    if (!part.checksum_crc64nvme.empty()) {
      ss << "<ChecksumCRC64NVME>" << part.checksum_crc64nvme
         << "</ChecksumCRC64NVME>";
    }
    ss << "</Part>";
  }
  ss << "</CompleteMultipartUpload>";
  std::string body = ss.str();
  req.body = body;

  utils::Multimap headers;
  headers.Add("Content-Type", "application/xml");
  headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.headers = headers;

  auto response = Execute(req);
  if (!response) {
    return tl::make_unexpected(response.error());
  }
  return CompleteMultipartUploadResponse::ParseXML(
      response->data, response->headers.GetFront("x-amz-version-id"));
}

Result<CreateMultipartUploadResponse> BaseClient::CreateMultipartUpload(
    CreateMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (!args.headers.Contains("Content-Type")) {
    args.headers.Add("Content-Type", "application/octet-stream");
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kPost, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("uploads", "");
  req.headers.AddAll(args.headers);

  if (auto resp = Execute(req)) {
    pugi::xml_document xdoc;
    pugi::xml_parse_result result = xdoc.load_string(resp->data.data());
    if (!result) {
      return error::make<CreateMultipartUploadResponse>("unable to parse XML");
    }
    auto text =
        xdoc.select_node("/InitiateMultipartUploadResult/UploadId/text()");
    return CreateMultipartUploadResponse(std::string(text.node().value()));
  } else {
    return tl::make_unexpected(resp.error());
  }
}

Result<DeleteBucketEncryptionResponse> BaseClient::DeleteBucketEncryption(
    DeleteBucketEncryptionArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("encryption", "");

  auto enc_resp = Execute(req);
  if (!enc_resp)
    return DeleteBucketEncryptionResponse();  // no encryption found
  return DeleteBucketEncryptionResponse(std::move(*enc_resp));
}

Result<DisableObjectLegalHoldResponse> BaseClient::DisableObjectLegalHold(
    DisableObjectLegalHoldArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }
  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::string body = "<LegalHold><Status>OFF</Status></LegalHold>";

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("legal-hold", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_disable = Execute(req);
  if (!exec_disable) return tl::make_unexpected(exec_disable.error());
  return DisableObjectLegalHoldResponse(std::move(*exec_disable));
}

Result<DeleteBucketLifecycleResponse> BaseClient::DeleteBucketLifecycle(
    DeleteBucketLifecycleArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("lifecycle", "");

  auto exec_life = Execute(req);
  if (!exec_life) return tl::make_unexpected(exec_life.error());
  return DeleteBucketLifecycleResponse(std::move(*exec_life));
}

Result<DeleteBucketNotificationResponse> BaseClient::DeleteBucketNotification(
    DeleteBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  NotificationConfig config;
  SetBucketNotificationArgs sbnargs(config);
  sbnargs.extra_headers = args.extra_headers;
  sbnargs.extra_query_params = args.extra_query_params;
  sbnargs.bucket = args.bucket;
  sbnargs.region = args.region;

  auto notif_resp = SetBucketNotification(sbnargs);
  if (!notif_resp) return tl::make_unexpected(notif_resp.error());
  return DeleteBucketNotificationResponse(std::move(*notif_resp));
}

Result<DeleteBucketPolicyResponse> BaseClient::DeleteBucketPolicy(
    DeleteBucketPolicyArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("policy", "");

  auto exec_set = Execute(req);
  if (!exec_set) return tl::make_unexpected(exec_set.error());
  return DeleteBucketPolicyResponse(std::move(*exec_set));
}

Result<DeleteBucketReplicationResponse> BaseClient::DeleteBucketReplication(
    DeleteBucketReplicationArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("replication", "");

  auto resp = Execute(req);
  if (!resp) {
    if (resp.error().String().find("ReplicationConfigurationNotFoundError") ==
        std::string::npos) {
      return tl::make_unexpected(resp.error());
    }
    return DeleteBucketReplicationResponse();
  }
  return DeleteBucketReplicationResponse(std::move(*resp));
}

Result<DeleteBucketTagsResponse> BaseClient::DeleteBucketTags(
    DeleteBucketTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("tagging", "");

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return DeleteBucketTagsResponse(std::move(*exec_set));
}

Result<DeleteObjectLockConfigResponse> BaseClient::DeleteObjectLockConfig(
    DeleteObjectLockConfigArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("object-lock", "");

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return DeleteObjectLockConfigResponse(std::move(*exec_set));
}

Result<DeleteObjectTagsResponse> BaseClient::DeleteObjectTags(
    DeleteObjectTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("tagging", "");

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return DeleteObjectTagsResponse(std::move(*exec_set));
}

Result<EnableObjectLegalHoldResponse> BaseClient::EnableObjectLegalHold(
    EnableObjectLegalHoldArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::string body = "<LegalHold><Status>ON</Status></LegalHold>";

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("legal-hold", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return EnableObjectLegalHoldResponse(std::move(*exec_set));
}

Result<GetBucketEncryptionResponse> BaseClient::GetBucketEncryption(
    GetBucketEncryptionArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("encryption", "");

  auto resp = Execute(req);
  if (resp) {
    return GetBucketEncryptionResponse::ParseXML(resp->data);
  }
  return tl::make_unexpected(resp.error());
}

Result<GetBucketLifecycleResponse> BaseClient::GetBucketLifecycle(
    GetBucketLifecycleArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("lifecycle", "");

  auto resp = Execute(req);

  if (!resp) {
    if (resp.error().String().find("NoSuchLifecycleConfiguration") !=
        std::string::npos) {
      return GetBucketLifecycleResponse(LifecycleConfig());
    }
    return tl::make_unexpected(resp.error());
  }

  return GetBucketLifecycleResponse::ParseXML(resp->data);
}

Result<GetBucketNotificationResponse> BaseClient::GetBucketNotification(
    GetBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("notification", "");

  auto resp = Execute(req);
  if (resp) {
    return GetBucketNotificationResponse::ParseXML(resp->data);
  }
  return tl::make_unexpected(resp.error());
}

Result<GetBucketPolicyResponse> BaseClient::GetBucketPolicy(
    GetBucketPolicyArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("policy", "");

  auto resp = Execute(req);
  if (resp) {
    return GetBucketPolicyResponse(resp->data);
  }
  return tl::make_unexpected(resp.error());
}

Result<GetBucketReplicationResponse> BaseClient::GetBucketReplication(
    GetBucketReplicationArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("replication", "");

  auto resp = Execute(req);
  if (resp) {
    return GetBucketReplicationResponse::ParseXML(resp->data);
  }
  return tl::make_unexpected(resp.error());
}

Result<GetBucketTagsResponse> BaseClient::GetBucketTags(
    GetBucketTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("tagging", "");

  auto resp = Execute(req);
  if (resp) {
    return GetBucketTagsResponse::ParseXML(resp->data);
  }
  return tl::make_unexpected(resp.error());
}

Result<GetBucketVersioningResponse> BaseClient::GetBucketVersioning(
    GetBucketVersioningArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("versioning", "");

  auto resp = Execute(req);
  if (!resp) {
    return tl::make_unexpected(resp.error());
  }
  GetBucketVersioningResponse response;

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp->data.data());
  if (!result) {
    return error::make<GetBucketVersioningResponse>("unable to parse XML");
  }

  auto root = xdoc.select_node("/VersioningConfiguration");

  pugi::xpath_node text;

  if (root.node().select_node("Status")) {
    text = root.node().select_node("Status/text()");
    response.status = (strcmp(text.node().value(), "Enabled") == 0);
  }
  if (root.node().select_node("MFADelete")) {
    text = root.node().select_node("MFADelete/text()");
    response.mfa_delete = (strcmp(text.node().value(), "Enabled") == 0);
  }

  return response;
}

Result<GetObjectResponse> BaseClient::GetObject(GetObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (args.ssec != nullptr && !base_url_.https) {
    return error::make<GetObjectResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.datafunc = args.datafunc;
  req.userdata = args.userdata;
  req.progressfunc = args.progressfunc;
  req.progress_userdata = args.progress_userdata;
  req.headers.AddAll(args.Headers());

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return GetObjectResponse(std::move(*exec_set));
}

Result<GetObjectLockConfigResponse> BaseClient::GetObjectLockConfig(
    GetObjectLockConfigArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("object-lock", "");

  auto resp = Execute(req);
  if (!resp) {
    return tl::make_unexpected(resp.error());
  }
  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp->data.data());
  if (!result) {
    return error::make<GetObjectLockConfigResponse>("unable to parse XML");
  }
  ObjectLockConfig config;

  auto rule = xdoc.select_node("/ObjectLockConfiguration/Rule");
  if (!rule) {
    return GetObjectLockConfigResponse(config);
  }
  auto text = rule.node().select_node("DefaultRetention/Mode/text()");
  config.retention_mode = StringToRetentionMode(text.node().value());

  if (rule.node().select_node("DefaultRetention/Days")) {
    text = rule.node().select_node("DefaultRetention/Days/text()");
    std::string value = text.node().value();
    config.retention_duration_days = Integer(std::stoi(value));
  }

  if (rule.node().select_node("DefaultRetention/Years")) {
    text = rule.node().select_node("DefaultRetention/Years/text()");
    std::string value = text.node().value();
    config.retention_duration_years = Integer(std::stoi(value));
  }

  return GetObjectLockConfigResponse(config);
}

Result<GetObjectRetentionResponse> BaseClient::GetObjectRetention(
    GetObjectRetentionArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("retention", "");

  GetObjectRetentionResponse response;

  auto resp = Execute(req);
  if (!resp) {
    if (resp.error().String().find("NoSuchObjectLockConfiguration") !=
        std::string::npos) {
      return response;
    }
    return tl::make_unexpected(resp.error());
  }

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp->data.data());
  if (!result) {
    return error::make<GetObjectRetentionResponse>("unable to parse XML");
  }

  auto text = xdoc.select_node("/Retention/Mode/text()");
  response.retention_mode = StringToRetentionMode(text.node().value());

  text = xdoc.select_node("/Retention/RetainUntilDate/text()");
  response.retain_until_date =
      utils::UtcTime::FromISO8601UTC(text.node().value());

  return response;
}

Result<GetObjectTagsResponse> BaseClient::GetObjectTags(
    GetObjectTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("tagging", "");

  auto resp = Execute(req);
  if (resp) {
    return GetObjectTagsResponse::ParseXML(resp->data);
  }
  return tl::make_unexpected(resp.error());
}

Result<GetPresignedObjectUrlResponse> BaseClient::GetPresignedObjectUrl(
    GetPresignedObjectUrlArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  utils::Multimap query_params;
  query_params.AddAll(args.extra_query_params);
  if (!args.version_id.empty()) query_params.Add("versionId", args.version_id);

  http::Url url;
  if (error::Error err = base_url_.BuildUrl(
          url, args.method, region, query_params, args.bucket, args.object)) {
    std::cerr << "failed to build url. error=" << err
              << ". This should not happen" << std::endl;
    std::terminate();
  }

  if (provider_ != nullptr) {
    creds::Credentials creds = provider_->Fetch();
    if (!creds.session_token.empty()) {
      query_params.Add("X-Amz-Security-Token", creds.session_token);
    }

    utils::UtcTime date = utils::UtcTime::Now();
    if (args.request_time) date = args.request_time;

    std::string host = url.HostHeaderValue();
    signer::PresignV4(args.method, host, url.path, region, query_params,
                      creds.access_key, creds.secret_key, date,
                      args.expiry_seconds);
    url.query_string = query_params.ToQueryString();
  }

  return GetPresignedObjectUrlResponse(url.String());
}

Result<GetPresignedPostFormDataResponse> BaseClient::GetPresignedPostFormData(
    PostPolicy policy) {
  if (!policy) {
    return tl::make_unexpected(error::Error("valid policy must be provided"));
  }

  if (provider_ == nullptr) {
    return tl::make_unexpected(error::Error(
        "Anonymous access does not require pre-signed post form-data"));
  }

  std::string region;
  auto get_resp = GetRegion(policy.bucket, policy.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  creds::Credentials creds = provider_->Fetch();
  std::map<std::string, std::string> data;
  if (error::Error err =
          policy.FormData(data, creds.access_key, creds.secret_key,
                          creds.session_token, region)) {
    return tl::make_unexpected(err);
  }
  return GetPresignedPostFormDataResponse(data);
}

Result<IsObjectLegalHoldEnabledResponse> BaseClient::IsObjectLegalHoldEnabled(
    IsObjectLegalHoldEnabledArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("legal-hold", "");

  auto resp = Execute(req);
  if (!resp) {
    if (resp.error().String().find("NoSuchObjectLockConfiguration") !=
        std::string::npos) {
      return IsObjectLegalHoldEnabledResponse(false);
    }
    return tl::make_unexpected(resp.error());
  }

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp->data.data());
  if (!result) {
    return error::make<IsObjectLegalHoldEnabledResponse>("unable to parse XML");
  }
  auto text = xdoc.select_node("/LegalHold/Status/text()");
  std::string value = text.node().value();
  return IsObjectLegalHoldEnabledResponse(value == "ON");
}

Result<ListBucketsResponse> BaseClient::ListBuckets(ListBucketsArgs args) {
  Request req(http::Method::kGet, base_url_.region, base_url_,
              args.extra_headers, args.extra_query_params);
  auto resp = Execute(req);
  if (!resp) {
    return tl::make_unexpected(resp.error());
  }
  return ListBucketsResponse::ParseXML(resp->data);
}

Result<ListBucketsResponse> BaseClient::ListBuckets() {
  return ListBuckets(ListBucketsArgs());
}

Result<ListenBucketNotificationResponse> BaseClient::ListenBucketNotification(
    ListenBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (!base_url_.aws_domain_suffix.empty()) {
    return error::make<ListenBucketNotificationResponse>(
        "ListenBucketNotification API is not supported in Amazon S3");
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req = Request(http::Method::kGet, region, base_url_,
                        args.extra_headers, args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("prefix", args.prefix);
  req.query_params.Add("suffix", args.suffix);
  if (args.events.size() > 0) {
    for (auto& event : args.events) req.query_params.Add("events", event);
  } else {
    req.query_params.Add("events", "s3:ObjectCreated:*");
    req.query_params.Add("events", "s3:ObjectRemoved:*");
    req.query_params.Add("events", "s3:ObjectAccessed:*");
  }

  std::string data;
  auto func = args.func;
  req.datafunc = [&func = func,
                  &data = data](http::DataFunctionArgs args) -> bool {
    data += args.datachunk;
    while (true) {
      size_t pos = data.find('\n');
      if (pos == std::string::npos) return true;
      std::string line = data.substr(0, pos);
      data.erase(0, pos + 1);
      line = utils::Trim(line);
      if (line.empty()) continue;

      nlohmann::json json = nlohmann::json::parse(line);
      if (!json.contains("Records")) continue;

      nlohmann::json j_records = json["Records"];
      std::list<NotificationRecord> records;
      for (auto& j_record : j_records) {
        records.push_back(NotificationRecord::ParseJSON(j_record));
      }

      if (records.size() <= 0) continue;

      if (!func(records)) {
        return false;
      }
    }
  };

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return ListenBucketNotificationResponse(std::move(*exec_set));
}

Result<ListObjectsResponse> BaseClient::ListObjectsV1(ListObjectsV1Args args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.marker.empty()) {
    req.query_params.Add("marker", args.marker);
  }
  auto resp = Execute(req);
  if (!resp) {
    return tl::make_unexpected(resp.error());
  }
  return ListObjectsResponse::ParseXML(resp->data, false);
}

Result<ListObjectsResponse> BaseClient::ListObjectsV2(ListObjectsV2Args args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("list-type", "2");
  req.query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.continuation_token.empty()) {
    req.query_params.Add("continuation-token", args.continuation_token);
  }
  if (args.fetch_owner) {
    req.query_params.Add("fetch-owner", "true");
  }
  if (!args.start_after.empty()) {
    req.query_params.Add("start-after", args.start_after);
  }
  if (args.include_user_metadata) {
    req.query_params.Add("metadata", "true");
  }
  auto resp = Execute(req);
  if (!resp) {
    return tl::make_unexpected(resp.error());
  }
  return ListObjectsResponse::ParseXML(resp->data, false);
}

Result<ListObjectsResponse> BaseClient::ListObjectVersions(
    ListObjectVersionsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("versions", "");
  req.query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.key_marker.empty()) {
    req.query_params.Add("key-marker", args.key_marker);
  }
  if (!args.version_id_marker.empty()) {
    req.query_params.Add("version-id-marker", args.version_id_marker);
  }

  auto resp = Execute(req);
  if (!resp) {
    return tl::make_unexpected(resp.error());
  }
  return ListObjectsResponse::ParseXML(resp->data, true);
}

Result<MakeBucketResponse> BaseClient::MakeBucket(MakeBucketArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region = args.region;
  std::string base_region = base_url_.region;
  if (!base_region.empty() && !region.empty() && base_region != region) {
    return error::make<MakeBucketResponse>("region must be " + base_region +
                                           ", but passed " + region);
  }

  if (region.empty()) {
    region = base_region;
  }
  if (region.empty()) {
    region = "us-east-1";
  }
  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  if (args.object_lock) {
    req.headers.Add("x-amz-bucket-object-lock-enabled", "true");
  }

  std::string body;
  if (region != "us-east-1") {
    std::stringstream ss;
    ss << "<CreateBucketConfiguration>" << "<LocationConstraint>" << region
       << "</LocationConstraint>" << "</CreateBucketConfiguration>";
    body = ss.str();
    req.body = body;
  }

  auto resp = Execute(req);
  if (resp) {
    std::unique_lock<std::shared_mutex> lock(region_map_mutex_);
    region_map_[args.bucket] = region;
    return MakeBucketResponse(std::move(*resp));
  }
  return tl::make_unexpected(resp.error());
}

Result<PutObjectResponse> BaseClient::PutObject(PutObjectApiArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

#ifdef MINIO_CPP_RDMA
  if (args.rdmaclient != nullptr && args.rdmaclient->isConnected()) {
    s3_rdma_client_ctx putCtx = {
        .provider = provider_,
        .bucket = args.bucket,
        .object = args.object,
        .url = base_url_,
        .region = region,
        .op = CUOBJ_PUT,
    };

    ssize_t ret =
        rdmaPutWithRetry(args.rdmaclient, &putCtx, args.buf, args.size);
    if (ret > 0) {
      PutObjectResponse resp;
      resp.etag = putCtx.etag;
      resp.checksum_crc64nvme = putCtx.checksum;
      return resp;
    }
    // ret < 0 (retries exhausted) or kRDMANotSupported (server declined):
    // fall through to HTTP path.
  }
#endif

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.AddAll(args.query_params);
  req.headers.AddAll(args.headers);
  req.body = args.data;
  req.progressfunc = args.progressfunc;
  req.progress_userdata = args.progress_userdata;

  auto response = Execute(req);
  if (!response) {
    return tl::make_unexpected(response.error());
  }

  PutObjectResponse resp;
  resp.etag = utils::Trim(response->headers.GetFront("etag"), '"');
  resp.version_id = response->headers.GetFront("x-amz-version-id");
  resp.checksumCRC32 = response->headers.GetFront("x-amz-checksum-crc32");
  resp.checksumCRC32C = response->headers.GetFront("x-amz-checksum-crc32c");
  resp.checksumSHA1 = response->headers.GetFront("x-amz-checksum-sha1");
  resp.checksumSHA256 = response->headers.GetFront("x-amz-checksum-sha256");

  return resp;
}

Result<RemoveBucketResponse> BaseClient::RemoveBucket(RemoveBucketArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return RemoveBucketResponse(std::move(*exec_set));
}

Result<RemoveObjectResponse> BaseClient::RemoveObject(RemoveObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return RemoveObjectResponse(std::move(*exec_set));
}

Result<RemoveObjectsResponse> BaseClient::RemoveObjects(
    RemoveObjectsApiArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kPost, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("delete", "");
  if (args.bypass_governance_mode) {
    req.headers.Add("x-amz-bypass-governance-retention", "true");
  }

  std::stringstream ss;
  ss << "<Delete>";
  if (args.quiet) ss << "<Quiet>true</Quiet>";
  for (auto& object : args.objects) {
    ss << "<Object>";
    ss << "<Key>" << utils::XMLEncode(object.name) << "</Key>";
    if (!object.version_id.empty()) {
      ss << "<VersionId>" << object.version_id << "</VersionId>";
    }
    ss << "</Object>";
  }
  ss << "</Delete>";
  std::string body = ss.str();
  req.body = body;
  req.headers.Add("Content-Type", "application/xml");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));

  auto response = Execute(req);
  if (!response) {
    return tl::make_unexpected(response.error());
  }
  return RemoveObjectsResponse::ParseXML(response->data);
}

Result<SelectObjectContentResponse> BaseClient::SelectObjectContent(
    SelectObjectContentArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (args.ssec != nullptr && !base_url_.https) {
    return error::make<SelectObjectContentResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kPost, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("select", "");
  req.query_params.Add("select-type", "2");
  std::string body = args.request.ToXML();
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = body;

  SelectHandler handler(args.resultfunc);
  using namespace std::placeholders;
  req.datafunc = std::bind(&SelectHandler::DataFunction, &handler, _1);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SelectObjectContentResponse(std::move(*exec_set));
}

Result<SetBucketEncryptionResponse> BaseClient::SetBucketEncryption(
    SetBucketEncryptionArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::stringstream ss;
  ss << "<ServerSideEncryptionConfiguration>";
  ss << "<Rule><ApplyServerSideEncryptionByDefault>";
  ss << "<SSEAlgorithm>" << args.config.sse_algorithm << "</SSEAlgorithm>";
  if (!args.config.kms_master_key_id.empty()) {
    ss << "<KMSMasterKeyID>" << args.config.kms_master_key_id
       << "</KMSMasterKeyID>";
  }
  ss << "</ApplyServerSideEncryptionByDefault></Rule>";
  ss << "</ServerSideEncryptionConfiguration>";
  std::string body = ss.str();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("encryption", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetBucketEncryptionResponse(std::move(*exec_set));
}

Result<SetBucketLifecycleResponse> BaseClient::SetBucketLifecycle(
    SetBucketLifecycleArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::string body = args.config.ToXML();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("lifecycle", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetBucketLifecycleResponse(std::move(*exec_set));
}

Result<SetBucketNotificationResponse> BaseClient::SetBucketNotification(
    SetBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::string body = args.config.ToXML();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("notification", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetBucketNotificationResponse(std::move(*exec_set));
}

Result<SetBucketPolicyResponse> BaseClient::SetBucketPolicy(
    SetBucketPolicyArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("policy", "");
  req.body = args.policy;
  req.headers.Add("Content-MD5", utils::Md5sumHash(args.policy));

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetBucketPolicyResponse(std::move(*exec_set));
}

Result<SetBucketReplicationResponse> BaseClient::SetBucketReplication(
    SetBucketReplicationArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::string body = args.config.ToXML();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("replication", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetBucketReplicationResponse(std::move(*exec_set));
}

Result<SetBucketTagsResponse> BaseClient::SetBucketTags(
    SetBucketTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::stringstream ss;
  ss << "<Tagging>";
  if (!args.tags.empty()) {
    ss << "<TagSet>";
    for (auto& [key, value] : args.tags) {
      ss << "<Tag>" << "<Key>" << key << "</Key>" << "<Value>" << value
         << "</Value>" << "</Tag>";
    }
    ss << "</TagSet>";
  }
  ss << "</Tagging>";

  std::string body = ss.str();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("tagging", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetBucketTagsResponse(std::move(*exec_set));
}

Result<SetBucketVersioningResponse> BaseClient::SetBucketVersioning(
    SetBucketVersioningArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::stringstream ss;
  ss << "<VersioningConfiguration>";
  if (args.status) {
    ss << "<Status>" << (args.status.Get() ? "Enabled" : "Suspended")
       << "</Status>";
  }
  if (args.mfa_delete) {
    ss << "<MFADelete>" << (args.mfa_delete.Get() ? "Enabled" : "Disabled")
       << "</MFADelete>";
  }
  ss << "</VersioningConfiguration>";
  std::string body = ss.str();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("versioning", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetBucketVersioningResponse(std::move(*exec_set));
}

Result<SetObjectLockConfigResponse> BaseClient::SetObjectLockConfig(
    SetObjectLockConfigArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::stringstream ss;
  ss << "<ObjectLockConfiguration>";
  ss << "<ObjectLockEnabled>Enabled</ObjectLockEnabled>";
  if (IsRetentionModeValid(args.config.retention_mode)) {
    ss << "<Rule><DefaultRetention>";
    ss << "<Mode>" << RetentionModeToString(args.config.retention_mode)
       << "</Mode>";
    if (args.config.retention_duration_days) {
      ss << "<Days>"
         << std::to_string(args.config.retention_duration_days.Get())
         << "</Days>";
    }
    if (args.config.retention_duration_years) {
      ss << "<Years>"
         << std::to_string(args.config.retention_duration_years.Get())
         << "</Years>";
    }
    ss << "</DefaultRetention></Rule>";
  }
  ss << "</ObjectLockConfiguration>";

  std::string body = ss.str();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("object-lock", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetObjectLockConfigResponse(std::move(*exec_set));
}

Result<SetObjectRetentionResponse> BaseClient::SetObjectRetention(
    SetObjectRetentionArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::stringstream ss;
  ss << "<Retention>" << "<Mode>" << RetentionModeToString(args.retention_mode)
     << "</Mode>" << "<RetainUntilDate>"
     << args.retain_until_date.ToISO8601UTC() << "</RetainUntilDate>"
     << "</Retention>";

  std::string body = ss.str();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("retention", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetObjectRetentionResponse(std::move(*exec_set));
}

Result<SetObjectTagsResponse> BaseClient::SetObjectTags(
    SetObjectTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  std::stringstream ss;
  ss << "<Tagging>";
  if (!args.tags.empty()) {
    ss << "<TagSet>";
    for (auto& [key, value] : args.tags) {
      ss << "<Tag>" << "<Key>" << key << "</Key>" << "<Value>" << value
         << "</Value>" << "</Tag>";
    }
    ss << "</TagSet>";
  }
  ss << "</Tagging>";

  std::string body = ss.str();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("tagging", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  auto exec_set = Execute(req);

  if (!exec_set) return tl::make_unexpected(exec_set.error());

  return SetObjectTagsResponse(std::move(*exec_set));
}

Result<StatObjectResponse> BaseClient::StatObject(StatObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (args.ssec != nullptr && !base_url_.https) {
    return error::make<StatObjectResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kHead, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.headers.AddAll(args.Headers());

  auto response = Execute(req);
  if (!response) {
    return tl::make_unexpected(response.error());
  }
  StatObjectResponse resp(std::move(*response));
  resp.bucket_name = args.bucket;
  resp.object_name = args.object;
  resp.version_id = response->headers.GetFront("x-amz-version-id");

  resp.etag = utils::Trim(response->headers.GetFront("etag"), '"');

  std::string value = response->headers.GetFront("content-length");
  if (!value.empty()) resp.size = std::stol(value);

  value = response->headers.GetFront("last-modified");
  if (!value.empty()) {
    resp.last_modified = utils::UtcTime::FromHttpHeaderValue(value.c_str());
  }

  value = response->headers.GetFront("x-amz-object-lock-mode");
  if (!value.empty()) resp.retention_mode = StringToRetentionMode(value);

  value = response->headers.GetFront("x-amz-object-lock-retain-until-date");
  if (!value.empty()) {
    resp.retention_retain_until_date =
        utils::UtcTime::FromISO8601UTC(value.c_str());
  }

  value = response->headers.GetFront("x-amz-object-lock-legal-hold");
  if (!value.empty()) resp.legal_hold = StringToLegalHold(value);

  value = response->headers.GetFront("x-amz-delete-marker");
  if (!value.empty()) resp.delete_marker = utils::StringToBool(value);

  utils::Multimap user_metadata;
  std::list<std::string> keys = response->headers.Keys();
  for (auto key : keys) {
    if (utils::StartsWith(key, "x-amz-meta-")) {
      std::list<std::string> values = response->headers.Get(key);
      key.erase(0, 11);
      for (auto value : values) user_metadata.Add(key, value);
    }
  }
  resp.user_metadata = user_metadata;

  return resp;
}

Result<UploadPartResponse> BaseClient::UploadPart(UploadPartArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

#ifdef MINIO_CPP_RDMA
  if (args.rdmaclient != nullptr && args.rdmaclient->isConnected()) {
    std::string region;
    auto get_resp = GetRegion(args.bucket, args.region);
    if (get_resp) {
      region = get_resp->region;
    } else {
      return tl::make_unexpected(get_resp.error());
    }

    s3_rdma_client_ctx putCtx = {
        .provider = provider_,
        .bucket = args.bucket,
        .object = args.object,
        .uploadId = args.upload_id,
        .partNumber = args.part_number,
        .url = base_url_,
        .region = region,
        .op = CUOBJ_PUT,
        .checksum = args.checksum_crc64nvme,
    };

    ssize_t ret =
        rdmaPutWithRetry(args.rdmaclient, &putCtx, args.buf, args.part_size);
    if (ret > 0) {
      UploadPartResponse resp;
      resp.etag = putCtx.etag;
      resp.checksum_crc64nvme = putCtx.checksum;
      return resp;
    }
    // ret < 0 (retries exhausted) or kRDMANotSupported (server declined):
    // fall through to HTTP upload part path.
  }
#endif

  utils::Multimap query_params;
  query_params.Add("partNumber", std::to_string(args.part_number));
  query_params.Add("uploadId", args.upload_id);

  PutObjectApiArgs api_args;
  api_args.extra_headers = args.extra_headers;
  api_args.extra_query_params = args.extra_query_params;
  api_args.bucket = args.bucket;
  api_args.region = args.region;
  api_args.object = args.object;
  api_args.data = args.data;
  api_args.headers = args.headers;
  api_args.progressfunc = args.progressfunc;
  api_args.progress_userdata = args.progress_userdata;
  api_args.query_params = query_params;

  auto put_obj = PutObject(api_args);
  if (!put_obj) return tl::make_unexpected(put_obj.error());
  return UploadPartResponse(std::move(*put_obj));
}

Result<UploadPartCopyResponse> BaseClient::UploadPartCopy(
    UploadPartCopyArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::string region;
  auto get_resp = GetRegion(args.bucket, args.region);
  if (get_resp) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.AddAll(args.extra_query_params);
  req.query_params.Add("partNumber", std::to_string(args.part_number));
  req.query_params.Add("uploadId", args.upload_id);
  req.headers.AddAll(args.headers);

  auto response = Execute(req);
  if (!response) {
    return tl::make_unexpected(response.error());
  }
  UploadPartCopyResponse resp;
  resp.etag = utils::Trim(response->headers.GetFront("etag"), '"');

  return resp;
}

// ---- Async overloads ----

std::future<Result<AbortMultipartUploadResponse>>
BaseClient::AbortMultipartUploadAsync(AbortMultipartUploadArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return AbortMultipartUpload(std::move(args));
                    });
}

std::future<Result<BucketExistsResponse>> BaseClient::BucketExistsAsync(
    BucketExistsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return BucketExists(std::move(args));
                    });
}

std::future<Result<CompleteMultipartUploadResponse>>
BaseClient::CompleteMultipartUploadAsync(CompleteMultipartUploadArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return CompleteMultipartUpload(std::move(args));
                    });
}

std::future<Result<CreateMultipartUploadResponse>>
BaseClient::CreateMultipartUploadAsync(CreateMultipartUploadArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return CreateMultipartUpload(std::move(args));
                    });
}

std::future<Result<DeleteBucketEncryptionResponse>>
BaseClient::DeleteBucketEncryptionAsync(DeleteBucketEncryptionArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteBucketEncryption(std::move(args));
                    });
}

std::future<Result<DeleteBucketLifecycleResponse>>
BaseClient::DeleteBucketLifecycleAsync(DeleteBucketLifecycleArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteBucketLifecycle(std::move(args));
                    });
}

std::future<Result<DeleteBucketNotificationResponse>>
BaseClient::DeleteBucketNotificationAsync(DeleteBucketNotificationArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteBucketNotification(std::move(args));
                    });
}

std::future<Result<DeleteBucketPolicyResponse>>
BaseClient::DeleteBucketPolicyAsync(DeleteBucketPolicyArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteBucketPolicy(std::move(args));
                    });
}

std::future<Result<DeleteBucketReplicationResponse>>
BaseClient::DeleteBucketReplicationAsync(DeleteBucketReplicationArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteBucketReplication(std::move(args));
                    });
}

std::future<Result<DeleteBucketTagsResponse>> BaseClient::DeleteBucketTagsAsync(
    DeleteBucketTagsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteBucketTags(std::move(args));
                    });
}

std::future<Result<DeleteObjectLockConfigResponse>>
BaseClient::DeleteObjectLockConfigAsync(DeleteObjectLockConfigArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteObjectLockConfig(std::move(args));
                    });
}

std::future<Result<DeleteObjectTagsResponse>> BaseClient::DeleteObjectTagsAsync(
    DeleteObjectTagsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DeleteObjectTags(std::move(args));
                    });
}

std::future<Result<DisableObjectLegalHoldResponse>>
BaseClient::DisableObjectLegalHoldAsync(DisableObjectLegalHoldArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DisableObjectLegalHold(std::move(args));
                    });
}

std::future<Result<EnableObjectLegalHoldResponse>>
BaseClient::EnableObjectLegalHoldAsync(EnableObjectLegalHoldArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return EnableObjectLegalHold(std::move(args));
                    });
}

std::future<Result<GetBucketEncryptionResponse>>
BaseClient::GetBucketEncryptionAsync(GetBucketEncryptionArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetBucketEncryption(std::move(args));
                    });
}

std::future<Result<GetBucketLifecycleResponse>>
BaseClient::GetBucketLifecycleAsync(GetBucketLifecycleArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetBucketLifecycle(std::move(args));
                    });
}

std::future<Result<GetBucketNotificationResponse>>
BaseClient::GetBucketNotificationAsync(GetBucketNotificationArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetBucketNotification(std::move(args));
                    });
}

std::future<Result<GetBucketPolicyResponse>> BaseClient::GetBucketPolicyAsync(
    GetBucketPolicyArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetBucketPolicy(std::move(args));
                    });
}

std::future<Result<GetBucketReplicationResponse>>
BaseClient::GetBucketReplicationAsync(GetBucketReplicationArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetBucketReplication(std::move(args));
                    });
}

std::future<Result<GetBucketTagsResponse>> BaseClient::GetBucketTagsAsync(
    GetBucketTagsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetBucketTags(std::move(args));
                    });
}

std::future<Result<GetBucketVersioningResponse>>
BaseClient::GetBucketVersioningAsync(GetBucketVersioningArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetBucketVersioning(std::move(args));
                    });
}

std::future<Result<GetObjectResponse>> BaseClient::GetObjectAsync(
    GetObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetObject(std::move(args));
                    });
}

std::future<Result<GetObjectLockConfigResponse>>
BaseClient::GetObjectLockConfigAsync(GetObjectLockConfigArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetObjectLockConfig(std::move(args));
                    });
}

std::future<Result<GetObjectRetentionResponse>>
BaseClient::GetObjectRetentionAsync(GetObjectRetentionArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetObjectRetention(std::move(args));
                    });
}

std::future<Result<GetObjectTagsResponse>> BaseClient::GetObjectTagsAsync(
    GetObjectTagsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetObjectTags(std::move(args));
                    });
}

std::future<Result<GetPresignedObjectUrlResponse>>
BaseClient::GetPresignedObjectUrlAsync(GetPresignedObjectUrlArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return GetPresignedObjectUrl(std::move(args));
                    });
}

std::future<Result<GetPresignedPostFormDataResponse>>
BaseClient::GetPresignedPostFormDataAsync(PostPolicy policy) {
  return std::async(std::launch::async,
                    [this, policy = std::move(policy)]() mutable {
                      return GetPresignedPostFormData(std::move(policy));
                    });
}

std::future<Result<IsObjectLegalHoldEnabledResponse>>
BaseClient::IsObjectLegalHoldEnabledAsync(IsObjectLegalHoldEnabledArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return IsObjectLegalHoldEnabled(std::move(args));
                    });
}

std::future<Result<ListBucketsResponse>> BaseClient::ListBucketsAsync(
    ListBucketsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return ListBuckets(std::move(args));
                    });
}

std::future<Result<ListBucketsResponse>> BaseClient::ListBucketsAsync() {
  return std::async(std::launch::async, [this]() { return ListBuckets(); });
}

std::future<Result<ListenBucketNotificationResponse>>
BaseClient::ListenBucketNotificationAsync(ListenBucketNotificationArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return ListenBucketNotification(std::move(args));
                    });
}

std::future<Result<ListObjectsResponse>> BaseClient::ListObjectsV1Async(
    ListObjectsV1Args args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return ListObjectsV1(std::move(args));
                    });
}

std::future<Result<ListObjectsResponse>> BaseClient::ListObjectsV2Async(
    ListObjectsV2Args args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return ListObjectsV2(std::move(args));
                    });
}

std::future<Result<ListObjectsResponse>> BaseClient::ListObjectVersionsAsync(
    ListObjectVersionsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return ListObjectVersions(std::move(args));
                    });
}

std::future<Result<MakeBucketResponse>> BaseClient::MakeBucketAsync(
    MakeBucketArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return MakeBucket(std::move(args));
                    });
}

std::future<Result<PutObjectResponse>> BaseClient::PutObjectAsync(
    PutObjectApiArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return BaseClient::PutObject(std::move(args));
                    });
}

std::future<Result<RemoveBucketResponse>> BaseClient::RemoveBucketAsync(
    RemoveBucketArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return RemoveBucket(std::move(args));
                    });
}

std::future<Result<RemoveObjectResponse>> BaseClient::RemoveObjectAsync(
    RemoveObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return RemoveObject(std::move(args));
                    });
}

std::future<Result<RemoveObjectsResponse>> BaseClient::RemoveObjectsAsync(
    RemoveObjectsApiArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return BaseClient::RemoveObjects(std::move(args));
                    });
}

// SelectObjectContentArgs owns request by value (deep-copied via
// SelectRequest's shared_ptr chain). Direct move is safe.
std::future<Result<SelectObjectContentResponse>>
BaseClient::SelectObjectContentAsync(SelectObjectContentArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SelectObjectContent(std::move(args));
                    });
}

// SetBucketEncryptionArgs owns config by value.
std::future<Result<SetBucketEncryptionResponse>>
BaseClient::SetBucketEncryptionAsync(SetBucketEncryptionArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetBucketEncryption(std::move(args));
                    });
}

// SetBucketLifecycleArgs owns config by value.
std::future<Result<SetBucketLifecycleResponse>>
BaseClient::SetBucketLifecycleAsync(SetBucketLifecycleArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetBucketLifecycle(std::move(args));
                    });
}

// SetBucketNotificationArgs owns config by value.
// config.
std::future<Result<SetBucketNotificationResponse>>
BaseClient::SetBucketNotificationAsync(SetBucketNotificationArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetBucketNotification(std::move(args));
                    });
}

// SetBucketReplicationArgs owns config by value.
std::future<Result<SetBucketReplicationResponse>>
BaseClient::SetBucketReplicationAsync(SetBucketReplicationArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetBucketReplication(std::move(args));
                    });
}

std::future<Result<SetBucketPolicyResponse>> BaseClient::SetBucketPolicyAsync(
    SetBucketPolicyArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetBucketPolicy(std::move(args));
                    });
}

std::future<Result<SetBucketTagsResponse>> BaseClient::SetBucketTagsAsync(
    SetBucketTagsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetBucketTags(std::move(args));
                    });
}

std::future<Result<SetBucketVersioningResponse>>
BaseClient::SetBucketVersioningAsync(SetBucketVersioningArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetBucketVersioning(std::move(args));
                    });
}

std::future<Result<SetObjectLockConfigResponse>>
BaseClient::SetObjectLockConfigAsync(SetObjectLockConfigArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetObjectLockConfig(std::move(args));
                    });
}

std::future<Result<SetObjectRetentionResponse>>
BaseClient::SetObjectRetentionAsync(SetObjectRetentionArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetObjectRetention(std::move(args));
                    });
}

std::future<Result<SetObjectTagsResponse>> BaseClient::SetObjectTagsAsync(
    SetObjectTagsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return SetObjectTags(std::move(args));
                    });
}

std::future<Result<StatObjectResponse>> BaseClient::StatObjectAsync(
    StatObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return StatObject(std::move(args));
                    });
}

std::future<Result<UploadPartResponse>> BaseClient::UploadPartAsync(
    UploadPartArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return UploadPart(std::move(args));
                    });
}

std::future<Result<UploadPartCopyResponse>> BaseClient::UploadPartCopyAsync(
    UploadPartCopyArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return UploadPartCopy(std::move(args));
                    });
}

}  // namespace minio::s3
