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

#include "client.h"

minio::utils::Multimap minio::s3::GetCommonListObjectsQueryParams(
    std::string delimiter, std::string encoding_type, unsigned int max_keys,
    std::string prefix) {
  utils::Multimap query_params;
  query_params.Add("delimiter", delimiter);
  query_params.Add("max-keys", std::to_string(max_keys > 0 ? max_keys : 1000));
  query_params.Add("prefix", prefix);
  if (!encoding_type.empty()) query_params.Add("encoding-type", encoding_type);
  return query_params;
}

minio::s3::Client::Client(http::BaseUrl& base_url, creds::Provider* provider)
    : base_url_(base_url) {
  if (!base_url_) {
    std::cerr << "base URL must not be empty" << std::endl;
    std::terminate();
  }

  provider_ = provider;
}

minio::error::Error minio::s3::Client::SetAppInfo(
    std::string_view app_name, std::string_view app_version) {
  if (app_name.empty() || app_version.empty()) {
    return error::Error("Application name/version cannot be empty");
  }

  user_agent_ = std::string(DEFAULT_USER_AGENT) + " " + std::string(app_name) +
                "/" + std::string(app_version);
  return error::SUCCESS;
}

void minio::s3::Client::HandleRedirectResponse(
    std::string& code, std::string& message, int status_code,
    http::Method method, utils::Multimap headers, std::string_view bucket_name,
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
      !bucket_name.empty() && !region_map_[std::string(bucket_name)].empty()) {
    code = "RetryHead";
    message = "";
  }
}

minio::s3::Response minio::s3::Client::GetErrorResponse(
    http::Response resp, std::string_view resource, http::Method method,
    std::string_view bucket_name, std::string_view object_name) {
  if (!resp.error.empty()) return error::Error(resp.error);

  Response response;
  response.status_code = resp.status_code;
  response.headers = resp.headers;
  std::string data = resp.body;
  if (!data.empty()) {
    std::list<std::string> values = resp.headers.Get("Content-Type");
    for (auto& value : values) {
      if (utils::Contains(utils::ToLower(value), "application/xml")) {
        return Response::ParseXML(resp.body, resp.status_code, resp.headers);
      }
    }

    response.error = "invalid response received; status code: " +
                     std::to_string(resp.status_code) +
                     "; content-type: " + utils::Join(values, ",");
    return response;
  }

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
      response.error = "server failed with HTTP status code " +
                       std::to_string(resp.status_code);
      return response;
  }

  response.resource = resource;
  response.request_id = response.headers.GetFront("x-amz-request-id");
  response.host_id = response.headers.GetFront("x-amz-id-2");
  response.bucket_name = bucket_name;
  response.object_name = object_name;

  return response;
}

minio::s3::Response minio::s3::Client::execute(Request& req) {
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

minio::s3::Response minio::s3::Client::Execute(Request& req) {
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

minio::s3::GetRegionResponse minio::s3::Client::GetRegion(
    std::string_view bucket_name, std::string_view region) {
  std::string base_region = base_url_.region;
  if (!region.empty()) {
    if (!base_region.empty() && base_region != region) {
      return error::Error("region must be " + base_region + ", but passed " +
                          std::string(region));
    }

    return std::string(region);
  }

  if (!base_region.empty()) return base_region;

  if (bucket_name.empty() || provider_ == NULL) return std::string("us-east-1");

  std::string stored_region = region_map_[std::string(bucket_name)];
  if (!stored_region.empty()) return stored_region;

  Request req(http::Method::kGet, "us-east-1", base_url_);
  utils::Multimap query_params;
  query_params.Add("location", "");
  req.query_params = query_params;
  req.bucket_name = bucket_name;

  Response response = Execute(req);
  if (!response) return response;

  pugi::xml_document xdoc;
  pugi::xml_parse_result result = xdoc.load_string(response.data.data());
  if (!result) return error::Error("unable to parse XML");
  auto text = xdoc.select_node("/LocationConstraint/text()");
  std::string value = text.node().value();

  if (value.empty()) {
    value = "us-east-1";
  } else if (value == "EU") {
    if (base_url_.aws_host) value = "eu-west-1";
  }

  region_map_[std::string(bucket_name)] = value;

  return value;
}

minio::s3::MakeBucketResponse minio::s3::Client::MakeBucket(
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

  Request req(http::Method::kPut, region, base_url_);
  req.bucket_name = args.bucket;

  utils::Multimap headers;
  if (args.object_lock) headers.Add("x-amz-bucket-object-lock-enabled", "true");
  req.headers = headers;

  std::string body;
  if (region != "us-east-1") {
    std::stringstream ss;
    ss << "<CreateBucketConfiguration>"
       << "<LocationConstraint>" << region << "</LocationConstraint>"
       << "</CreateBucketConfiguration>";
    body = ss.str();
    req.body = body;
  }

  Response response = Execute(req);
  if (response) region_map_[std::string(args.bucket)] = region;

  return response;
}

minio::s3::ListBucketsResponse minio::s3::Client::ListBuckets(
    ListBucketsArgs args) {
  Request req(http::Method::kGet, base_url_.region, base_url_);
  req.headers = args.extra_headers;
  req.query_params = args.extra_query_params;
  Response resp = Execute(req);
  if (!resp) return resp;
  return ListBucketsResponse::ParseXML(resp.data);
}

minio::s3::ListBucketsResponse minio::s3::Client::ListBuckets() {
  return ListBuckets(ListBucketsArgs());
}

minio::s3::BucketExistsResponse minio::s3::Client::BucketExists(
    BucketExistsArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return (resp.code == "NoSuchBucket") ? false : resp;
  }

  Request req(http::Method::kHead, region, base_url_);
  req.bucket_name = args.bucket;
  if (Response resp = Execute(req)) {
    return true;
  } else {
    return (resp.code == "NoSuchBucket") ? false : resp;
  }
}

minio::s3::RemoveBucketResponse minio::s3::Client::RemoveBucket(
    RemoveBucketArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kDelete, region, base_url_);
  req.bucket_name = args.bucket;

  return Execute(req);
}

minio::s3::AbortMultipartUploadResponse minio::s3::Client::AbortMultipartUpload(
    AbortMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kDelete, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  utils::Multimap query_params;
  query_params.Add("uploadId", args.upload_id);
  req.query_params = query_params;

  return Execute(req);
}

minio::s3::CompleteMultipartUploadResponse
minio::s3::Client::CompleteMultipartUpload(CompleteMultipartUploadArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kPost, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  utils::Multimap query_params;
  query_params.Add("uploadId", args.upload_id);
  req.query_params = query_params;

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
minio::s3::Client::CreateMultipartUpload(CreateMultipartUploadArgs args) {
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

  Request req(http::Method::kPost, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  utils::Multimap query_params;
  query_params.Add("uploads", "");
  req.query_params = query_params;

  req.headers = args.headers;

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

minio::s3::PutObjectResponse minio::s3::Client::PutObject(
    PutObjectApiArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kPut, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params = args.query_params;
  req.headers = args.headers;
  req.body = args.data;

  Response response = Execute(req);
  if (!response) return response;

  PutObjectResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');
  resp.version_id = response.headers.GetFront("x-amz-version-id");

  return resp;
}

minio::s3::UploadPartResponse minio::s3::Client::UploadPart(
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

minio::s3::UploadPartCopyResponse minio::s3::Client::UploadPartCopy(
    UploadPartCopyArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kPut, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;

  utils::Multimap query_params;
  query_params.AddAll(args.extra_query_params);
  query_params.Add("partNumber", std::to_string(args.part_number));
  query_params.Add("uploadId", args.upload_id);
  req.query_params = query_params;

  req.headers = args.headers;

  Response response = Execute(req);
  if (!response) return response;

  UploadPartCopyResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');

  return resp;
}

minio::s3::StatObjectResponse minio::s3::Client::StatObject(
    StatObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.ssec != NULL && !base_url_.is_https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kHead, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  utils::Multimap query_params;
  if (!args.version_id.empty()) {
    query_params.Add("versionId", args.version_id);
    req.query_params = query_params;
  }
  req.headers = args.Headers();

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

minio::s3::RemoveObjectResponse minio::s3::Client::RemoveObject(
    RemoveObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kDelete, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  utils::Multimap query_params;
  if (!args.version_id.empty()) {
    query_params.Add("versionId", args.version_id);
    req.query_params = query_params;
  }

  return Execute(req);
}

minio::s3::DownloadObjectResponse minio::s3::Client::DownloadObject(
    DownloadObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.ssec != NULL && !base_url_.is_https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string etag;
  size_t size;
  {
    StatObjectArgs soargs;
    soargs.bucket = args.bucket;
    soargs.region = args.region;
    soargs.object = args.object;
    soargs.version_id = args.version_id;
    soargs.ssec = args.ssec;
    StatObjectResponse resp = StatObject(soargs);
    if (!resp) return resp;
    etag = resp.etag;
    size = resp.size;
  }

  std::string temp_filename =
      args.filename + "." + curlpp::escape(etag) + ".part.minio";
  std::ofstream fout(temp_filename, fout.trunc | fout.out);
  if (!fout.is_open()) {
    DownloadObjectResponse resp;
    resp.error = "unable to open file " + temp_filename;
    return resp;
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kGet, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  utils::Multimap query_params;
  if (!args.version_id.empty()) {
    query_params.Add("versionId", std::string(args.version_id));
    req.query_params = query_params;
  }
  req.data_callback = [](http::DataCallbackArgs args) -> size_t {
    std::ofstream* fout = (std::ofstream*)args.user_arg;
    *fout << std::string(args.buffer, args.length);
    return args.size * args.length;
  };
  req.user_arg = &fout;

  Response response = Execute(req);
  fout.close();
  if (response) std::filesystem::rename(temp_filename, args.filename);
  return response;
}

minio::s3::GetObjectResponse minio::s3::Client::GetObject(GetObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.ssec != NULL && !base_url_.is_https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kGet, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  utils::Multimap query_params;
  if (!args.version_id.empty()) {
    query_params.Add("versionId", args.version_id);
    req.query_params = query_params;
  }
  req.data_callback = args.data_callback;
  req.user_arg = args.user_arg;
  if (args.ssec != NULL) {
    req.headers = args.ssec->Headers();
  }

  return Execute(req);
}

minio::s3::ListObjectsResponse minio::s3::Client::ListObjectsV1(
    ListObjectsV1Args args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  utils::Multimap query_params;
  query_params.AddAll(args.extra_query_params);
  query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.marker.empty()) query_params.Add("marker", args.marker);

  Request req(http::Method::kGet, region, base_url_);
  req.bucket_name = args.bucket;
  req.query_params = query_params;
  req.headers = args.extra_headers;

  Response resp = Execute(req);
  if (!resp) resp;

  return ListObjectsResponse::ParseXML(resp.data, false);
}

minio::s3::ListObjectsResponse minio::s3::Client::ListObjectsV2(
    ListObjectsV2Args args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  utils::Multimap query_params;
  query_params.Add("list-type", "2");
  query_params.AddAll(args.extra_query_params);
  query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.continuation_token.empty())
    query_params.Add("continuation-token", args.continuation_token);
  if (args.fetch_owner) query_params.Add("fetch-owner", "true");
  if (!args.start_after.empty()) {
    query_params.Add("start-after", args.start_after);
  }
  if (args.include_user_metadata) query_params.Add("metadata", "true");

  Request req(http::Method::kGet, region, base_url_);
  req.bucket_name = args.bucket;
  req.query_params = query_params;
  req.headers = args.extra_headers;

  Response resp = Execute(req);
  if (!resp) resp;

  return ListObjectsResponse::ParseXML(resp.data, false);
}

minio::s3::ListObjectsResponse minio::s3::Client::ListObjectVersions(
    ListObjectVersionsArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  utils::Multimap query_params;
  query_params.Add("versions", "");
  query_params.AddAll(args.extra_query_params);
  query_params.AddAll(GetCommonListObjectsQueryParams(
      args.delimiter, args.encoding_type, args.max_keys, args.prefix));
  if (!args.key_marker.empty()) query_params.Add("key-marker", args.key_marker);
  if (!args.version_id_marker.empty()) {
    query_params.Add("version-id-marker", args.version_id_marker);
  }

  Request req(http::Method::kGet, region, base_url_);
  req.bucket_name = args.bucket;
  req.query_params = query_params;
  req.headers = args.extra_headers;

  Response resp = Execute(req);
  if (!resp) resp;

  return ListObjectsResponse::ParseXML(resp.data, true);
}

minio::s3::ListObjectsResult::ListObjectsResult(error::Error err) {
  failed_ = true;
  resp_.contents.push_back(Item(err));
  itr_ = resp_.contents.begin();
}

minio::s3::ListObjectsResult::ListObjectsResult(Client* client,
                                                ListObjectsArgs* args) {
  client_ = client;
  args_ = args;
  Populate();
}

void minio::s3::ListObjectsResult::Populate() {
  if (args_->include_versions) {
    args_->key_marker = resp_.next_key_marker;
    args_->version_id_marker = resp_.next_version_id_marker;
  } else if (args_->use_api_v1) {
    args_->marker = resp_.next_marker;
  } else {
    args_->start_after = resp_.start_after;
    args_->continuation_token = resp_.next_continuation_token;
  }

  std::string region;
  if (GetRegionResponse resp =
          client_->GetRegion(args_->bucket, args_->region)) {
    region = resp.region;
    if (args_->recursive) {
      args_->delimiter = "";
    } else if (args_->delimiter.empty()) {
      args_->delimiter = "/";
    }

    if (args_->include_versions || !args_->version_id_marker.empty()) {
      resp_ = client_->ListObjectVersions(*args_);
    } else if (args_->use_api_v1) {
      resp_ = client_->ListObjectsV1(*args_);
    } else {
      resp_ = client_->ListObjectsV2(*args_);
    }

    if (!resp_) {
      failed_ = true;
      resp_.contents.push_back(Item(resp_));
    }
  } else {
    failed_ = true;
    resp_.contents.push_back(Item(resp));
  }

  itr_ = resp_.contents.begin();
}

minio::s3::ListObjectsResult minio::s3::Client::ListObjects(
    ListObjectsArgs args) {
  if (error::Error err = args.Validate()) return err;
  return ListObjectsResult(this, &args);
}

minio::s3::PutObjectResponse minio::s3::Client::PutObject(
    PutObjectArgs& args, std::string& upload_id, char* buf) {
  utils::Multimap headers = args.Headers();
  if (!headers.Contains("Content-Type")) {
    if (args.content_type.empty()) {
      headers.Add("Content-Type", "application/octet-stream");
    } else {
      headers.Add("Content-Type", args.content_type);
    }
  }

  long object_size = args.object_size;
  size_t part_size = args.part_size;
  size_t uploaded_size = 0;
  unsigned int part_number = 0;
  std::string one_byte;
  bool stop = false;
  std::list<Part> parts;
  long part_count = args.part_count;

  while (!stop) {
    part_number++;

    size_t bytes_read = 0;
    if (part_count > 0) {
      if (part_number == part_count) {
        part_size = object_size - uploaded_size;
        stop = true;
      }

      if (error::Error err =
              utils::ReadPart(args.stream, buf, part_size, bytes_read)) {
        return err;
      }

      if (bytes_read != part_size) {
        return error::Error("not enough data in the stream; expected: " +
                            std::to_string(part_size) +
                            ", got: " + std::to_string(bytes_read) + " bytes");
      }
    } else {
      char* b = buf;
      size_t size = part_size + 1;

      if (!one_byte.empty()) {
        buf[0] = one_byte.front();
        b = buf + 1;
        size--;
        bytes_read = 1;
        one_byte = "";
      }

      size_t n = 0;
      if (error::Error err = utils::ReadPart(args.stream, b, size, n)) {
        return err;
      }

      bytes_read += n;

      // If bytes read is less than or equals to part size, then we have reached
      // last part.
      if (bytes_read <= part_size) {
        part_count = part_number;
        part_size = bytes_read;
        stop = true;
      } else {
        one_byte = buf[part_size + 1];
      }
    }

    std::string_view data(buf, part_size);

    uploaded_size += part_size;

    if (part_count == 1) {
      PutObjectApiArgs api_args;
      api_args.extra_query_params = args.extra_query_params;
      api_args.bucket = args.bucket;
      api_args.region = args.region;
      api_args.object = args.object;
      api_args.data = data;
      api_args.headers = headers;

      return PutObject(api_args);
    }

    if (upload_id.empty()) {
      CreateMultipartUploadArgs cmu_args;
      cmu_args.extra_query_params = args.extra_query_params;
      cmu_args.bucket = args.bucket;
      cmu_args.region = args.region;
      cmu_args.object = args.object;
      cmu_args.headers = headers;
      if (CreateMultipartUploadResponse resp =
              CreateMultipartUpload(cmu_args)) {
        upload_id = resp.upload_id;
      } else {
        return resp;
      }
    }

    UploadPartArgs up_args;
    up_args.bucket = args.bucket;
    up_args.region = args.region;
    up_args.object = args.object;
    up_args.upload_id = upload_id;
    up_args.part_number = part_number;
    up_args.data = data;
    if (args.sse != NULL) {
      if (SseCustomerKey* ssec = dynamic_cast<SseCustomerKey*>(args.sse)) {
        up_args.headers = ssec->Headers();
      }
    }

    if (UploadPartResponse resp = UploadPart(up_args)) {
      parts.push_back(Part{part_number, resp.etag});
    } else {
      return resp;
    }
  }

  CompleteMultipartUploadArgs cmu_args;
  cmu_args.bucket = args.bucket;
  cmu_args.region = args.region;
  cmu_args.object = args.object;
  cmu_args.upload_id = upload_id;
  cmu_args.parts = parts;
  return CompleteMultipartUpload(cmu_args);
}

minio::s3::PutObjectResponse minio::s3::Client::PutObject(PutObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.sse != NULL && args.sse->TlsRequired() && !base_url_.is_https) {
    return error::Error(
        "SSE operation must be performed over a secure connection");
  }

  char* buf = NULL;
  if (args.part_count > 0) {
    buf = new char[args.part_size];
  } else {
    buf = new char[args.part_size + 1];
  }

  std::string upload_id;
  PutObjectResponse resp = PutObject(args, upload_id, buf);
  delete buf;

  if (!resp && !upload_id.empty()) {
    AbortMultipartUploadArgs amu_args;
    amu_args.bucket = args.bucket;
    amu_args.region = args.region;
    amu_args.object = args.object;
    amu_args.upload_id = upload_id;
    AbortMultipartUpload(amu_args);
  }

  return resp;
}

minio::s3::CopyObjectResponse minio::s3::Client::CopyObject(
    CopyObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.sse != NULL && args.sse->TlsRequired() && !base_url_.is_https) {
    return error::Error(
        "SSE operation must be performed over a secure connection");
  }

  if (args.source.ssec != NULL && !base_url_.is_https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string etag;
  size_t size;
  {
    StatObjectArgs soargs;
    soargs.extra_headers = args.source.extra_headers;
    soargs.extra_query_params = args.source.extra_query_params;
    soargs.bucket = args.source.bucket;
    soargs.region = args.source.region;
    soargs.object = args.source.object;
    soargs.version_id = args.source.version_id;
    soargs.ssec = args.source.ssec;
    StatObjectResponse resp = StatObject(soargs);
    if (!resp) return resp;
    etag = resp.etag;
    size = resp.size;
  }

  if (args.source.offset != NULL || args.source.length != NULL ||
      size > utils::kMaxPartSize) {
    if (args.metadata_directive != NULL &&
        *args.metadata_directive == Directive::kCopy) {
      return error::Error(
          "COPY metadata directive is not applicable to source object size "
          "greater than 5 GiB");
    }

    if (args.tagging_directive != NULL &&
        *args.tagging_directive == Directive::kCopy) {
      return error::Error(
          "COPY tagging directive is not applicable to source object size "
          "greater than 5 GiB");
    }

    ComposeSource src;
    src.extra_headers = args.source.extra_headers;
    src.extra_query_params = args.source.extra_query_params;
    src.bucket = args.source.bucket;
    src.region = args.source.region;
    src.object = args.source.object;
    src.ssec = args.source.ssec;
    src.offset = args.source.offset;
    src.length = args.source.length;
    src.match_etag = args.source.match_etag;
    src.not_match_etag = args.source.not_match_etag;
    src.modified_since = args.source.modified_since;
    src.unmodified_since = args.source.unmodified_since;

    ComposeObjectArgs coargs;
    coargs.extra_headers = args.extra_headers;
    coargs.extra_query_params = args.extra_query_params;
    coargs.bucket = args.bucket;
    coargs.region = args.region;
    coargs.object = args.object;
    coargs.sse = args.sse;
    coargs.sources.push_back(src);

    return ComposeObject(coargs);
  }

  utils::Multimap headers;
  headers.AddAll(args.extra_headers);
  headers.AddAll(args.Headers());
  if (args.metadata_directive != NULL) {
    headers.Add("x-amz-metadata-directive",
                DirectiveToString(*args.metadata_directive));
  }
  if (args.tagging_directive != NULL) {
    headers.Add("x-amz-tagging-directive",
                DirectiveToString(*args.tagging_directive));
  }
  headers.AddAll(args.source.CopyHeaders());

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kPut, region, base_url_);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.query_params = args.extra_query_params;
  req.headers = headers;

  Response response = Execute(req);
  if (!response) return response;

  CopyObjectResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');
  resp.version_id = response.headers.GetFront("x-amz-version-id");

  return resp;
}

minio::s3::StatObjectResponse minio::s3::Client::CalculatePartCount(
    size_t& part_count, std::list<ComposeSource> sources) {
  size_t object_size = 0;
  int i = 0;
  for (auto& source : sources) {
    if (source.ssec != NULL && !base_url_.is_https) {
      std::string msg = "source " + source.bucket + "/" + source.object;
      if (!source.version_id.empty()) msg += "?versionId=" + source.version_id;
      msg += ": SSE-C operation must be performed over a secure connection";
      return error::Error(msg);
    }

    i++;

    std::string etag;
    size_t size;

    StatObjectArgs soargs;
    soargs.extra_headers = source.extra_headers;
    soargs.extra_query_params = source.extra_query_params;
    soargs.bucket = source.bucket;
    soargs.region = source.region;
    soargs.object = source.object;
    soargs.version_id = source.version_id;
    soargs.ssec = source.ssec;
    StatObjectResponse resp = StatObject(soargs);
    if (!resp) return resp;
    etag = resp.etag;
    size = resp.size;
    if (error::Error err = source.BuildHeaders(size, etag)) return err;

    if (source.length != NULL) {
      size = *source.length;
    } else if (source.offset != NULL) {
      size -= *source.offset;
    }

    if (size < utils::kMinPartSize && sources.size() != 1 &&
        i != sources.size()) {
      std::string msg = "source " + source.bucket + "/" + source.object;
      if (!source.version_id.empty()) msg += "?versionId=" + source.version_id;
      msg += ": size " + std::to_string(size) + " must be greater than " +
             std::to_string(utils::kMinPartSize);
      return error::Error(msg);
    }

    object_size += size;
    if (object_size > utils::kMaxObjectSize) {
      return error::Error("destination object size must be less than " +
                          std::to_string(utils::kMaxObjectSize));
    }

    if (size > utils::kMaxPartSize) {
      size_t count = size / utils::kMaxPartSize;
      size_t last_part_size = size - (count * utils::kMaxPartSize);
      if (last_part_size > 0) {
        count++;
      } else {
        last_part_size = utils::kMaxPartSize;
      }

      if (last_part_size < utils::kMinPartSize && sources.size() != 1 &&
          i != sources.size()) {
        std::string msg = "source " + source.bucket + "/" + source.object;
        if (!source.version_id.empty()) {
          msg += "?versionId=" + source.version_id;
        }
        msg += ": size " + std::to_string(size) +
               " for multipart split upload of " + std::to_string(size) +
               ", last part size is less than " +
               std::to_string(utils::kMinPartSize);
        return error::Error(msg);
      }

      part_count += count;
    } else {
      part_count++;
    }

    if (part_count > utils::kMaxMultipartCount) {
      return error::Error(
          "Compose sources create more than allowed multipart count " +
          std::to_string(utils::kMaxMultipartCount));
    }
  }

  return error::SUCCESS;
}

minio::s3::ComposeObjectResponse minio::s3::Client::ComposeObject(
    ComposeObjectArgs args, std::string& upload_id) {
  size_t part_count = 0;
  {
    StatObjectResponse resp = CalculatePartCount(part_count, args.sources);
    if (!resp) return resp;
  }

  ComposeSource& source = args.sources.front();
  if (part_count == 1 && source.offset == NULL && source.length == NULL) {
    CopySource src;
    src.extra_headers = source.extra_headers;
    src.extra_query_params = source.extra_query_params;
    src.bucket = source.bucket;
    src.region = source.region;
    src.object = source.object;
    src.ssec = source.ssec;
    src.offset = source.offset;
    src.length = source.length;
    src.match_etag = source.match_etag;
    src.not_match_etag = source.not_match_etag;
    src.modified_since = source.modified_since;
    src.unmodified_since = source.unmodified_since;

    CopyObjectArgs coargs;
    coargs.extra_headers = args.extra_headers;
    coargs.extra_query_params = args.extra_query_params;
    coargs.bucket = args.bucket;
    coargs.region = args.region;
    coargs.object = args.object;
    coargs.sse = args.sse;
    coargs.source = src;

    return CopyObject(coargs);
  }

  utils::Multimap headers = args.Headers();

  {
    CreateMultipartUploadArgs cmu_args;
    cmu_args.extra_query_params = args.extra_query_params;
    cmu_args.bucket = args.bucket;
    cmu_args.region = args.region;
    cmu_args.object = args.object;
    cmu_args.headers = headers;
    if (CreateMultipartUploadResponse resp = CreateMultipartUpload(cmu_args)) {
      upload_id = resp.upload_id;
    } else {
      return resp;
    }
  }

  unsigned int part_number = 0;
  utils::Multimap ssecheaders;
  if (args.sse != NULL) {
    if (SseCustomerKey* ssec = dynamic_cast<SseCustomerKey*>(args.sse)) {
      ssecheaders = ssec->Headers();
    }
  }

  std::list<Part> parts;
  for (auto& source : args.sources) {
    size_t size = source.ObjectSize();
    if (source.length != NULL) {
      size = *source.length;
    } else if (source.offset != NULL) {
      size -= *source.offset;
    }

    size_t offset = 0;
    if (source.offset != NULL) offset = *source.offset;

    utils::Multimap headers;
    headers.AddAll(source.Headers());
    headers.AddAll(ssecheaders);

    if (size <= utils::kMaxPartSize) {
      part_number++;
      if (source.length != NULL) {
        headers.Add("x-amz-copy-source-range",
                    "bytes=" + std::to_string(offset) + "-" +
                        std::to_string(offset + *source.length - 1));
      } else if (source.offset != NULL) {
        headers.Add("x-amz-copy-source-range",
                    "bytes=" + std::to_string(offset) + "-" +
                        std::to_string(offset + size - 1));
      }

      UploadPartCopyArgs upc_args;
      upc_args.bucket = args.bucket;
      upc_args.region = args.region;
      upc_args.object = args.object;
      upc_args.headers = headers;
      upc_args.upload_id = upload_id;
      upc_args.part_number = part_number;
      UploadPartCopyResponse resp = UploadPartCopy(upc_args);
      if (!resp) return resp;
      parts.push_back(Part{part_number, resp.etag});
    } else {
      while (size > 0) {
        part_number++;

        size_t start_bytes = offset;
        size_t end_bytes = start_bytes + utils::kMaxPartSize;
        if (size < utils::kMaxPartSize) end_bytes = start_bytes + size;

        utils::Multimap headerscopy;
        headerscopy.AddAll(headers);
        headerscopy.Add("x-amz-copy-source-range",
                        "bytes=" + std::to_string(start_bytes) + "-" +
                            std::to_string(end_bytes));

        UploadPartCopyArgs upc_args;
        upc_args.bucket = args.bucket;
        upc_args.region = args.region;
        upc_args.object = args.object;
        upc_args.headers = headerscopy;
        upc_args.upload_id = upload_id;
        upc_args.part_number = part_number;
        UploadPartCopyResponse resp = UploadPartCopy(upc_args);
        if (!resp) return resp;
        parts.push_back(Part{part_number, resp.etag});

        offset = start_bytes;
        size -= (end_bytes - start_bytes);
      }
    }
  }

  CompleteMultipartUploadArgs cmu_args;
  cmu_args.bucket = args.bucket;
  cmu_args.region = args.region;
  cmu_args.object = args.object;
  cmu_args.upload_id = upload_id;
  cmu_args.parts = parts;
  return CompleteMultipartUpload(cmu_args);
}

minio::s3::ComposeObjectResponse minio::s3::Client::ComposeObject(
    ComposeObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.sse != NULL && args.sse->TlsRequired() && !base_url_.is_https) {
    return error::Error(
        "SSE operation must be performed over a secure connection");
  }

  std::string upload_id;
  ComposeObjectResponse resp = ComposeObject(args, upload_id);
  if (!resp && !upload_id.empty()) {
    AbortMultipartUploadArgs amu_args;
    amu_args.bucket = args.bucket;
    amu_args.region = args.region;
    amu_args.object = args.object;
    amu_args.upload_id = upload_id;
    AbortMultipartUpload(amu_args);
  }

  return resp;
}

minio::s3::UploadObjectResponse minio::s3::Client::UploadObject(
    UploadObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  std::ifstream file;
  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  try {
    file.open(args.filename);
  } catch (std::system_error& err) {
    return error::Error("unable to open file " + args.filename + "; " +
                        err.code().message());
  }

  PutObjectArgs po_args(file, args.object_size, args.part_size);
  po_args.extra_headers = args.extra_headers;
  po_args.extra_query_params = args.extra_query_params;
  po_args.bucket = args.bucket;
  po_args.region = args.region;
  po_args.object = args.object;
  po_args.headers = args.headers;
  po_args.user_metadata = args.user_metadata;
  po_args.sse = args.sse;
  po_args.tags = args.tags;
  po_args.retention = args.retention;
  po_args.legal_hold = args.legal_hold;
  po_args.part_count = args.part_count;
  po_args.content_type = args.content_type;

  PutObjectResponse resp = PutObject(po_args);
  file.close();
  return resp;
}
