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

#include "baseclient.h"

#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <nlohmann/json.hpp>
#include <ostream>
#include <pugixml.hpp>
#include <sstream>
#include <string>
#include <type_traits>

#include "args.h"
#include "config.h"
#include "credentials.h"
#include "error.h"
#include "http.h"
#include "providers.h"
#include "request.h"
#include "response.h"
#include "select.h"
#include "signer.h"
#include "types.h"
#include "utils.h"

minio::utils::Multimap minio::s3::GetCommonListObjectsQueryParams(
    const std::string& delimiter, const std::string& encoding_type,
    unsigned int max_keys, const std::string& prefix) {
  utils::Multimap query_params;
  query_params.Add("delimiter", delimiter);
  query_params.Add("max-keys", std::to_string(max_keys > 0 ? max_keys : 1000));
  query_params.Add("prefix", prefix);
  if (!encoding_type.empty()) query_params.Add("encoding-type", encoding_type);
  return query_params;
}

minio::s3::BaseClient::BaseClient(BaseUrl base_url, creds::Provider* provider)
    : base_url_(std::move(base_url)), provider_(provider) {
  if (!base_url_) {
    std::cerr << "valid base url must be provided; " << base_url_.Error()
              << std::endl;
    std::terminate();
  }
}

minio::error::Error minio::s3::BaseClient::SetAppInfo(
    std::string_view app_name, std::string_view app_version) {
  if (app_name.empty() || app_version.empty()) {
    return error::Error("Application name/version cannot be empty");
  }

  user_agent_ = std::string(DEFAULT_USER_AGENT) + " " + std::string(app_name) +
                "/" + std::string(app_version);
  return error::SUCCESS;
}

void minio::s3::BaseClient::HandleRedirectResponse(
    std::string& code, std::string& message, int status_code,
    http::Method method, const utils::Multimap& headers,
    const std::string& bucket_name, bool retry) {
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
      !bucket_name.empty() && !region_map_[bucket_name].empty()) {
    code = "RetryHead";
    message.clear();
  }
}

minio::s3::Response minio::s3::BaseClient::GetErrorResponse(
    http::Response resp, std::string_view resource, http::Method method,
    const std::string& bucket_name, const std::string& object_name) {
  if (!resp.error.empty()) {
    return error::make<Response>(resp.error);
  }

  if (!resp.body.empty()) {
    std::list<std::string> values = resp.headers.Get("Content-Type");
    for (auto& value : values) {
      if (utils::Contains(utils::ToLower(value), "application/xml")) {
        return Response::ParseXML(resp.body, resp.status_code, resp.headers);
      }
    }

    auto response =
        error::make<Response>("invalid response received; status code: " +
                              std::to_string(resp.status_code) +
                              "; content-type: " + utils::Join(values, ","));
    response.status_code = resp.status_code;
    response.headers = resp.headers;
    return response;
  }

  Response response;
  response.status_code = resp.status_code;
  response.headers = resp.headers;

  switch (resp.status_code) {
    case 301:
    case 307:
    case 400:
      HandleRedirectResponse(response.code, response.message, resp.status_code,
                             method, resp.headers, bucket_name, true);
      break;
    case 403:
      response.code = "AccessDenied";
      response.message = "Access denied";
      break;
    case 404:
      if (!object_name.empty()) {
        response.code = "NoSuchKey";
        response.message = "Object does not exist";
      } else if (bucket_name.empty()) {
        response.code = "NoSuchBucket";
        response.message = "Bucket does not exist";
      } else {
        response.code = "ResourceNotFound";
        response.message = "Request resource not found";
      }
      break;
    case 405:
      response.code = "MethodNotAllowed";
      response.message =
          "The specified method is not allowed against this resource";
      break;
    case 409:
      if (bucket_name.empty()) {
        response.code = "NoSuchBucket";
        response.message = "Bucket does not exist";
      } else {
        response.code = "ResourceConflict";
        response.message = "Request resource conflicts";
      }
      break;
    case 501:
      response.code = "MethodNotAllowed";
      response.message =
          "The specified method is not allowed against this resource";
      break;
    default: {
      auto response =
          error::make<Response>("server failed with HTTP status code " +
                                std::to_string(resp.status_code));
      response.status_code = resp.status_code;
      response.headers = resp.headers;
      return response;
    }
  }

  response.resource = resource;
  response.request_id = response.headers.GetFront("x-amz-request-id");
  response.host_id = response.headers.GetFront("x-amz-id-2");
  response.bucket_name = bucket_name;
  response.object_name = object_name;

  return response;
}

minio::s3::Response minio::s3::BaseClient::execute(Request& req) {
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

  Response resp = GetErrorResponse(response, request.url.path, req.method,
                                   req.bucket_name, req.object_name);
  if (resp.code == "NoSuchBucket" || resp.code == "RetryHead") {
    region_map_.erase(req.bucket_name);
  }

  return resp;
}

minio::s3::Response minio::s3::BaseClient::Execute(Request& req) {
  Response resp = execute(req);
  if (resp || resp.code != "RetryHead") return resp;

  // Retry only once on RetryHead error.
  resp = execute(req);
  if (resp || resp.code != "RetryHead") return resp;

  std::string code;
  std::string message;
  HandleRedirectResponse(code, message, resp.status_code, req.method,
                         resp.headers, req.bucket_name);
  resp.code = code;
  resp.message = message;

  return resp;
}

minio::s3::GetRegionResponse minio::s3::BaseClient::GetRegion(
    const std::string& bucket_name, const std::string& region) {
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

  std::string stored_region = region_map_[bucket_name];
  if (!stored_region.empty()) {
    return GetRegionResponse(stored_region);
  }
  Request req(http::Method::kGet, "us-east-1", base_url_, utils::Multimap(),
              utils::Multimap());
  req.query_params.Add("location", "");
  req.bucket_name = bucket_name;

  Response resp = Execute(req);
  if (!resp) {
    return GetRegionResponse(resp);
  }

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp.data.data());
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

  region_map_[bucket_name] = value;

  return GetRegionResponse(value);
}

minio::s3::AbortMultipartUploadResponse
minio::s3::BaseClient::AbortMultipartUpload(AbortMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) {
    return AbortMultipartUploadResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return AbortMultipartUploadResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("uploadId", args.upload_id);

  return AbortMultipartUploadResponse(Execute(req));
}

minio::s3::BucketExistsResponse minio::s3::BaseClient::BucketExists(
    BucketExistsArgs args) {
  if (error::Error err = args.Validate()) {
    return BucketExistsResponse(err);
  }
  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return (resp.code == "NoSuchBucket") ? BucketExistsResponse(false)
                                         : BucketExistsResponse(resp);
  }

  Request req(http::Method::kHead, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  if (Response resp = Execute(req)) {
    return BucketExistsResponse(true);
  } else {
    return (resp.code == "NoSuchBucket") ? BucketExistsResponse(false)
                                         : BucketExistsResponse(resp);
  }
}

minio::s3::CompleteMultipartUploadResponse
minio::s3::BaseClient::CompleteMultipartUpload(
    CompleteMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) {
    return CompleteMultipartUploadResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return CompleteMultipartUploadResponse(resp);
  }

  Request req(http::Method::kPost, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("uploadId", args.upload_id);

  std::stringstream ss;
  ss << "<CompleteMultipartUpload>";
  for (auto& part : args.parts) {
    ss << "<Part>"
       << "<PartNumber>" << part.number << "</PartNumber>"
       << "<ETag>"
       << "\"" << part.etag << "\""
       << "</ETag>"
       << "</Part>";
  }
  ss << "</CompleteMultipartUpload>";
  std::string body = ss.str();
  req.body = body;

  utils::Multimap headers;
  headers.Add("Content-Type", "application/xml");
  headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.headers = headers;

  Response response = Execute(req);
  if (!response) {
    return CompleteMultipartUploadResponse(response);
  }
  return CompleteMultipartUploadResponse::ParseXML(
      response.data, response.headers.GetFront("x-amz-version-id"));
}

minio::s3::CreateMultipartUploadResponse
minio::s3::BaseClient::CreateMultipartUpload(CreateMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) {
    return CreateMultipartUploadResponse(err);
  }

  if (!args.headers.Contains("Content-Type")) {
    args.headers.Add("Content-Type", "application/octet-stream");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return CreateMultipartUploadResponse(resp);
  }

  Request req(http::Method::kPost, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("uploads", "");
  req.headers.AddAll(args.headers);

  if (Response resp = Execute(req)) {
    pugi::xml_document xdoc;
    pugi::xml_parse_result result = xdoc.load_string(resp.data.data());
    if (!result) {
      return error::make<CreateMultipartUploadResponse>("unable to parse XML");
    }
    auto text =
        xdoc.select_node("/InitiateMultipartUploadResult/UploadId/text()");
    return CreateMultipartUploadResponse(std::string(text.node().value()));
  } else {
    return CreateMultipartUploadResponse(resp);
  }
}

minio::s3::DeleteBucketEncryptionResponse
minio::s3::BaseClient::DeleteBucketEncryption(DeleteBucketEncryptionArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteBucketEncryptionResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DeleteBucketEncryptionResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("encryption", "");

  Response resp = Execute(req);
  if (resp) {
    return DeleteBucketEncryptionResponse(resp);
  }
  if (resp.code != "ServerSideEncryptionConfigurationNotFoundError") {
    return DeleteBucketEncryptionResponse(resp);
  }
  return DeleteBucketEncryptionResponse();
}

minio::s3::DisableObjectLegalHoldResponse
minio::s3::BaseClient::DisableObjectLegalHold(DisableObjectLegalHoldArgs args) {
  if (error::Error err = args.Validate()) {
    return DisableObjectLegalHoldResponse(err);
  }
  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DisableObjectLegalHoldResponse(resp);
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

  return DisableObjectLegalHoldResponse(Execute(req));
}

minio::s3::DeleteBucketLifecycleResponse
minio::s3::BaseClient::DeleteBucketLifecycle(DeleteBucketLifecycleArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteBucketLifecycleResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DeleteBucketLifecycleResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("lifecycle", "");

  return DeleteBucketLifecycleResponse(Execute(req));
}

minio::s3::DeleteBucketNotificationResponse
minio::s3::BaseClient::DeleteBucketNotification(
    DeleteBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteBucketNotificationResponse(err);
  }

  NotificationConfig config;
  SetBucketNotificationArgs sbnargs(config);
  sbnargs.extra_headers = args.extra_headers;
  sbnargs.extra_query_params = args.extra_query_params;
  sbnargs.bucket = args.bucket;
  sbnargs.region = args.region;

  return DeleteBucketNotificationResponse(SetBucketNotification(sbnargs));
}

minio::s3::DeleteBucketPolicyResponse minio::s3::BaseClient::DeleteBucketPolicy(
    DeleteBucketPolicyArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteBucketPolicyResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DeleteBucketPolicyResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("policy", "");

  return DeleteBucketPolicyResponse(Execute(req));
}

minio::s3::DeleteBucketReplicationResponse
minio::s3::BaseClient::DeleteBucketReplication(
    DeleteBucketReplicationArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteBucketReplicationResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DeleteBucketReplicationResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("replication", "");

  Response resp = Execute(req);
  if (resp) {
    return DeleteBucketReplicationResponse(resp);
  }
  if (resp.code != "ReplicationConfigurationNotFoundError") {
    return DeleteBucketReplicationResponse(resp);
  }
  return DeleteBucketReplicationResponse();
}

minio::s3::DeleteBucketTagsResponse minio::s3::BaseClient::DeleteBucketTags(
    DeleteBucketTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteBucketTagsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DeleteBucketTagsResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("tagging", "");

  return DeleteBucketTagsResponse(Execute(req));
}

minio::s3::DeleteObjectLockConfigResponse
minio::s3::BaseClient::DeleteObjectLockConfig(DeleteObjectLockConfigArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteObjectLockConfigResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DeleteObjectLockConfigResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("object-lock", "");

  return DeleteObjectLockConfigResponse(Execute(req));
}

minio::s3::DeleteObjectTagsResponse minio::s3::BaseClient::DeleteObjectTags(
    DeleteObjectTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return DeleteObjectTagsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return DeleteObjectTagsResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("tagging", "");

  return DeleteObjectTagsResponse(Execute(req));
}

minio::s3::EnableObjectLegalHoldResponse
minio::s3::BaseClient::EnableObjectLegalHold(EnableObjectLegalHoldArgs args) {
  if (error::Error err = args.Validate()) {
    return EnableObjectLegalHoldResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return EnableObjectLegalHoldResponse(resp);
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

  return EnableObjectLegalHoldResponse(Execute(req));
}

minio::s3::GetBucketEncryptionResponse
minio::s3::BaseClient::GetBucketEncryption(GetBucketEncryptionArgs args) {
  if (error::Error err = args.Validate()) {
    return GetBucketEncryptionResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetBucketEncryptionResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("encryption", "");

  Response resp = Execute(req);
  if (resp) {
    return GetBucketEncryptionResponse::ParseXML(resp.data);
  }
  return GetBucketEncryptionResponse(resp);
}

minio::s3::GetBucketLifecycleResponse minio::s3::BaseClient::GetBucketLifecycle(
    GetBucketLifecycleArgs args) {
  if (error::Error err = args.Validate()) {
    return GetBucketLifecycleResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetBucketLifecycleResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("lifecycle", "");

  Response resp = Execute(req);

  if (!resp) {
    if (resp.code == "NoSuchLifecycleConfiguration") {
      return GetBucketLifecycleResponse(LifecycleConfig());
    }
    return GetBucketLifecycleResponse(resp);
  }

  return GetBucketLifecycleResponse::ParseXML(resp.data);
}

minio::s3::GetBucketNotificationResponse
minio::s3::BaseClient::GetBucketNotification(GetBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return GetBucketNotificationResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetBucketNotificationResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("notification", "");

  Response resp = Execute(req);
  if (resp) {
    return GetBucketNotificationResponse::ParseXML(resp.data);
  }
  return GetBucketNotificationResponse(resp);
}

minio::s3::GetBucketPolicyResponse minio::s3::BaseClient::GetBucketPolicy(
    GetBucketPolicyArgs args) {
  if (error::Error err = args.Validate()) {
    return GetBucketPolicyResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetBucketPolicyResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("policy", "");

  Response resp = Execute(req);
  if (resp) {
    return GetBucketPolicyResponse(resp.data);
  }
  return GetBucketPolicyResponse(resp);
}

minio::s3::GetBucketReplicationResponse
minio::s3::BaseClient::GetBucketReplication(GetBucketReplicationArgs args) {
  if (error::Error err = args.Validate()) {
    return GetBucketReplicationResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetBucketReplicationResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("replication", "");

  Response resp = Execute(req);
  if (resp) {
    return GetBucketReplicationResponse::ParseXML(resp.data);
  }
  return GetBucketReplicationResponse(resp);
}

minio::s3::GetBucketTagsResponse minio::s3::BaseClient::GetBucketTags(
    GetBucketTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return GetBucketTagsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetBucketTagsResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("tagging", "");

  Response resp = Execute(req);
  if (resp) {
    return GetBucketTagsResponse::ParseXML(resp.data);
  }
  return GetBucketTagsResponse(resp);
}

minio::s3::GetBucketVersioningResponse
minio::s3::BaseClient::GetBucketVersioning(GetBucketVersioningArgs args) {
  if (error::Error err = args.Validate()) {
    return GetBucketVersioningResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetBucketVersioningResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("versioning", "");

  Response resp = Execute(req);
  if (!resp) {
    return GetBucketVersioningResponse(resp);
  }
  GetBucketVersioningResponse response;

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp.data.data());
  if (!result) {
    return error::make<GetBucketVersioningResponse>("unable to parse XML");
  }

  auto root = xdoc.select_node("/VersioningConfiguration");

  pugi::xpath_node text;

  if (!root.node().select_node("Status")) {
    text = root.node().select_node("Status/text()");
    response.status = (strcmp(text.node().value(), "Enabled") == 0);
  }
  if (!root.node().select_node("MFADelete")) {
    text = root.node().select_node("MFADelete/text()");
    response.mfa_delete = (strcmp(text.node().value(), "Enabled") == 0);
  }

  return GetBucketVersioningResponse(response);
}

minio::s3::GetObjectResponse minio::s3::BaseClient::GetObject(
    GetObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return GetObjectResponse(err);
  }

  if (args.ssec != nullptr && !base_url_.https) {
    return error::make<GetObjectResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetObjectResponse(resp);
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
  if (args.ssec != nullptr) req.headers.AddAll(args.ssec->Headers());

  return GetObjectResponse(Execute(req));
}

minio::s3::GetObjectLockConfigResponse
minio::s3::BaseClient::GetObjectLockConfig(GetObjectLockConfigArgs args) {
  if (error::Error err = args.Validate()) {
    return GetObjectLockConfigResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetObjectLockConfigResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("object-lock", "");

  Response resp = Execute(req);
  if (!resp) {
    return GetObjectLockConfigResponse(resp);
  }
  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp.data.data());
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

minio::s3::GetObjectRetentionResponse minio::s3::BaseClient::GetObjectRetention(
    GetObjectRetentionArgs args) {
  if (error::Error err = args.Validate()) {
    return GetObjectRetentionResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetObjectRetentionResponse(resp);
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

  Response resp = Execute(req);
  if (!resp) {
    if (resp.code == "NoSuchObjectLockConfiguration") {
      return GetObjectRetentionResponse(response);
    }
    return GetObjectRetentionResponse(resp);
  }

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp.data.data());
  if (!result) {
    return error::make<GetObjectRetentionResponse>("unable to parse XML");
  }

  auto text = xdoc.select_node("/Retention/Mode/text()");
  response.retention_mode = StringToRetentionMode(text.node().value());

  text = xdoc.select_node("/Retention/RetainUntilDate/text()");
  response.retain_until_date =
      utils::UtcTime::FromISO8601UTC(text.node().value());

  return GetObjectRetentionResponse(response);
}

minio::s3::GetObjectTagsResponse minio::s3::BaseClient::GetObjectTags(
    GetObjectTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return GetObjectTagsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetObjectTagsResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("tagging", "");

  Response resp = Execute(req);
  if (resp) {
    return GetObjectTagsResponse::ParseXML(resp.data);
  }
  return GetObjectTagsResponse(resp);
}

minio::s3::GetPresignedObjectUrlResponse
minio::s3::BaseClient::GetPresignedObjectUrl(GetPresignedObjectUrlArgs args) {
  if (error::Error err = args.Validate()) {
    return GetPresignedObjectUrlResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return GetPresignedObjectUrlResponse(resp);
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

minio::s3::GetPresignedPostFormDataResponse
minio::s3::BaseClient::GetPresignedPostFormData(PostPolicy policy) {
  if (!policy) {
    return error::make<GetPresignedPostFormDataResponse>(
        "valid policy must be provided");
  }

  if (provider_ == nullptr) {
    return error::make<GetPresignedPostFormDataResponse>(
        "Anonymous access does not require pre-signed post form-data");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(policy.bucket, policy.region)) {
    region = resp.region;
  } else {
    return GetPresignedPostFormDataResponse(resp);
  }

  creds::Credentials creds = provider_->Fetch();
  std::map<std::string, std::string> data;
  if (error::Error err =
          policy.FormData(data, creds.access_key, creds.secret_key,
                          creds.session_token, region)) {
    return GetPresignedPostFormDataResponse(err);
  }
  return GetPresignedPostFormDataResponse(data);
}

minio::s3::IsObjectLegalHoldEnabledResponse
minio::s3::BaseClient::IsObjectLegalHoldEnabled(
    IsObjectLegalHoldEnabledArgs args) {
  if (error::Error err = args.Validate()) {
    return IsObjectLegalHoldEnabledResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return IsObjectLegalHoldEnabledResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.query_params.Add("legal-hold", "");

  Response resp = Execute(req);
  if (!resp) {
    if (resp.code == "NoSuchObjectLockConfiguration") {
      return IsObjectLegalHoldEnabledResponse(false);
    }
    return IsObjectLegalHoldEnabledResponse(resp);
  }

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp.data.data());
  if (!result) {
    return error::make<IsObjectLegalHoldEnabledResponse>("unable to parse XML");
  }
  auto text = xdoc.select_node("/LegalHold/Status/text()");
  std::string value = text.node().value();
  return IsObjectLegalHoldEnabledResponse(value == "ON");
}

minio::s3::ListBucketsResponse minio::s3::BaseClient::ListBuckets(
    ListBucketsArgs args) {
  Request req(http::Method::kGet, base_url_.region, base_url_,
              args.extra_headers, args.extra_query_params);
  Response resp = Execute(req);
  if (!resp) {
    return ListBucketsResponse(resp);
  }
  return ListBucketsResponse::ParseXML(resp.data);
}

minio::s3::ListBucketsResponse minio::s3::BaseClient::ListBuckets() {
  return ListBuckets(ListBucketsArgs());
}

minio::s3::ListenBucketNotificationResponse
minio::s3::BaseClient::ListenBucketNotification(
    ListenBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return ListenBucketNotificationResponse(err);
  }

  if (!base_url_.aws_domain_suffix.empty()) {
    return error::make<ListenBucketNotificationResponse>(
        "ListenBucketNotification API is not supported in Amazon S3");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return ListenBucketNotificationResponse(resp);
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
      std::list<minio::s3::NotificationRecord> records;
      for (auto& j_record : j_records) {
        records.push_back(NotificationRecord::ParseJSON(j_record));
      }

      if (records.size() <= 0) continue;

      if (!func(records)) {
        return false;
      }
    }
  };

  return ListenBucketNotificationResponse(Execute(req));
}

minio::s3::ListObjectsResponse minio::s3::BaseClient::ListObjectsV1(
    ListObjectsV1Args args) {
  if (error::Error err = args.Validate()) {
    return ListObjectsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return ListObjectsResponse(resp);
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.marker.empty()) {
    req.query_params.Add("marker", args.marker);
  }
  Response resp = Execute(req);
  if (!resp) {
    return ListObjectsResponse(resp);
  }
  return ListObjectsResponse::ParseXML(resp.data, false);
}

minio::s3::ListObjectsResponse minio::s3::BaseClient::ListObjectsV2(
    ListObjectsV2Args args) {
  if (error::Error err = args.Validate()) {
    return ListObjectsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return ListObjectsResponse(resp);
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
  Response resp = Execute(req);
  if (!resp) {
    return ListObjectsResponse(resp);
  }
  return ListObjectsResponse::ParseXML(resp.data, false);
}

minio::s3::ListObjectsResponse minio::s3::BaseClient::ListObjectVersions(
    ListObjectVersionsArgs args) {
  if (error::Error err = args.Validate()) {
    return ListObjectsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return ListObjectsResponse(resp);
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

  Response resp = Execute(req);
  if (!resp) {
    return ListObjectsResponse(resp);
  }
  return ListObjectsResponse::ParseXML(resp.data, true);
}

minio::s3::MakeBucketResponse minio::s3::BaseClient::MakeBucket(
    MakeBucketArgs args) {
  if (error::Error err = args.Validate()) {
    return MakeBucketResponse(err);
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
    ss << "<CreateBucketConfiguration>"
       << "<LocationConstraint>" << region << "</LocationConstraint>"
       << "</CreateBucketConfiguration>";
    body = ss.str();
    req.body = body;
  }

  Response resp = Execute(req);
  if (resp) {
    region_map_[args.bucket] = region;
  }
  return MakeBucketResponse(resp);
}

minio::s3::PutObjectResponse minio::s3::BaseClient::PutObject(
    PutObjectApiArgs args) {
  if (error::Error err = args.Validate()) {
    return PutObjectResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return PutObjectResponse(resp);
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.AddAll(args.query_params);
  req.headers.AddAll(args.headers);
  req.body = args.data;
  req.progressfunc = args.progressfunc;
  req.progress_userdata = args.progress_userdata;

  Response response = Execute(req);
  if (!response) {
    return PutObjectResponse(response);
  }
  PutObjectResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');
  resp.version_id = response.headers.GetFront("x-amz-version-id");

  return resp;
}

minio::s3::RemoveBucketResponse minio::s3::BaseClient::RemoveBucket(
    RemoveBucketArgs args) {
  if (error::Error err = args.Validate()) {
    return RemoveBucketResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return RemoveBucketResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;

  return RemoveBucketResponse(Execute(req));
}

minio::s3::RemoveObjectResponse minio::s3::BaseClient::RemoveObject(
    RemoveObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return RemoveObjectResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return RemoveObjectResponse(resp);
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }

  return RemoveObjectResponse(Execute(req));
}

minio::s3::RemoveObjectsResponse minio::s3::BaseClient::RemoveObjects(
    RemoveObjectsApiArgs args) {
  if (error::Error err = args.Validate()) {
    return RemoveObjectsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return RemoveObjectsResponse(resp);
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
    ss << "<Key>" << object.name << "</Key>";
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

  Response response = Execute(req);
  if (!response) {
    return RemoveObjectsResponse(response);
  }
  return RemoveObjectsResponse::ParseXML(response.data);
}

minio::s3::SelectObjectContentResponse
minio::s3::BaseClient::SelectObjectContent(SelectObjectContentArgs args) {
  if (error::Error err = args.Validate()) {
    return SelectObjectContentResponse(err);
  }

  if (args.ssec != nullptr && !base_url_.https) {
    return error::make<SelectObjectContentResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SelectObjectContentResponse(resp);
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

  return SelectObjectContentResponse(Execute(req));
}

minio::s3::SetBucketEncryptionResponse
minio::s3::BaseClient::SetBucketEncryption(SetBucketEncryptionArgs args) {
  if (error::Error err = args.Validate()) {
    return SetBucketEncryptionResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetBucketEncryptionResponse(resp);
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

  return SetBucketEncryptionResponse(Execute(req));
}

minio::s3::SetBucketLifecycleResponse minio::s3::BaseClient::SetBucketLifecycle(
    SetBucketLifecycleArgs args) {
  if (error::Error err = args.Validate()) {
    return SetBucketLifecycleResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetBucketLifecycleResponse(resp);
  }

  std::string body = args.config.ToXML();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("lifecycle", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  return SetBucketLifecycleResponse(Execute(req));
}

minio::s3::SetBucketNotificationResponse
minio::s3::BaseClient::SetBucketNotification(SetBucketNotificationArgs args) {
  if (error::Error err = args.Validate()) {
    return SetBucketNotificationResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetBucketNotificationResponse(resp);
  }

  std::string body = args.config.ToXML();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("notification", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  return SetBucketNotificationResponse(Execute(req));
}

minio::s3::SetBucketPolicyResponse minio::s3::BaseClient::SetBucketPolicy(
    SetBucketPolicyArgs args) {
  if (error::Error err = args.Validate()) {
    return SetBucketPolicyResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetBucketPolicyResponse(resp);
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("policy", "");
  req.body = args.policy;
  req.headers.Add("Content-MD5", utils::Md5sumHash(args.policy));

  return SetBucketPolicyResponse(Execute(req));
}

minio::s3::SetBucketReplicationResponse
minio::s3::BaseClient::SetBucketReplication(SetBucketReplicationArgs args) {
  if (error::Error err = args.Validate()) {
    return SetBucketReplicationResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetBucketReplicationResponse(resp);
  }

  std::string body = args.config.ToXML();

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.Add("replication", "");
  req.headers.Add("Content-MD5", utils::Md5sumHash(body));
  req.body = std::move(body);

  return SetBucketReplicationResponse(Execute(req));
}

minio::s3::SetBucketTagsResponse minio::s3::BaseClient::SetBucketTags(
    SetBucketTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return SetBucketTagsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetBucketTagsResponse(resp);
  }

  std::stringstream ss;
  ss << "<Tagging>";
  if (!args.tags.empty()) {
    ss << "<TagSet>";
    for (auto& [key, value] : args.tags) {
      ss << "<Tag>"
         << "<Key>" << key << "</Key>"
         << "<Value>" << value << "</Value>"
         << "</Tag>";
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

  return SetBucketTagsResponse(Execute(req));
}

minio::s3::SetBucketVersioningResponse
minio::s3::BaseClient::SetBucketVersioning(SetBucketVersioningArgs args) {
  if (error::Error err = args.Validate()) {
    return SetBucketVersioningResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetBucketVersioningResponse(resp);
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

  return SetBucketVersioningResponse(Execute(req));
}

minio::s3::SetObjectLockConfigResponse
minio::s3::BaseClient::SetObjectLockConfig(SetObjectLockConfigArgs args) {
  if (error::Error err = args.Validate()) {
    return SetObjectLockConfigResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetObjectLockConfigResponse(resp);
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

  return SetObjectLockConfigResponse(Execute(req));
}

minio::s3::SetObjectRetentionResponse minio::s3::BaseClient::SetObjectRetention(
    SetObjectRetentionArgs args) {
  if (error::Error err = args.Validate()) {
    return SetObjectRetentionResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetObjectRetentionResponse(resp);
  }

  std::stringstream ss;
  ss << "<Retention>"
     << "<Mode>" << RetentionModeToString(args.retention_mode) << "</Mode>"
     << "<RetainUntilDate>" << args.retain_until_date.ToISO8601UTC()
     << "</RetainUntilDate>"
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

  return SetObjectRetentionResponse(Execute(req));
}

minio::s3::SetObjectTagsResponse minio::s3::BaseClient::SetObjectTags(
    SetObjectTagsArgs args) {
  if (error::Error err = args.Validate()) {
    return SetObjectTagsResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return SetObjectTagsResponse(resp);
  }

  std::stringstream ss;
  ss << "<Tagging>";
  if (!args.tags.empty()) {
    ss << "<TagSet>";
    for (auto& [key, value] : args.tags) {
      ss << "<Tag>"
         << "<Key>" << key << "</Key>"
         << "<Value>" << value << "</Value>"
         << "</Tag>";
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

  return SetObjectTagsResponse(Execute(req));
}

minio::s3::StatObjectResponse minio::s3::BaseClient::StatObject(
    StatObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return StatObjectResponse(err);
  }

  if (args.ssec != nullptr && !base_url_.https) {
    return error::make<StatObjectResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return StatObjectResponse(resp);
  }

  Request req(http::Method::kHead, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.headers.AddAll(args.Headers());

  Response response = Execute(req);
  if (!response) {
    return StatObjectResponse(response);
  }
  StatObjectResponse resp(response);
  resp.bucket_name = args.bucket;
  resp.object_name = args.object;
  resp.version_id = response.headers.GetFront("x-amz-version-id");

  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');

  std::string value = response.headers.GetFront("content-length");
  if (!value.empty()) resp.size = std::stol(value);

  value = response.headers.GetFront("last-modified");
  if (!value.empty()) {
    resp.last_modified = utils::UtcTime::FromHttpHeaderValue(value.c_str());
  }

  value = response.headers.GetFront("x-amz-object-lock-mode");
  if (!value.empty()) resp.retention_mode = StringToRetentionMode(value);

  value = response.headers.GetFront("x-amz-object-lock-retain-until-date");
  if (!value.empty()) {
    resp.retention_retain_until_date =
        utils::UtcTime::FromISO8601UTC(value.c_str());
  }

  value = response.headers.GetFront("x-amz-object-lock-legal-hold");
  if (!value.empty()) resp.legal_hold = StringToLegalHold(value);

  value = response.headers.GetFront("x-amz-delete-marker");
  if (!value.empty()) resp.delete_marker = utils::StringToBool(value);

  utils::Multimap user_metadata;
  std::list<std::string> keys = response.headers.Keys();
  for (auto key : keys) {
    if (utils::StartsWith(key, "x-amz-meta-")) {
      std::list<std::string> values = response.headers.Get(key);
      key.erase(0, 11);
      for (auto value : values) user_metadata.Add(key, value);
    }
  }
  resp.user_metadata = user_metadata;

  return resp;
}

minio::s3::UploadPartResponse minio::s3::BaseClient::UploadPart(
    UploadPartArgs args) {
  if (error::Error err = args.Validate()) {
    return UploadPartResponse(err);
  }

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
  api_args.progressfunc = args.progressfunc;
  api_args.progress_userdata = args.progress_userdata;
  api_args.query_params = query_params;

  return UploadPartResponse(PutObject(api_args));
}

minio::s3::UploadPartCopyResponse minio::s3::BaseClient::UploadPartCopy(
    UploadPartCopyArgs args) {
  if (error::Error err = args.Validate()) {
    return UploadPartCopyResponse(err);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return UploadPartCopyResponse(resp);
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.AddAll(args.extra_query_params);
  req.query_params.Add("partNumber", std::to_string(args.part_number));
  req.query_params.Add("uploadId", args.upload_id);
  req.headers.AddAll(args.headers);

  Response response = Execute(req);
  if (!response) {
    return UploadPartCopyResponse(response);
  }
  UploadPartCopyResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');

  return resp;
}
