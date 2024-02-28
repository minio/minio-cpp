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

#include <filesystem>
#include <nlohmann/json.hpp>
#include "args.h"

minio::error::Error minio::s3::BucketArgs::Validate() const {
  return utils::CheckBucketName(bucket);
}

minio::error::Error minio::s3::ObjectArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(object)) {
    return error::Error("object name cannot be empty");
  }

  return error::SUCCESS;
}

minio::utils::Multimap minio::s3::ObjectWriteArgs::Headers() const {
  utils::Multimap h;
  h.AddAll(extra_headers);
  h.AddAll(headers);
  h.AddAll(user_metadata);

  if (sse != nullptr) h.AddAll(sse->Headers());

  std::string tagging;
  for (auto& [key, value] : tags) {
    std::string tag = curlpp::escape(key) + "=" + curlpp::escape(value);
    if (!tagging.empty()) tagging += "&";
    tagging += tag;
  }
  if (!tagging.empty()) h.Add("x-amz-tagging", tagging);

  if (retention != nullptr) {
    h.Add("x-amz-object-lock-mode",
                RetentionModeToString(retention->mode));
    h.Add("x-amz-object-lock-retain-until-date",
                retention->retain_until_date.ToISO8601UTC());
  }

  if (legal_hold) h.Add("x-amz-object-lock-legal-hold", "ON");

  return h;
}

minio::utils::Multimap minio::s3::ObjectConditionalReadArgs::Headers() const {
  size_t* off = offset;
  size_t* len = length;

  size_t zero = 0;
  if (len != nullptr && off == nullptr) {
    off = &zero;
  }

  std::string range;
  if (off != nullptr) {
    range = "bytes=" + std::to_string(*off) + "-";
    if (len != nullptr) {
      range += std::to_string(*off + *len - 1);
    }
  }

  utils::Multimap h;
  if (!range.empty()) h.Add("Range", range);
  if (!match_etag.empty()) h.Add("if-match", match_etag);
  if (!not_match_etag.empty()) h.Add("if-none-match", not_match_etag);
  if (modified_since) {
    h.Add("if-modified-since", modified_since.ToHttpHeaderValue());
  }
  if (unmodified_since) {
    h.Add("if-unmodified-since", unmodified_since.ToHttpHeaderValue());
  }
  if (ssec != nullptr) h.AddAll(ssec->Headers());

  return h;
}

minio::utils::Multimap minio::s3::ObjectConditionalReadArgs::CopyHeaders() const {
  utils::Multimap h;

  std::string copy_source = curlpp::escape("/" + bucket + "/" + object);
  if (!version_id.empty()) {
    copy_source += "?versionId=" + curlpp::escape(version_id);
  }

  h.Add("x-amz-copy-source", copy_source);

  if (ssec != nullptr) h.AddAll(ssec->CopyHeaders());
  if (!match_etag.empty()) {
    h.Add("x-amz-copy-source-if-match", match_etag);
  }
  if (!not_match_etag.empty()) {
    h.Add("x-amz-copy-source-if-none-match", not_match_etag);
  }
  if (modified_since) {
    h.Add("x-amz-copy-source-if-modified-since",
                modified_since.ToHttpHeaderValue());
  }
  if (unmodified_since) {
    h.Add("x-amz-copy-source-if-unmodified-since",
                unmodified_since.ToHttpHeaderValue());
  }

  return h;
}

minio::error::Error minio::s3::MakeBucketArgs::Validate() const {
  return utils::CheckBucketName(bucket, true);
}

minio::error::Error minio::s3::AbortMultipartUploadArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::CompleteMultipartUploadArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::UploadPartArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }
  if (part_number < 1 || part_number > 10000) {
    return error::Error("part number must be between 1 and 10000");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::UploadPartCopyArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }
  if (part_number < 1 || part_number > 10000) {
    return error::Error("part number must be between 1 and 10000");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::DownloadObjectArgs::Validate() const {
  if (error::Error err = ObjectReadArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(filename)) {
    return error::Error("filename cannot be empty");
  }

  if (!overwrite && std::filesystem::exists(filename)) {
    return error::Error("file " + filename + " already exists");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::GetObjectArgs::Validate() const {
  if (error::Error err = ObjectConditionalReadArgs::Validate()) return err;
  if (datafunc == nullptr) {
    return error::Error("data callback must be set");
  }

  return error::SUCCESS;
}

minio::s3::ListObjectsV1Args::ListObjectsV1Args() {}

minio::s3::ListObjectsV1Args::ListObjectsV1Args(ListObjectsArgs args) { // PWTODO: why copy constructor is wrong?
  this->extra_headers = args.extra_headers;
  this->extra_query_params = args.extra_query_params;
  this->bucket = args.bucket;
  this->region = args.region;
  this->delimiter = args.delimiter;
  this->encoding_type = args.use_url_encoding_type ? "url" : "";
  this->max_keys = args.max_keys;
  this->prefix = args.prefix;
  this->marker = args.marker;
}

minio::s3::ListObjectsV2Args::ListObjectsV2Args() {}

minio::s3::ListObjectsV2Args::ListObjectsV2Args(ListObjectsArgs args) { // PWTODO: why copy constructor is wrong?
  this->extra_headers = args.extra_headers;
  this->extra_query_params = args.extra_query_params;
  this->bucket = args.bucket;
  this->region = args.region;
  this->delimiter = args.delimiter;
  this->encoding_type = args.use_url_encoding_type ? "url" : "";
  this->max_keys = args.max_keys;
  this->prefix = args.prefix;
  this->start_after = args.start_after;
  this->continuation_token = args.continuation_token;
  this->fetch_owner = args.fetch_owner;
  this->include_user_metadata = args.include_user_metadata;
}

minio::s3::ListObjectVersionsArgs::ListObjectVersionsArgs() {}

minio::s3::ListObjectVersionsArgs::ListObjectVersionsArgs(
    ListObjectsArgs args) {
  this->extra_headers = args.extra_headers;
  this->extra_query_params = args.extra_query_params;
  this->bucket = args.bucket;
  this->region = args.region;
  this->delimiter = args.delimiter;
  this->encoding_type = args.use_url_encoding_type ? "url" : "";
  this->max_keys = args.max_keys;
  this->prefix = args.prefix;
  this->key_marker = args.key_marker;
  this->version_id_marker = args.version_id_marker;
}

minio::s3::PutObjectArgs::PutObjectArgs(std::istream& istream, long object_size,
                                        long part_size)
    : stream(istream) {
  this->object_size = object_size;
  this->part_size = part_size;
}

minio::error::Error minio::s3::PutObjectArgs::Validate() {
  if (error::Error err = ObjectArgs::Validate()) return err;
  return utils::CalcPartInfo(object_size, part_size, part_count);
}

minio::error::Error minio::s3::CopyObjectArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (error::Error err = source.Validate()) return err;

  if (source.offset != nullptr || source.length != nullptr) {
    if (metadata_directive != nullptr && *metadata_directive == Directive::kCopy) {
      return error::Error(
          "COPY metadata directive is not applicable to source object with "
          "range");
    }

    if (tagging_directive != nullptr && *tagging_directive == Directive::kCopy) {
      return error::Error(
          "COPY tagging directive is not applicable to source object with "
          "range");
    }
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::ComposeSource::BuildHeaders(size_t object_size, std::string& etag) {
  std::string msg = "source " + bucket + "/" + object;
  if (!version_id.empty()) msg += "?versionId=" + version_id;
  msg += ": ";

  if (offset != nullptr && *offset >= object_size) {
    return error::Error(msg + "offset " + std::to_string(*offset) +
                        " is beyond object size " +
                        std::to_string(object_size));
  }

  if (length != nullptr) {
    if (*length > object_size) {
      return error::Error(msg + "length " + std::to_string(*length) +
                          " is beyond object size " +
                          std::to_string(object_size));
    }

    size_t off = 0;
    if (offset != nullptr) off = *offset;
    if ((off + *length) > object_size) {
      return error::Error(
          msg + "compose size " + std::to_string(off + *length) +
          " is beyond object size " + std::to_string(object_size));
    }
  }

  object_size_ = object_size;
  headers_ = CopyHeaders();
  if (!headers_.Contains("x-amz-copy-source-if-match")) {
    headers_.Add("x-amz-copy-source-if-match", etag);
  }

  return error::SUCCESS;
}

size_t minio::s3::ComposeSource::ObjectSize() const {
  if (object_size_ == -1) {
    std::cerr << "ABORT: ComposeSource::BuildHeaders() must be called prior to "
                 "this method invocation. This shoud not happen."
              << std::endl;
    std::terminate();
  }

  return object_size_;
}

minio::utils::Multimap minio::s3::ComposeSource::Headers() const {
  if (!headers_) {
    std::cerr << "ABORT: ComposeSource::BuildHeaders() must be called prior to "
                 "this method invocation. This shoud not happen."
              << std::endl;
    std::terminate();
  }

  return headers_;
}

minio::error::Error minio::s3::ComposeObjectArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (sources.empty()) return error::Error("compose sources cannot be empty");

  int i = 1;
  for (auto& source : sources) {
    if (error::Error err = source.Validate()) {
      return error::Error("source " + std::to_string(i) + ": " + err.String());
    }
    i++;
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::UploadObjectArgs::Validate() {
  if (error::Error err = ObjectArgs::Validate()) return err;

  if (!utils::CheckNonEmptyString(filename)) {
    return error::Error("filename cannot be empty");
  }

  if (!std::filesystem::exists(filename)) {
    return error::Error("file " + filename + " does not exist");
  }

  std::filesystem::path file_path = filename;
  size_t obj_size = std::filesystem::file_size(file_path);
  object_size = obj_size;
  return utils::CalcPartInfo(object_size, part_size, part_count);
}

minio::error::Error minio::s3::RemoveObjectsArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;
  if (func == nullptr) {
    return error::Error("delete object function must be set");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::SelectObjectContentArgs::Validate() const {
  if (error::Error err = ObjectReadArgs::Validate()) return err;

  if (!utils::CheckNonEmptyString(request.expr)) {
    return error::Error("SQL expression must not be empty");
  }

  if (!((request.csv_input != nullptr) ^ (request.json_input != nullptr) ^
        (request.parquet_input != nullptr))) {
    return error::Error(
        "One of CSV, JSON or Parquet input serialization must be set");
  }

  if (!((request.csv_output != nullptr) ^ (request.json_output != nullptr))) {
    return error::Error("One of CSV or JSON output serialization must be set");
  }

  if (resultfunc == nullptr) return error::Error("result function must be set");

  return error::SUCCESS;
}

minio::error::Error minio::s3::ListenBucketNotificationArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;
  if (func == nullptr) error::Error("notification records function must be set");

  return error::SUCCESS;
}

minio::error::Error minio::s3::SetBucketPolicyArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;

  if (!utils::CheckNonEmptyString(policy)) {
    return error::Error("bucket policy cannot be empty");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::SetBucketEncryptionArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;

  if (!config) {
    return error::Error("bucket encryption configuration cannot be empty");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::SetBucketVersioningArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;
  if (!status) return error::Error("versioning status must be set");

  return error::SUCCESS;
}

minio::error::Error minio::s3::SetBucketTagsArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;

  if (tags.size() > 50) {
    return error::Error("too many bucket tags; allowed = 50, found = " +
                        std::to_string(tags.size()));
  }

  for (auto& [key, value] : tags) {
    if (key.length() == 0 || key.length() > 128 || utils::Contains(key, "&")) {
      return error::Error("invalid tag key '" + key + "'");
    }

    if (value.length() > 256 || utils::Contains(value, "&")) {
      return error::Error("invalid tag value '" + value + "'");
    }
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::SetObjectLockConfigArgs::Validate() const {
  if (error::Error err = BucketArgs::Validate()) return err;
  return config.Validate();
}

minio::error::Error minio::s3::SetObjectTagsArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;

  if (tags.size() > 10) {
    return error::Error("too many object tags; allowed = 10, found = " +
                        std::to_string(tags.size()));
  }

  for (auto& [key, value] : tags) {
    if (key.length() == 0 || key.length() > 128 || utils::Contains(key, "&")) {
      return error::Error("invalid tag key '" + key + "'");
    }

    if (value.length() > 256 || utils::Contains(value, "&")) {
      return error::Error("invalid tag value '" + value + "'");
    }
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::SetObjectRetentionArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (IsRetentionModeValid(retention_mode)) {
    return error::Error("valid retention mode must be set");
  }
  if (!retain_until_date) {
    return error::Error("retention until date must be set");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::GetPresignedObjectUrlArgs::Validate() const {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (method < http::Method::kGet || method > http::Method::kDelete) {
    return error::Error("valid HTTP method must be provided");
  }
  if (expiry_seconds < 1 || expiry_seconds > kDefaultExpirySeconds) {
    return error::Error("expiry seconds must be between 1 and " +
                        std::to_string(kDefaultExpirySeconds));
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::PostPolicy::AddEqualsCondition(std::string element, std::string value) {
  if (element.empty()) {
    return error::Error("condition element cannot be empty");
  }

  element = trimDollar(element);
  if (element == "success_action_redirect" || element == "redirect" ||
      element == "content-length-range") {
    return error::Error(element + " is unsupported for equals condition");
  }

  if (isReservedElement(element)) {
    return error::Error(element + " cannot be set");
  }

  conditions_[eq_][element] = std::move(value);
  return error::SUCCESS;
}

minio::error::Error minio::s3::PostPolicy::RemoveEqualsCondition(std::string element) {
  if (element.empty()) {
    return error::Error("condition element cannot be empty");
  }
  conditions_[eq_].erase(element);
  return error::SUCCESS;
}

minio::error::Error minio::s3::PostPolicy::AddStartsWithCondition(std::string element, std::string value) {
  if (element.empty()) {
    return error::Error("condition element cannot be empty");
  }

  element = trimDollar(element);
  if (element == "success_action_status" ||
      element == "content-length-range" ||
      (utils::StartsWith(element, "x-amz-") &&
       utils::StartsWith(element, "x-amz-meta-"))) {
    return error::Error(element +
                        " is unsupported for starts-with condition");
  }

  if (isReservedElement(element)) {
    return error::Error(element + " cannot be set");
  }

  conditions_[starts_with_][element] = value;
  return error::SUCCESS;
}

minio::error::Error minio::s3::PostPolicy::RemoveStartsWithCondition(std::string element) {
  if (element.empty()) {
    return error::Error("condition element cannot be empty");
  }
  conditions_[starts_with_].erase(element);
  return error::SUCCESS;
}

minio::error::Error minio::s3::PostPolicy::AddContentLengthRangeCondition(size_t lower_limit,
                                            size_t upper_limit) {
  if (lower_limit > upper_limit) {
    return error::Error("lower limit cannot be greater than upper limit");
  }
  lower_limit_ = Integer(lower_limit);
  upper_limit_ = Integer(upper_limit);
  return error::SUCCESS;
}

void minio::s3::PostPolicy::RemoveContentLengthRangeCondition() {
  lower_limit_ = Integer();
  upper_limit_ = Integer();
}

minio::error::Error minio::s3::PostPolicy::FormData(std::map<std::string, std::string> &data,
                      std::string access_key, std::string secret_key,
                      std::string session_token, std::string region) {
  if (region.empty()) return error::Error("region cannot be empty");
  if (conditions_[eq_]["key"].empty() &&
      conditions_[starts_with_]["key"].empty()) {
    return error::Error("key condition must be set");
  }

  nlohmann::json policy;
  policy["expiration"] = expiration_.ToISO8601UTC();

  nlohmann::json conditions = nlohmann::json::array();
  conditions.push_back({eq_, "$bucket", bucket});
  for (auto &[cond_key, cond] : conditions_) {
    for (auto &[key, value] : cond) {
      conditions.push_back({cond_key, "$" + key, value});
    }
  }
  if (lower_limit_ && upper_limit_) {
    conditions.push_back(
        {"content-length-range", lower_limit_.Get(), upper_limit_.Get()});
  }
  utils::Time date = utils::Time::Now();
  std::string credential = getCredentialString(access_key, date, region);
  std::string amz_date = date.ToAmzDate();
  conditions.push_back({eq_, "$x-amz-algorithm", algorithm_});
  conditions.push_back({eq_, "$x-amz-credential", credential});
  if (!session_token.empty()) {
    conditions.push_back({eq_, "$x-amz-security-token", session_token});
  }
  conditions.push_back({eq_, "$x-amz-date", amz_date});
  policy["conditions"] = conditions;

  std::string encoded_policy = utils::Base64Encode(policy.dump());
  std::string signature =
      signer::PostPresignV4(encoded_policy, secret_key, date, region);

  data["x-amz-algorithm"] = algorithm_;
  data["x-amz-credential"] = credential;
  data["x-amz-date"] = amz_date;
  data["policy"] = encoded_policy;
  data["x-amz-signature"] = signature;
  if (!session_token.empty()) {
    data["x-amz-security-token"] = session_token;
  }

  return error::SUCCESS;
}

std::string minio::s3::PostPolicy::trimDollar(std::string value) {
  if (value.front() == '$') value.erase(0, 1);
  return value;
}

std::string minio::s3::PostPolicy::getCredentialString(std::string access_key,
                                       utils::Time date, std::string region) {
  return access_key + "/" + date.ToSignerDate() + "/" + region +
         "/s3/aws4_request";
}

bool minio::s3::PostPolicy::isReservedElement(std::string element) {
  return element == "bucket" || element == "x-amz-algorithm" ||
         element == "x-amz-credential" || element == "x-amz-date" ||
         element == "policy" || element == "x-amz-signature";
}
