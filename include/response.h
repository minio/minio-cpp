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

#ifndef _MINIO_S3_RESPONSE_H
#define _MINIO_S3_RESPONSE_H

#include <pugixml.hpp>

#include "types.h"

namespace minio {
namespace s3 {
struct Response {
  int status_code = 0;
  utils::Multimap headers;
  std::string data;

  std::string code;
  std::string message;
  std::string resource;
  std::string request_id;
  std::string host_id;
  std::string bucket_name;
  std::string object_name;

  Response();
  explicit Response(error::Error err) : err_(std::move(err)) {}

  Response(const Response& resp) = default;
  Response& operator=(const Response& resp) = default;

  Response(Response&& resp) = default;
  Response& operator=(Response&& resp) = default;

  ~Response();

  explicit operator bool() const {
    return !err_ && code.empty() && message.empty() &&
           (status_code == 0 || (status_code >= 200 && status_code <= 299));
  }

  error::Error Error() const;

  static Response ParseXML(std::string_view data, int status_code,
                           utils::Multimap headers);

 private:
  error::Error err_;
};  // struct Response

struct GetRegionResponse : public Response {
  std::string region;

  explicit GetRegionResponse(std::string region) : region(std::move(region)) {}

  explicit GetRegionResponse(error::Error err) : Response(std::move(err)) {}

  explicit GetRegionResponse(const Response& resp)
    : Response(resp) {}

  ~GetRegionResponse() = default;
};  // struct GetRegionResponse

struct MakeBucketResponse : public Response {
  MakeBucketResponse() = default;
  ~MakeBucketResponse() = default;

  explicit MakeBucketResponse(error::Error err) : Response(std::move(err)) {}

  explicit MakeBucketResponse(const Response& resp) : Response(resp) {}

  MakeBucketResponse& operator =(const Response& resp) {
    return static_cast<MakeBucketResponse&>(Response::operator =(resp));
  }
}; // struct MakeBucketResponse

struct ListBucketsResponse : public Response {
  std::list<Bucket> buckets;

  explicit ListBucketsResponse(std::list<Bucket> buckets)
      : buckets(std::move(buckets)) {}

  explicit ListBucketsResponse(error::Error err) : Response(std::move(err)) {}

  explicit ListBucketsResponse(const Response& resp)
    : Response(resp) {} 

  ~ListBucketsResponse() = default;

  static ListBucketsResponse ParseXML(std::string_view data);
};  // struct ListBucketsResponse

struct BucketExistsResponse : public Response {
  bool exist = false;

  explicit BucketExistsResponse(bool exist) : exist(exist) {}

  explicit BucketExistsResponse(error::Error err) : Response(std::move(err)) {}

  explicit BucketExistsResponse(const Response& resp)
    : Response(resp) {}

  ~BucketExistsResponse() = default;
};  // struct BucketExistsResponse

struct RemoveBucketResponse : public Response {
  RemoveBucketResponse() = default;
  ~RemoveBucketResponse() = default;

  explicit RemoveBucketResponse(error::Error err) : Response(std::move(err)) {}

  explicit RemoveBucketResponse(const Response& resp) : Response(resp) {}

  RemoveBucketResponse& operator =(const Response& resp) {
    return static_cast<RemoveBucketResponse&>(Response::operator =(resp));
  }
}; // struct RemoveBucketResponse

struct AbortMultipartUploadResponse : public Response {
  AbortMultipartUploadResponse() = default;
  ~AbortMultipartUploadResponse() = default;

  explicit AbortMultipartUploadResponse(error::Error err) : Response(std::move(err)) {}

  explicit AbortMultipartUploadResponse(const Response& resp) : Response(resp) {}

  AbortMultipartUploadResponse& operator =(const Response& resp) {
    return static_cast<AbortMultipartUploadResponse&>(Response::operator =(resp));
  }
}; // struct AbortMultipartUploadResponse

struct CompleteMultipartUploadResponse : public Response {
  std::string location;
  std::string etag;
  std::string version_id;

  CompleteMultipartUploadResponse() = default;

  explicit CompleteMultipartUploadResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit CompleteMultipartUploadResponse(const Response& resp)
    : Response(resp) {}

  ~CompleteMultipartUploadResponse() = default;

  static CompleteMultipartUploadResponse ParseXML(std::string_view data,
                                                  std::string version_id);
};  // struct CompleteMultipartUploadResponse

struct CreateMultipartUploadResponse : public Response {
  std::string upload_id;

  explicit CreateMultipartUploadResponse(std::string upload_id)
      : upload_id(std::move(upload_id)) {}

  explicit CreateMultipartUploadResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit CreateMultipartUploadResponse(const Response& resp)
    : Response(resp) {}

  ~CreateMultipartUploadResponse() = default;
};  // struct CreateMultipartUploadResponse

struct PutObjectResponse : public Response {
  std::string etag;
  std::string version_id;

  PutObjectResponse() = default;

  explicit PutObjectResponse(error::Error err) : Response(std::move(err)) {}

  explicit PutObjectResponse(const Response& resp)
    : Response(resp) {}

  explicit PutObjectResponse(const CompleteMultipartUploadResponse& resp)
      : Response(resp), etag(resp.etag), version_id(resp.version_id) {}

  ~PutObjectResponse() = default;
};  // struct PutObjectResponse

using UploadPartResponse = PutObjectResponse;

using UploadPartCopyResponse = PutObjectResponse;

struct StatObjectResponse : public Response {
  std::string version_id;
  std::string etag;
  size_t size = 0;
  utils::Time last_modified;
  RetentionMode retention_mode;
  utils::Time retention_retain_until_date;
  LegalHold legal_hold;
  bool delete_marker;
  utils::Multimap user_metadata;

  StatObjectResponse() = default;

  explicit StatObjectResponse(error::Error err) : Response(std::move(err)) {}

  explicit StatObjectResponse(const Response& resp)
    : Response(resp) {}

  ~StatObjectResponse() = default;
};  // struct StatObjectResponse

using RemoveObjectResponse = Response;

using DownloadObjectResponse = Response;

using GetObjectResponse = Response;

struct Item : public Response {
  std::string etag;  // except DeleteMarker
  std::string name;
  utils::Time last_modified;
  std::string owner_id;
  std::string owner_name;
  size_t size = 0;  // except DeleteMarker
  std::string storage_class;
  bool is_latest = false;  // except ListObjects V1/V2
  std::string version_id;  // except ListObjects V1/V2
  std::map<std::string, std::string> user_metadata;
  bool is_prefix = false;
  bool is_delete_marker = false;
  std::string encoding_type;

  Item() = default;

  explicit Item(error::Error err) : Response(std::move(err)) {}

  explicit Item(const Response& resp)
    : Response(resp) {}

  ~Item() = default;
};  // struct Item

struct ListObjectsResponse : public Response {
  // Common
  std::string name;
  std::string encoding_type;
  std::string prefix;
  std::string delimiter;
  bool is_truncated;
  unsigned int max_keys;
  std::list<Item> contents;

  // ListObjectsV1
  std::string marker;
  std::string next_marker;

  // ListObjectsV2
  unsigned int key_count;
  std::string start_after;
  std::string continuation_token;
  std::string next_continuation_token;

  // ListObjectVersions
  std::string key_marker;
  std::string next_key_marker;
  std::string version_id_marker;
  std::string next_version_id_marker;

  ListObjectsResponse() = default;

  explicit ListObjectsResponse(error::Error err) : Response(std::move(err)) {}

  explicit ListObjectsResponse(const Response& resp)
    : Response(resp) {}

  ~ListObjectsResponse() = default;

  static ListObjectsResponse ParseXML(std::string_view data, bool version);
};  // struct ListObjectsResponse

using CopyObjectResponse = PutObjectResponse;

using ComposeObjectResponse = PutObjectResponse;

using UploadObjectResponse = PutObjectResponse;

struct DeletedObject : public Response {
  std::string name;
  std::string version_id;
  bool delete_marker;
  std::string delete_marker_version_id;

  DeletedObject() = default;
  ~DeletedObject() = default;
};  // struct DeletedObject

struct DeleteError : public Response {
  std::string version_id;

  DeleteError() = default;

  explicit DeleteError(error::Error err) : Response(std::move(err)) {}

  explicit DeleteError(const Response& resp)
    : Response(resp) {}

  ~DeleteError() = default;
};  // struct DeleteError

struct RemoveObjectsResponse : public Response {
  std::list<DeletedObject> objects;
  std::list<DeleteError> errors;

  RemoveObjectsResponse() = default;

  explicit RemoveObjectsResponse(error::Error err) : Response(std::move(err)) {}

  explicit RemoveObjectsResponse(const Response& resp)
    : Response(resp) {}

  ~RemoveObjectsResponse() = default;

  static RemoveObjectsResponse ParseXML(std::string_view data);
};  // struct RemoveObjectsResponse

using SelectObjectContentResponse = Response;

using ListenBucketNotificationResponse = Response;

using DeleteBucketPolicyResponse = Response;

struct GetBucketPolicyResponse : public Response {
  std::string policy;

  explicit GetBucketPolicyResponse(std::string policy)
      : policy(std::move(policy)) {}

  explicit GetBucketPolicyResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetBucketPolicyResponse(const Response& resp)
    : Response(resp) {}

  ~GetBucketPolicyResponse() = default;
};  // struct GetBucketPolicyResponse

using SetBucketPolicyResponse = Response;

using DeleteBucketNotificationResponse = Response;

struct GetBucketNotificationResponse : public Response {
  NotificationConfig config;

  explicit GetBucketNotificationResponse(NotificationConfig config)
      : config(std::move(config)) {}

  explicit GetBucketNotificationResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetBucketNotificationResponse(const Response& resp)
    : Response(resp) {}

  ~GetBucketNotificationResponse() = default;

  static GetBucketNotificationResponse ParseXML(std::string_view data);
};  // struct GetBucketNotificationResponse

using SetBucketNotificationResponse = Response;

using DeleteBucketEncryptionResponse = Response;

struct GetBucketEncryptionResponse : public Response {
  SseConfig config;

  explicit GetBucketEncryptionResponse(SseConfig config)
      : config(std::move(config)) {}

  explicit GetBucketEncryptionResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetBucketEncryptionResponse(const Response& resp)
    : Response(resp) {}

  ~GetBucketEncryptionResponse() = default;

  static GetBucketEncryptionResponse ParseXML(std::string_view data);
};  // struct GetBucketEncryptionResponse

using SetBucketEncryptionResponse = Response;

struct GetBucketVersioningResponse : public Response {
  Boolean status;
  Boolean mfa_delete;

  GetBucketVersioningResponse() = default;

  explicit GetBucketVersioningResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetBucketVersioningResponse(const Response& resp)
    : Response(resp) {}

  ~GetBucketVersioningResponse() = default;

  std::string Status() const {
    if (!status) return "Off";
    return status.Get() ? "Enabled" : "Suspended";
  }

  std::string MfaDelete() const {
    if (!mfa_delete) {
      return {};
    }
    return mfa_delete.Get() ? "Enabled" : "Disabled";
  }
};  // struct GetBucketVersioningResponse

using SetBucketVersioningResponse = Response;

using DeleteBucketReplicationResponse = Response;

struct GetBucketReplicationResponse : public Response {
  ReplicationConfig config;

  explicit GetBucketReplicationResponse(ReplicationConfig config)
      : config(std::move(config)) {}

  explicit GetBucketReplicationResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetBucketReplicationResponse(const Response& resp)
    : Response(resp) {}

  ~GetBucketReplicationResponse() = default;

  static GetBucketReplicationResponse ParseXML(std::string_view data);
};  // struct GetBucketReplicationResponse

using SetBucketReplicationResponse = Response;

using DeleteBucketLifecycleResponse = Response;

struct GetBucketLifecycleResponse : public Response {
  LifecycleConfig config;

  explicit GetBucketLifecycleResponse(LifecycleConfig config)
      : config(std::move(config)) {}

  explicit GetBucketLifecycleResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetBucketLifecycleResponse(const Response& resp)
    : Response(resp) {}

  static GetBucketLifecycleResponse ParseXML(std::string_view data);
};  // struct GetBucketLifecycleResponse

using SetBucketLifecycleResponse = Response;

using DeleteBucketTagsResponse = Response;

struct GetBucketTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetBucketTagsResponse(std::map<std::string, std::string> tags)
      : tags(std::move(tags)) {}

  explicit GetBucketTagsResponse(error::Error err) : Response(std::move(err)) {}

  explicit GetBucketTagsResponse(const Response& resp)
    : Response(resp) {}

  ~GetBucketTagsResponse() = default;

  static GetBucketTagsResponse ParseXML(std::string_view data);
};  // struct GetBucketTagsResponse

using SetBucketTagsResponse = Response;

using DeleteObjectLockConfigResponse = Response;

struct GetObjectLockConfigResponse : public Response {
  ObjectLockConfig config;

  explicit GetObjectLockConfigResponse(ObjectLockConfig config)
      : config(std::move(config)) {}

  explicit GetObjectLockConfigResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetObjectLockConfigResponse(const Response& resp)
    : Response(resp) {}

  ~GetObjectLockConfigResponse() = default;
};  // struct GetObjectLockConfigResponse

using SetObjectLockConfigResponse = Response;

using DeleteObjectTagsResponse = Response;

struct GetObjectTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetObjectTagsResponse(std::map<std::string, std::string> tags)
      : tags(std::move(tags)) {}

  explicit GetObjectTagsResponse(error::Error err) : Response(std::move(err)) {}

  explicit GetObjectTagsResponse(const Response& resp)
    : Response(resp) {}

  ~GetObjectTagsResponse() = default;

  static GetObjectTagsResponse ParseXML(std::string_view data);
};  // struct GetObjectTagsResponse

using SetObjectTagsResponse = Response;

using EnableObjectLegalHoldResponse = Response;

using DisableObjectLegalHoldResponse = Response;

struct IsObjectLegalHoldEnabledResponse : public Response {
  bool enabled = false;

  explicit IsObjectLegalHoldEnabledResponse(bool enabled) : enabled(enabled) {}

  explicit IsObjectLegalHoldEnabledResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit IsObjectLegalHoldEnabledResponse(const Response& resp)
    : Response(resp) {}

  ~IsObjectLegalHoldEnabledResponse() = default;
};  // struct IsObjectLegalHoldEnabledResponse

struct GetObjectRetentionResponse : public Response {
  RetentionMode retention_mode;
  utils::Time retain_until_date;

  GetObjectRetentionResponse() = default;

  explicit GetObjectRetentionResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetObjectRetentionResponse(const Response& resp)
    : Response(resp) {}

  ~GetObjectRetentionResponse() = default;
};  // struct GetObjectRetentionResponse

using SetObjectRetentionResponse = Response;

struct GetPresignedObjectUrlResponse : public Response {
  std::string url;

  explicit GetPresignedObjectUrlResponse(std::string url)
      : url(std::move(url)) {}

  explicit GetPresignedObjectUrlResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetPresignedObjectUrlResponse(const Response& resp)
    : Response(resp) {}

  ~GetPresignedObjectUrlResponse() = default;
};  // struct GetPresignedObjectUrlResponse

struct GetPresignedPostFormDataResponse : public Response {
  std::map<std::string, std::string> form_data;

  GetPresignedPostFormDataResponse(std::map<std::string, std::string> form_data)
      : form_data(std::move(form_data)) {}

  explicit GetPresignedPostFormDataResponse(error::Error err)
      : Response(std::move(err)) {}

  explicit GetPresignedPostFormDataResponse(const Response& resp)
    : Response(resp) {}

  ~GetPresignedPostFormDataResponse() = default;
};  // struct GetPresignedPostFormDataResponse
}  // namespace s3
}  // namespace minio

#endif  // #ifndef _MINIO_S3_RESPONSE_H
