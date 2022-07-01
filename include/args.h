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

#ifndef _MINIO_S3_ARGS_H
#define _MINIO_S3_ARGS_H

#include <filesystem>
#include <nlohmann/json.hpp>

#include "http.h"
#include "signer.h"
#include "sse.h"
#include "types.h"

namespace minio {
namespace s3 {
struct BaseArgs {
  utils::Multimap extra_headers;
  utils::Multimap extra_query_params;
};  // struct BaseArgs

struct BucketArgs : public BaseArgs {
  std::string bucket;
  std::string region;

  error::Error Validate();
};  // struct BucketArgs

struct ObjectArgs : public BucketArgs {
  std::string object;

  error::Error Validate();
};  // struct ObjectArgs

struct ObjectWriteArgs : public ObjectArgs {
  utils::Multimap headers;
  utils::Multimap user_metadata;
  Sse *sse = NULL;
  std::map<std::string, std::string> tags;
  Retention *retention = NULL;
  bool legal_hold = false;

  utils::Multimap Headers();
};  // struct ObjectWriteArgs

struct ObjectVersionArgs : public ObjectArgs {
  std::string version_id;
};  // struct ObjectVersionArgs

struct ObjectReadArgs : public ObjectVersionArgs {
  SseCustomerKey *ssec = NULL;
};  // struct ObjectReadArgs

struct ObjectConditionalReadArgs : public ObjectReadArgs {
  size_t *offset = NULL;
  size_t *length = NULL;
  std::string match_etag;
  std::string not_match_etag;
  utils::Time modified_since;
  utils::Time unmodified_since;

  utils::Multimap Headers();
  utils::Multimap CopyHeaders();
};  // struct ObjectConditionalReadArgs

struct MakeBucketArgs : public BucketArgs {
  bool object_lock = false;

  error::Error Validate();
};  // struct MakeBucketArgs

using ListBucketsArgs = BaseArgs;

using BucketExistsArgs = BucketArgs;

using RemoveBucketArgs = BucketArgs;

struct AbortMultipartUploadArgs : public ObjectArgs {
  std::string upload_id;

  error::Error Validate();
};  // struct AbortMultipartUploadArgs

struct CompleteMultipartUploadArgs : public ObjectArgs {
  std::string upload_id;
  std::list<Part> parts;

  error::Error Validate();
};  // struct CompleteMultipartUploadArgs

struct CreateMultipartUploadArgs : public ObjectArgs {
  utils::Multimap headers;
};  // struct CreateMultipartUploadArgs

struct PutObjectBaseArgs : public ObjectWriteArgs {
  long object_size = -1;
  size_t part_size = 0;
  long part_count = 0;
  std::string content_type;
};  // struct PutObjectBaseArgs

struct PutObjectApiArgs : public PutObjectBaseArgs {
  std::string_view data;
  utils::Multimap query_params;
};  // struct PutObjectApiArgs

struct UploadPartArgs : public ObjectWriteArgs {
  std::string upload_id;
  unsigned int part_number;
  std::string_view data;

  error::Error Validate();
};  // struct UploadPartArgs

struct UploadPartCopyArgs : public ObjectWriteArgs {
  std::string upload_id;
  unsigned int part_number;
  utils::Multimap headers;

  error::Error Validate();
};  // struct UploadPartCopyArgs

using StatObjectArgs = ObjectConditionalReadArgs;

using RemoveObjectArgs = ObjectVersionArgs;

struct DownloadObjectArgs : public ObjectReadArgs {
  std::string filename;
  bool overwrite;

  error::Error Validate();
};  // struct DownloadObjectArgs

struct GetObjectArgs : public ObjectConditionalReadArgs {
  http::DataFunction datafunc;
  void *userdata = NULL;

  error::Error Validate();
};  // struct GetObjectArgs

struct ListObjectsArgs : public BucketArgs {
  std::string delimiter;
  bool use_url_encoding_type = true;
  std::string marker;       // only for ListObjectsV1.
  std::string start_after;  // only for ListObjectsV2.
  std::string key_marker;   // only for GetObjectVersions.
  unsigned int max_keys = 1000;
  std::string prefix;
  std::string continuation_token;      // only for ListObjectsV2.
  bool fetch_owner = false;            // only for ListObjectsV2.
  std::string version_id_marker;       // only for GetObjectVersions.
  bool include_user_metadata = false;  // MinIO extension for ListObjectsV2.
  bool recursive = false;
  bool use_api_v1 = false;
  bool include_versions = false;
};  // struct ListObjectsArgs

struct ListObjectsCommonArgs : public BucketArgs {
  std::string delimiter;
  std::string encoding_type;
  unsigned int max_keys = 1000;
  std::string prefix;
};  // struct ListObjectsCommonArgs

struct ListObjectsV1Args : public ListObjectsCommonArgs {
  std::string marker;

  ListObjectsV1Args();
  ListObjectsV1Args(ListObjectsArgs args);
};  // struct ListObjectsV1Args

struct ListObjectsV2Args : public ListObjectsCommonArgs {
  std::string start_after;
  std::string continuation_token;
  bool fetch_owner;
  bool include_user_metadata;

  ListObjectsV2Args();
  ListObjectsV2Args(ListObjectsArgs args);
};  // struct ListObjectsV2Args

struct ListObjectVersionsArgs : public ListObjectsCommonArgs {
  std::string key_marker;
  std::string version_id_marker;

  ListObjectVersionsArgs();
  ListObjectVersionsArgs(ListObjectsArgs args);
};  // struct ListObjectVersionsArgs

struct PutObjectArgs : public PutObjectBaseArgs {
  std::istream &stream;

  PutObjectArgs(std::istream &stream, long object_size, long part_size);
  error::Error Validate();
};  // struct PutObjectArgs

using CopySource = ObjectConditionalReadArgs;

struct CopyObjectArgs : public ObjectWriteArgs {
  CopySource source;
  Directive *metadata_directive = NULL;
  Directive *tagging_directive = NULL;

  error::Error Validate();
};  // struct CopyObjectArgs

struct ComposeSource : public ObjectConditionalReadArgs {
  error::Error BuildHeaders(size_t object_size, std::string &etag);
  size_t ObjectSize();
  utils::Multimap Headers();

 private:
  long object_size_ = -1;
  utils::Multimap headers_;
};  // struct ComposeSource

struct ComposeObjectArgs : public ObjectWriteArgs {
  std::list<ComposeSource> sources;

  error::Error Validate();
};  // struct ComposeObjectArgs

struct UploadObjectArgs : public PutObjectBaseArgs {
  std::string filename;

  error::Error Validate();
};  // struct PutObjectArgs

struct RemoveObjectsApiArgs : public BucketArgs {
  bool bypass_governance_mode = false;
  bool quiet = true;
  std::list<DeleteObject> objects;
};  // struct RemoveObjectsApiArgs

using DeleteObjectFunction = std::function<bool(DeleteObject &)>;

struct RemoveObjectsArgs : public BucketArgs {
  bool bypass_governance_mode = false;
  DeleteObjectFunction func = NULL;

  error::Error Validate();
};  // struct RemoveObjectsArgs

struct SelectObjectContentArgs : public ObjectReadArgs {
  SelectRequest &request;
  SelectResultFunction resultfunc = NULL;

  SelectObjectContentArgs(SelectRequest &req, SelectResultFunction func)
      : request(req), resultfunc(func) {}
  error::Error Validate();
};  // struct SelectObjectContentArgs

struct ListenBucketNotificationArgs : public BucketArgs {
  std::string prefix;
  std::string suffix;
  std::list<std::string> events;
  NotificationRecordsFunction func = NULL;

  error::Error Validate();
};  // struct ListenBucketNotificationArgs

using DeleteBucketPolicyArgs = BucketArgs;

using GetBucketPolicyArgs = BucketArgs;

struct SetBucketPolicyArgs : public BucketArgs {
  std::string policy;

  error::Error Validate();
};  // struct SetBucketPolicy

using DeleteBucketNotificationArgs = BucketArgs;

using GetBucketNotificationArgs = BucketArgs;

struct SetBucketNotificationArgs : public BucketArgs {
  NotificationConfig &config;

  SetBucketNotificationArgs(NotificationConfig &configvalue)
      : config(configvalue) {}
};  // struct SetBucketNotification

using DeleteBucketEncryptionArgs = BucketArgs;

using GetBucketEncryptionArgs = BucketArgs;

struct SetBucketEncryptionArgs : public BucketArgs {
  SseConfig &config;

  SetBucketEncryptionArgs(SseConfig &sseconfig) : config(sseconfig) {}
  error::Error Validate();
};  // struct SetBucketEncryption

using GetBucketVersioningArgs = BucketArgs;

struct SetBucketVersioningArgs : public BucketArgs {
  Boolean status;
  Boolean mfa_delete;

  error::Error Validate();
};  // struct SetBucketVersioning

using DeleteBucketReplicationArgs = BucketArgs;

using GetBucketReplicationArgs = BucketArgs;

struct SetBucketReplicationArgs : public BucketArgs {
  ReplicationConfig &config;

  SetBucketReplicationArgs(ReplicationConfig &value) : config(value) {}
};  // struct SetBucketReplication

using DeleteBucketLifecycleArgs = BucketArgs;

using GetBucketLifecycleArgs = BucketArgs;

struct SetBucketLifecycleArgs : public BucketArgs {
  LifecycleConfig &config;

  SetBucketLifecycleArgs(LifecycleConfig &value) : config(value) {}
};  // struct SetBucketLifecycle

using DeleteBucketTagsArgs = BucketArgs;

using GetBucketTagsArgs = BucketArgs;

struct SetBucketTagsArgs : public BucketArgs {
  std::map<std::string, std::string> tags;

  error::Error Validate();
};  // struct SetBucketTags

using DeleteObjectLockConfigArgs = BucketArgs;

using GetObjectLockConfigArgs = BucketArgs;

struct SetObjectLockConfigArgs : public BucketArgs {
  ObjectLockConfig config;

  error::Error Validate();
};  // struct SetObjectLockConfig

using DeleteObjectTagsArgs = ObjectVersionArgs;

using GetObjectTagsArgs = ObjectVersionArgs;

struct SetObjectTagsArgs : public ObjectVersionArgs {
  std::map<std::string, std::string> tags;

  error::Error Validate();
};  // struct SetObjectTags

using EnableObjectLegalHoldArgs = ObjectVersionArgs;

using DisableObjectLegalHoldArgs = ObjectVersionArgs;

using IsObjectLegalHoldEnabledArgs = ObjectVersionArgs;

using GetObjectRetentionArgs = ObjectVersionArgs;

struct SetObjectRetentionArgs : public ObjectVersionArgs {
  RetentionMode retention_mode;
  utils::Time retain_until_date;

  error::Error Validate();
};  // struct SetObjectRetention

inline constexpr unsigned int kDefaultExpirySeconds =
    (60 * 60 * 24 * 7);  // 7 days

struct GetPresignedObjectUrlArgs : public ObjectVersionArgs {
  http::Method method;
  unsigned int expiry_seconds = kDefaultExpirySeconds;
  utils::Time request_time;

  error::Error Validate();
};  // struct GetPresignedObjectUrlArgs

struct PostPolicy {
  std::string bucket;
  std::string region;

  PostPolicy(std::string bucket, utils::Time expiration) {
    this->bucket = bucket;
    this->expiration_ = expiration;
  }

  operator bool() const { return !bucket.empty() && !expiration_; }

  error::Error AddEqualsCondition(std::string element, std::string value) {
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

    conditions_[eq_][element] = value;
    return error::SUCCESS;
  }

  error::Error RemoveEqualsCondition(std::string element) {
    if (element.empty()) {
      return error::Error("condition element cannot be empty");
    }
    conditions_[eq_].erase(element);
    return error::SUCCESS;
  }

  error::Error AddStartsWithCondition(std::string element, std::string value) {
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

  error::Error RemoveStartsWithCondition(std::string element) {
    if (element.empty()) {
      return error::Error("condition element cannot be empty");
    }
    conditions_[starts_with_].erase(element);
    return error::SUCCESS;
  }

  error::Error AddContentLengthRangeCondition(size_t lower_limit,
                                              size_t upper_limit) {
    if (lower_limit > upper_limit) {
      return error::Error("lower limit cannot be greater than upper limit");
    }
    lower_limit_ = Integer(lower_limit);
    upper_limit_ = Integer(upper_limit);
    return error::SUCCESS;
  }

  void RemoveContentLengthRangeCondition() {
    lower_limit_ = Integer();
    upper_limit_ = Integer();
  }

  error::Error FormData(std::map<std::string, std::string> &data,
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

 private:
  static constexpr const char *eq_ = "eq";
  static constexpr const char *starts_with_ = "starts-with";
  static constexpr const char *algorithm_ = "AWS4-HMAC-SHA256";

  utils::Time expiration_;
  std::map<std::string, std::map<std::string, std::string>> conditions_;
  Integer lower_limit_;
  Integer upper_limit_;

  static std::string trimDollar(std::string value) {
    if (value.front() == '$') value.erase(0, 1);
    return value;
  }

  static std::string getCredentialString(std::string access_key,
                                         utils::Time date, std::string region) {
    return access_key + "/" + date.ToSignerDate() + "/" + region +
           "/s3/aws4_request";
  }

  static bool isReservedElement(std::string element) {
    return element == "bucket" || element == "x-amz-algorithm" ||
           element == "x-amz-credential" || element == "x-amz-date" ||
           element == "policy" || element == "x-amz-signature";
  }
};  // struct PostPolicy
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_S3_ARGS_H
