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
  Response(error::Error err) : err_(std::move(err)) {}

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

  GetRegionResponse(std::string region) : region(std::move(region)) {}

  GetRegionResponse(error::Error err) : Response(std::move(err)) {}

  GetRegionResponse(const Response& resp) : Response(resp) {}

  ~GetRegionResponse() = default;
};  // struct GetRegionResponse

using MakeBucketResponse = Response;

struct ListBucketsResponse : public Response {
  std::list<Bucket> buckets;

  ListBucketsResponse(std::list<Bucket> buckets)
      : buckets(std::move(buckets)) {}

  ListBucketsResponse(error::Error err) : Response(std::move(err)) {}

  ListBucketsResponse(const Response& resp) : Response(resp) {}

  ~ListBucketsResponse() = default;

  static ListBucketsResponse ParseXML(std::string_view data);
};  // struct ListBucketsResponse

struct BucketExistsResponse : public Response {
  bool exist = false;

  BucketExistsResponse(bool exist) : exist(exist) {}

  BucketExistsResponse(error::Error err) : Response(std::move(err)) {}

  BucketExistsResponse(const Response& resp) : Response(resp) {}

  ~BucketExistsResponse() = default;
};  // struct BucketExistsResponse

using RemoveBucketResponse = Response;

using AbortMultipartUploadResponse = Response;

struct CompleteMultipartUploadResponse : public Response {
  std::string location;
  std::string etag;
  std::string version_id;

  CompleteMultipartUploadResponse() = default;

  CompleteMultipartUploadResponse(error::Error err)
      : Response(std::move(err)) {}

  CompleteMultipartUploadResponse(const Response& resp) : Response(resp) {}

  ~CompleteMultipartUploadResponse() = default;
  
  static CompleteMultipartUploadResponse ParseXML(std::string_view data,
                                                  std::string version_id);
};  // struct CompleteMultipartUploadResponse

struct CreateMultipartUploadResponse : public Response {
  std::string upload_id;

  CreateMultipartUploadResponse(std::string upload_id)
      : upload_id(std::move(upload_id)) {}

  CreateMultipartUploadResponse(error::Error err) : Response(std::move(err)) {}

  CreateMultipartUploadResponse(const Response& resp) : Response(resp) {}

  ~CreateMultipartUploadResponse() = default;
};  // struct CreateMultipartUploadResponse

struct PutObjectResponse : public Response {
  std::string etag;
  std::string version_id;

  PutObjectResponse() = default;

  PutObjectResponse(error::Error err) : Response(std::move(err)) {}

  PutObjectResponse(const Response& resp) : Response(resp) {}

  PutObjectResponse(const CompleteMultipartUploadResponse& resp)
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

  StatObjectResponse(error::Error err) : Response(std::move(err)) {}

  StatObjectResponse(const Response& resp) : Response(resp) {}

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

  Item(error::Error err) : Response(std::move(err)) {}

  Item(const Response& resp) : Response(resp) {}

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

  ListObjectsResponse(error::Error err) : Response(std::move(err)) {}

  ListObjectsResponse(const Response& resp) : Response(resp) {}

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

  DeleteError(error::Error err) : Response(std::move(err)) {}

  DeleteError(const Response& resp) : Response(resp) {}

  ~DeleteError() = default;
};  // struct DeleteError

struct RemoveObjectsResponse : public Response {
  std::list<DeletedObject> objects;
  std::list<DeleteError> errors;

  RemoveObjectsResponse() = default;

  RemoveObjectsResponse(error::Error err) : Response(std::move(err)) {}

  RemoveObjectsResponse(const Response& resp) : Response(resp) {}

  ~RemoveObjectsResponse() = default;

  static RemoveObjectsResponse ParseXML(std::string_view data);
};  // struct RemoveObjectsResponse

using SelectObjectContentResponse = Response;

using ListenBucketNotificationResponse = Response;

using DeleteBucketPolicyResponse = Response;

struct GetBucketPolicyResponse : public Response {
  std::string policy;

  GetBucketPolicyResponse(std::string policy) : policy(std::move(policy)) {}

  GetBucketPolicyResponse(error::Error err) : Response(std::move(err)) {}

  GetBucketPolicyResponse(const Response& resp) : Response(resp) {}

  ~GetBucketPolicyResponse() = default;
};  // struct GetBucketPolicyResponse

using SetBucketPolicyResponse = Response;

using DeleteBucketNotificationResponse = Response;

struct GetBucketNotificationResponse : public Response {
  NotificationConfig config;

  GetBucketNotificationResponse(NotificationConfig config)
      : config(std::move(config)) {}

  GetBucketNotificationResponse(error::Error err) : Response(std::move(err)) {}

  GetBucketNotificationResponse(const Response& resp) : Response(resp) {}

  ~GetBucketNotificationResponse() = default;

  static GetBucketNotificationResponse ParseXML(std::string_view data);
};  // struct GetBucketNotificationResponse

using SetBucketNotificationResponse = Response;

using DeleteBucketEncryptionResponse = Response;

struct GetBucketEncryptionResponse : public Response {
  SseConfig config;

  GetBucketEncryptionResponse(SseConfig config) : config(std::move(config)) {}

  GetBucketEncryptionResponse(error::Error err) : Response(std::move(err)) {}

  GetBucketEncryptionResponse(const Response& resp) : Response(resp) {}

  ~GetBucketEncryptionResponse() = default;

  static GetBucketEncryptionResponse ParseXML(std::string_view data);
};  // struct GetBucketEncryptionResponse

using SetBucketEncryptionResponse = Response;

struct GetBucketVersioningResponse : public Response {
  Boolean status;
  Boolean mfa_delete;

  GetBucketVersioningResponse() = default;

  GetBucketVersioningResponse(error::Error err) : Response(std::move(err)) {}

  GetBucketVersioningResponse(const Response& resp) : Response(resp) {}

  ~GetBucketVersioningResponse() = default;

  std::string Status() const {
    if (!status) return "Off";
    return status.Get() ? "Enabled" : "Suspended";
  }

  std::string MfaDelete() const {
    if (!mfa_delete) return {};
    return mfa_delete.Get() ? "Enabled" : "Disabled";
  }
};  // struct GetBucketVersioningResponse

using SetBucketVersioningResponse = Response;

using DeleteBucketReplicationResponse = Response;

struct GetBucketReplicationResponse : public Response {
  ReplicationConfig config;

  GetBucketReplicationResponse(ReplicationConfig config)
      : config(std::move(config)) {}

  GetBucketReplicationResponse(error::Error err) : Response(std::move(err)) {}

  GetBucketReplicationResponse(const Response& resp) : Response(resp) {}

  ~GetBucketReplicationResponse() = default;

  static GetBucketReplicationResponse ParseXML(std::string_view data);
};  // struct GetBucketReplicationResponse

using SetBucketReplicationResponse = Response;

using DeleteBucketLifecycleResponse = Response;

struct GetBucketLifecycleResponse : public Response {
  LifecycleConfig config;

  GetBucketLifecycleResponse(LifecycleConfig config)
      : config(std::move(config)) {}

  GetBucketLifecycleResponse(error::Error err) : Response(std::move(err)) {}

  GetBucketLifecycleResponse(const Response& resp) : Response(resp) {}

  static GetBucketLifecycleResponse ParseXML(std::string_view data);
};  // struct GetBucketLifecycleResponse

using SetBucketLifecycleResponse = Response;

using DeleteBucketTagsResponse = Response;

struct GetBucketTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetBucketTagsResponse(std::map<std::string, std::string> tags)
      : tags(std::move(tags)) {}

  GetBucketTagsResponse(error::Error err) : Response(std::move(err)) {}

  GetBucketTagsResponse(const Response& resp) : Response(resp) {}

  ~GetBucketTagsResponse() = default;

  static GetBucketTagsResponse ParseXML(std::string_view data);
};  // struct GetBucketTagsResponse

using SetBucketTagsResponse = Response;

using DeleteObjectLockConfigResponse = Response;

struct GetObjectLockConfigResponse : public Response {
  ObjectLockConfig config;

  GetObjectLockConfigResponse(ObjectLockConfig config)
      : config(std::move(config)) {}

  GetObjectLockConfigResponse(error::Error err) : Response(std::move(err)) {}

  GetObjectLockConfigResponse(const Response& resp) : Response(resp) {}

  ~GetObjectLockConfigResponse() = default;
};  // struct GetObjectLockConfigResponse

using SetObjectLockConfigResponse = Response;

using DeleteObjectTagsResponse = Response;

struct GetObjectTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetObjectTagsResponse(std::map<std::string, std::string> tags)
      : tags(std::move(tags)) {}

  GetObjectTagsResponse(error::Error err) : Response(std::move(err)) {}

  GetObjectTagsResponse(const Response& resp) : Response(resp) {}

  ~GetObjectTagsResponse() = default;

  static GetObjectTagsResponse ParseXML(std::string_view data);
};  // struct GetObjectTagsResponse

using SetObjectTagsResponse = Response;

using EnableObjectLegalHoldResponse = Response;

using DisableObjectLegalHoldResponse = Response;

struct IsObjectLegalHoldEnabledResponse : public Response {
  bool enabled = false;

  IsObjectLegalHoldEnabledResponse(bool enabled) : enabled(enabled) {}

  IsObjectLegalHoldEnabledResponse(error::Error err)
      : Response(std::move(err)) {}

  IsObjectLegalHoldEnabledResponse(const Response& resp) : Response(resp) {}

  ~IsObjectLegalHoldEnabledResponse() = default;
};  // struct IsObjectLegalHoldEnabledResponse

struct GetObjectRetentionResponse : public Response {
  RetentionMode retention_mode;
  utils::Time retain_until_date;

  GetObjectRetentionResponse() = default;

  GetObjectRetentionResponse(error::Error err) : Response(std::move(err)) {}

  GetObjectRetentionResponse(const Response& resp) : Response(resp) {}

  ~GetObjectRetentionResponse() = default;
};  // struct GetObjectRetentionResponse

using SetObjectRetentionResponse = Response;

struct GetPresignedObjectUrlResponse : public Response {
  std::string url;

  GetPresignedObjectUrlResponse(std::string url) : url(std::move(url)) {}

  GetPresignedObjectUrlResponse(error::Error err) : Response(std::move(err)) {}

  GetPresignedObjectUrlResponse(const Response& resp) : Response(resp) {}

  ~GetPresignedObjectUrlResponse() = default;
};  // struct GetPresignedObjectUrlResponse

struct GetPresignedPostFormDataResponse : public Response {
  std::map<std::string, std::string> form_data;

  GetPresignedPostFormDataResponse(std::map<std::string, std::string> form_data)
      : form_data(std::move(form_data)) {}

  GetPresignedPostFormDataResponse(error::Error err)
      : Response(std::move(err)) {}

  GetPresignedPostFormDataResponse(const Response& resp) : Response(resp) {}

  ~GetPresignedPostFormDataResponse() = default;
};  // struct GetPresignedPostFormDataResponse
}  // namespace s3
}  // namespace minio

#endif  // #ifndef _MINIO_S3_RESPONSE_H
