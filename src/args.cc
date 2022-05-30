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

#include "args.h"

minio::error::Error minio::s3::BucketArgs::Validate() {
  return utils::CheckBucketName(bucket);
}

minio::error::Error minio::s3::ObjectArgs::Validate() {
  if (error::Error err = BucketArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(object)) {
    return error::Error("object name cannot be empty");
  }

  return error::SUCCESS;
}

minio::utils::Multimap minio::s3::ObjectWriteArgs::Headers() {
  utils::Multimap headers;
  headers.AddAll(extra_headers);
  headers.AddAll(headers);
  headers.AddAll(user_metadata);

  if (sse != NULL) headers.AddAll(sse->Headers());

  std::string tagging;
  for (auto& [key, value] : tags) {
    std::string tag = curlpp::escape(key) + "=" + curlpp::escape(value);
    if (!tagging.empty()) tagging += "&";
    tagging += tag;
  }
  if (!tagging.empty()) headers.Add("x-amz-tagging", tagging);

  if (retention != NULL) {
    headers.Add("x-amz-object-lock-mode",
                RetentionModeToString(retention->mode));
    headers.Add("x-amz-object-lock-retain-until-date",
                retention->retain_until_date.ToISO8601UTC());
  }

  if (legal_hold) headers.Add("x-amz-object-lock-legal-hold", "ON");

  return headers;
}

minio::utils::Multimap minio::s3::ObjectConditionalReadArgs::Headers() {
  size_t* off = offset;
  size_t* len = length;

  size_t zero = 0;
  if (len != NULL && off == NULL) {
    off = &zero;
  }

  std::string range;
  if (off != NULL) {
    range = "bytes=" + std::to_string(*off) + "-";
    if (len != NULL) {
      range += std::to_string(*off + *len - 1);
    }
  }

  utils::Multimap headers;
  if (!range.empty()) headers.Add("Range", range);
  if (!match_etag.empty()) headers.Add("if-match", match_etag);
  if (!not_match_etag.empty()) headers.Add("if-none-match", not_match_etag);
  if (modified_since) {
    headers.Add("if-modified-since", modified_since.ToHttpHeaderValue());
  }
  if (unmodified_since) {
    headers.Add("if-unmodified-since", unmodified_since.ToHttpHeaderValue());
  }
  if (ssec != NULL) headers.AddAll(ssec->Headers());

  return headers;
}

minio::utils::Multimap minio::s3::ObjectConditionalReadArgs::CopyHeaders() {
  utils::Multimap headers;

  std::string copy_source = curlpp::escape("/" + bucket + "/" + object);
  if (!version_id.empty()) {
    copy_source += "?versionId=" + curlpp::escape(version_id);
  }

  headers.Add("x-amz-copy-source", copy_source);

  if (ssec != NULL) headers.AddAll(ssec->CopyHeaders());
  if (!match_etag.empty()) {
    headers.Add("x-amz-copy-source-if-match", match_etag);
  }
  if (!not_match_etag.empty()) {
    headers.Add("x-amz-copy-source-if-none-match", not_match_etag);
  }
  if (modified_since) {
    headers.Add("x-amz-copy-source-if-modified-since",
                modified_since.ToHttpHeaderValue());
  }
  if (unmodified_since) {
    headers.Add("x-amz-copy-source-if-unmodified-since",
                unmodified_since.ToHttpHeaderValue());
  }

  return headers;
}

minio::error::Error minio::s3::MakeBucketArgs::Validate() {
  return utils::CheckBucketName(bucket, true);
}

minio::error::Error minio::s3::AbortMultipartUploadArgs::Validate() {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::CompleteMultipartUploadArgs::Validate() {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::UploadPartArgs::Validate() {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }
  if (part_number < 1 || part_number > 10000) {
    return error::Error("part number must be between 1 and 10000");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::UploadPartCopyArgs::Validate() {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(upload_id)) {
    return error::Error("upload ID cannot be empty");
  }
  if (part_number < 1 || part_number > 10000) {
    return error::Error("part number must be between 1 and 10000");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::DownloadObjectArgs::Validate() {
  if (error::Error err = ObjectReadArgs::Validate()) return err;
  if (!utils::CheckNonEmptyString(filename)) {
    return error::Error("filename cannot be empty");
  }

  if (!overwrite && std::filesystem::exists(filename)) {
    return error::Error("file " + filename + " already exists");
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::GetObjectArgs::Validate() {
  if (error::Error err = ObjectConditionalReadArgs::Validate()) return err;
  if (datafunc == NULL) {
    return error::Error("data callback must be set");
  }

  return error::SUCCESS;
}

minio::s3::ListObjectsV1Args::ListObjectsV1Args() {}

minio::s3::ListObjectsV1Args::ListObjectsV1Args(ListObjectsArgs args) {
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

minio::s3::ListObjectsV2Args::ListObjectsV2Args(ListObjectsArgs args) {
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

minio::error::Error minio::s3::CopyObjectArgs::Validate() {
  if (error::Error err = ObjectArgs::Validate()) return err;
  if (error::Error err = source.Validate()) return err;

  if (source.offset != NULL || source.length != NULL) {
    if (metadata_directive != NULL && *metadata_directive == Directive::kCopy) {
      return error::Error(
          "COPY metadata directive is not applicable to source object with "
          "range");
    }

    if (tagging_directive != NULL && *tagging_directive == Directive::kCopy) {
      return error::Error(
          "COPY tagging directive is not applicable to source object with "
          "range");
    }
  }

  return error::SUCCESS;
}

minio::error::Error minio::s3::ComposeSource::BuildHeaders(size_t object_size,
                                                           std::string& etag) {
  std::string msg = "source " + bucket + "/" + object;
  if (!version_id.empty()) msg += "?versionId=" + version_id;
  msg += ": ";

  if (offset != NULL && *offset >= object_size) {
    return error::Error(msg + "offset " + std::to_string(*offset) +
                        " is beyond object size " +
                        std::to_string(object_size));
  }

  if (length != NULL) {
    if (*length > object_size) {
      return error::Error(msg + "length " + std::to_string(*length) +
                          " is beyond object size " +
                          std::to_string(object_size));
    }

    size_t off = 0;
    if (offset != NULL) off = *offset;
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

size_t minio::s3::ComposeSource::ObjectSize() {
  if (object_size_ == -1) {
    std::cerr << "ABORT: ComposeSource::BuildHeaders() must be called prior to "
                 "this method invocation. This shoud not happen."
              << std::endl;
    std::terminate();
  }

  return object_size_;
}

minio::utils::Multimap minio::s3::ComposeSource::Headers() {
  if (!headers_) {
    std::cerr << "ABORT: ComposeSource::BuildHeaders() must be called prior to "
                 "this method invocation. This shoud not happen."
              << std::endl;
    std::terminate();
  }

  return headers_;
}

minio::error::Error minio::s3::ComposeObjectArgs::Validate() {
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
