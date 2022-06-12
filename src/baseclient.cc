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

minio::utils::Multimap minio::s3::GetCommonListObjectsQueryParams(
    std::string& delimiter, std::string& encoding_type, unsigned int max_keys,
    std::string& prefix) {
  utils::Multimap query_params;
  query_params.Add("delimiter", delimiter);
  query_params.Add("max-keys", std::to_string(max_keys > 0 ? max_keys : 1000));
  query_params.Add("prefix", prefix);
  if (!encoding_type.empty()) query_params.Add("encoding-type", encoding_type);
  return query_params;
}

minio::s3::BaseClient::BaseClient(BaseUrl& base_url, creds::Provider* provider)
    : base_url_(base_url) {
  if (!base_url_) {
    std::cerr << "valid base url must be provided; "
              << base_url_.Error().String() << std::endl;
    std::terminate();
  }

  this->provider_ = provider;
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
    http::Method method, utils::Multimap headers, std::string& bucket_name,
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
      code = "";
      message = "";
      break;
  }

  std::string region = headers.GetFront("x-amz-bucket-region");

  if (!message.empty() && !region.empty()) {
    message += "; use region " + region;
  }

  if (retry && !region.empty() && method == http::Method::kHead &&
      !bucket_name.empty() && !region_map_[bucket_name].empty()) {
    code = "RetryHead";
    message = "";
  }
}

minio::s3::Response minio::s3::BaseClient::GetErrorResponse(
    http::Response resp, std::string_view resource, http::Method method,
    std::string& bucket_name, std::string& object_name) {
  if (!resp.error.empty()) return error::Error(resp.error);

  if (!resp.body.empty()) {
    std::list<std::string> values = resp.headers.Get("Content-Type");
    for (auto& value : values) {
      if (utils::Contains(utils::ToLower(value), "application/xml")) {
        return Response::ParseXML(resp.body, resp.status_code, resp.headers);
      }
    }

    Response response(
        error::Error("invalid response received; status code: " +
                     std::to_string(resp.status_code) +
                     "; content-type: " + utils::Join(values, ",")));
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
    default:
      Response response(error::Error("server failed with HTTP status code " +
                                     std::to_string(resp.status_code)));
      response.status_code = resp.status_code;
      response.headers = resp.headers;
      return response;
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
  http::Request request = req.ToHttpRequest(provider_);
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
    std::string& bucket_name, std::string& region) {
  std::string base_region = base_url_.region;
  if (!region.empty()) {
    if (!base_region.empty() && base_region != region) {
      return error::Error("region must be " + base_region + ", but passed " +
                          region);
    }

    return region;
  }

  if (!base_region.empty()) return base_region;

  if (bucket_name.empty() || provider_ == NULL) return std::string("us-east-1");

  std::string stored_region = region_map_[bucket_name];
  if (!stored_region.empty()) return stored_region;

  Request req(http::Method::kGet, "us-east-1", base_url_, utils::Multimap(),
              utils::Multimap());
  req.query_params.Add("location", "");
  req.bucket_name = bucket_name;

  Response resp = Execute(req);
  if (!resp) return resp;

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(resp.data.data());
  if (!result) return error::Error("unable to parse XML");
  auto text = xdoc.select_node("/LocationConstraint/text()");
  std::string value = text.node().value();

  if (value.empty()) {
    value = "us-east-1";
  } else if (value == "EU") {
    if (base_url_.aws_host) value = "eu-west-1";
  }

  region_map_[bucket_name] = value;

  return value;
}

minio::s3::AbortMultipartUploadResponse
minio::s3::BaseClient::AbortMultipartUpload(AbortMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.Add("uploadId", args.upload_id);

  return Execute(req);
}

minio::s3::BucketExistsResponse minio::s3::BaseClient::BucketExists(
    BucketExistsArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return (resp.code == "NoSuchBucket") ? false : resp;
  }

  Request req(http::Method::kHead, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  if (Response resp = Execute(req)) {
    return true;
  } else {
    return (resp.code == "NoSuchBucket") ? false : resp;
  }
}

minio::s3::CompleteMultipartUploadResponse
minio::s3::BaseClient::CompleteMultipartUpload(
    CompleteMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
  if (!response) return response;
  return CompleteMultipartUploadResponse::ParseXML(
      response.data, response.headers.GetFront("x-amz-version-id"));
}

minio::s3::CreateMultipartUploadResponse
minio::s3::BaseClient::CreateMultipartUpload(CreateMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (!args.headers.Contains("Content-Type")) {
    args.headers.Add("Content-Type", "application/octet-stream");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
    if (!result) return error::Error("unable to parse XML");
    auto text =
        xdoc.select_node("/InitiateMultipartUploadResult/UploadId/text()");
    return std::string(text.node().value());
  } else {
    return resp;
  }
}

minio::s3::GetObjectResponse minio::s3::BaseClient::GetObject(
    GetObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.ssec != NULL && !base_url_.https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
  if (args.ssec != NULL) req.headers.AddAll(args.ssec->Headers());

  return Execute(req);
}

minio::s3::ListBucketsResponse minio::s3::BaseClient::ListBuckets(
    ListBucketsArgs args) {
  Request req(http::Method::kGet, base_url_.region, base_url_,
              args.extra_headers, args.extra_query_params);
  Response resp = Execute(req);
  if (!resp) return resp;
  return ListBucketsResponse::ParseXML(resp.data);
}

minio::s3::ListBucketsResponse minio::s3::BaseClient::ListBuckets() {
  return ListBuckets(ListBucketsArgs());
}

minio::s3::ListObjectsResponse minio::s3::BaseClient::ListObjectsV1(
    ListObjectsV1Args args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.marker.empty()) req.query_params.Add("marker", args.marker);

  Response resp = Execute(req);
  if (!resp) resp;

  return ListObjectsResponse::ParseXML(resp.data, false);
}

minio::s3::ListObjectsResponse minio::s3::BaseClient::ListObjectsV2(
    ListObjectsV2Args args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
  if (args.fetch_owner) req.query_params.Add("fetch-owner", "true");
  if (!args.start_after.empty()) {
    req.query_params.Add("start-after", args.start_after);
  }
  if (args.include_user_metadata) req.query_params.Add("metadata", "true");

  Response resp = Execute(req);
  if (!resp) resp;

  return ListObjectsResponse::ParseXML(resp.data, false);
}

minio::s3::ListObjectsResponse minio::s3::BaseClient::ListObjectVersions(
    ListObjectVersionsArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
  if (!resp) resp;

  return ListObjectsResponse::ParseXML(resp.data, true);
}

minio::s3::MakeBucketResponse minio::s3::BaseClient::MakeBucket(
    MakeBucketArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region = args.region;
  std::string base_region = base_url_.region;
  if (!base_region.empty() && !region.empty() && base_region != region) {
    return error::Error("region must be " + base_region + ", but passed " +
                        region);
  }

  if (region.empty()) region = base_region;
  if (region.empty()) region = "us-east-1";

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
  if (resp) region_map_[args.bucket] = region;

  return resp;
}

minio::s3::PutObjectResponse minio::s3::BaseClient::PutObject(
    PutObjectApiArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params.AddAll(args.query_params);
  req.headers.AddAll(args.headers);
  req.body = args.data;

  Response response = Execute(req);
  if (!response) return response;

  PutObjectResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');
  resp.version_id = response.headers.GetFront("x-amz-version-id");

  return resp;
}

minio::s3::RemoveBucketResponse minio::s3::BaseClient::RemoveBucket(
    RemoveBucketArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;

  return Execute(req);
}

minio::s3::RemoveObjectResponse minio::s3::BaseClient::RemoveObject(
    RemoveObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kDelete, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }

  return Execute(req);
}

minio::s3::RemoveObjectsResponse minio::s3::BaseClient::RemoveObjects(
    RemoveObjectsApiArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
  if (!response) return response;
  return RemoveObjectsResponse::ParseXML(response.data);
}

minio::s3::StatObjectResponse minio::s3::BaseClient::StatObject(
    StatObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.ssec != NULL && !base_url_.https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
  if (!response) return response;

  StatObjectResponse resp = response;
  resp.bucket_name = args.bucket;
  resp.object_name = args.object;
  resp.version_id = response.headers.GetFront("x-amz-version-id");

  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');

  std::string value = response.headers.GetFront("content-length");
  if (!value.empty()) resp.size = std::stoi(value);

  value = response.headers.GetFront("last-modified");
  if (!value.empty()) {
    resp.last_modified = utils::Time::FromHttpHeaderValue(value.c_str());
  }

  value = response.headers.GetFront("x-amz-object-lock-mode");
  if (!value.empty()) resp.retention_mode = StringToRetentionMode(value);

  value = response.headers.GetFront("x-amz-object-lock-retain-until-date");
  if (!value.empty()) {
    resp.retention_retain_until_date =
        utils::Time::FromISO8601UTC(value.c_str());
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
  if (error::Error err = args.Validate()) return err;

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
  api_args.query_params = query_params;

  return PutObject(api_args);
}

minio::s3::UploadPartCopyResponse minio::s3::BaseClient::UploadPartCopy(
    UploadPartCopyArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
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
  if (!response) return response;

  UploadPartCopyResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');

  return resp;
}
