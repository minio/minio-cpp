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

  Response() {}

  Response(error::Error err) { this->err_ = err; }

  Response(const Response& resp) {
    this->err_ = resp.err_;

    this->status_code = resp.status_code;
    this->headers = resp.headers;
    this->data = resp.data;
    this->code = resp.code;
    this->message = resp.message;
    this->resource = resp.resource;
    this->request_id = resp.request_id;
    this->host_id = resp.host_id;
    this->bucket_name = resp.bucket_name;
    this->object_name = resp.object_name;
  }

  operator bool() const {
    return !err_ && code.empty() && message.empty() &&
           (status_code == 0 || status_code >= 200 && status_code <= 299);
  }

  error::Error Error() {
    if (err_) return err_;
    if (!code.empty()) return error::Error(code + ": " + message);
    if (status_code && (status_code < 200 || status_code > 299)) {
      return error::Error("failed with HTTP status code " +
                          std::to_string(status_code));
    }
    return error::SUCCESS;
  }

  static Response ParseXML(std::string_view data, int status_code,
                           utils::Multimap headers);

 private:
  error::Error err_;
};  // struct Response

struct GetRegionResponse : public Response {
  std::string region;

  GetRegionResponse(std::string region) { this->region = region; }

  GetRegionResponse(error::Error err) : Response(err) {}

  GetRegionResponse(const Response& resp) : Response(resp) {}
};  // struct GetRegionResponse

using MakeBucketResponse = Response;

struct ListBucketsResponse : public Response {
  std::list<Bucket> buckets;

  ListBucketsResponse(std::list<Bucket> buckets) { this->buckets = buckets; }

  ListBucketsResponse(error::Error err) : Response(err) {}

  ListBucketsResponse(const Response& resp) : Response(resp) {}

  static ListBucketsResponse ParseXML(std::string_view data);
};  // struct ListBucketsResponse

struct BucketExistsResponse : public Response {
  bool exist = false;

  BucketExistsResponse(bool exist) { this->exist = exist; }

  BucketExistsResponse(error::Error err) : Response(err) {}

  BucketExistsResponse(const Response& resp) : Response(resp) {}
};  // struct BucketExistsResponse

using RemoveBucketResponse = Response;

using AbortMultipartUploadResponse = Response;

struct CompleteMultipartUploadResponse : public Response {
  std::string location;
  std::string etag;
  std::string version_id;

  CompleteMultipartUploadResponse() {}

  CompleteMultipartUploadResponse(error::Error err) : Response(err) {}

  CompleteMultipartUploadResponse(const Response& resp) : Response(resp) {}

  static CompleteMultipartUploadResponse ParseXML(std::string_view data,
                                                  std::string version_id);
};  // struct CompleteMultipartUploadResponse

struct CreateMultipartUploadResponse : public Response {
  std::string upload_id;

  CreateMultipartUploadResponse(std::string upload_id) {
    this->upload_id = upload_id;
  }

  CreateMultipartUploadResponse(error::Error err) : Response(err) {}

  CreateMultipartUploadResponse(const Response& resp) : Response(resp) {}
};  // struct CreateMultipartUploadResponse

struct PutObjectResponse : public Response {
  std::string etag;
  std::string version_id;

  PutObjectResponse() {}

  PutObjectResponse(const Response& resp) : Response(resp) {}

  PutObjectResponse(error::Error err) : Response(err) {}
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

  StatObjectResponse() {}

  StatObjectResponse(error::Error err) : Response(err) {}

  StatObjectResponse(const Response& resp) : Response(resp) {}
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

  Item() {}

  Item(error::Error err) : Response(err) {}

  Item(const Response& resp) : Response(resp) {}
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

  ListObjectsResponse() {}

  ListObjectsResponse(error::Error err) : Response(err) {}

  ListObjectsResponse(const Response& resp) : Response(resp) {}

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
};  // struct DeletedObject

struct DeleteError : public Response {
  std::string version_id;

  DeleteError() {}

  DeleteError(error::Error err) : Response(err) {}

  DeleteError(const Response& resp) : Response(resp) {}
};  // struct DeleteError

struct RemoveObjectsResponse : public Response {
  std::list<DeletedObject> objects;
  std::list<DeleteError> errors;

  RemoveObjectsResponse() {}

  RemoveObjectsResponse(error::Error err) : Response(err) {}

  RemoveObjectsResponse(const Response& resp) : Response(resp) {}

  static RemoveObjectsResponse ParseXML(std::string_view data);
};  // struct RemoveObjectsResponse

using SelectObjectContentResponse = Response;

using ListenBucketNotificationResponse = Response;

using DeleteBucketPolicyResponse = Response;

struct GetBucketPolicyResponse : public Response {
  std::string policy;

  GetBucketPolicyResponse(std::string policy) { this->policy = policy; }

  GetBucketPolicyResponse(error::Error err) : Response(err) {}

  GetBucketPolicyResponse(const Response& resp) : Response(resp) {}
};  // struct GetBucketPolicyResponse

using SetBucketPolicyResponse = Response;

using DeleteBucketNotificationResponse = Response;

struct GetBucketNotificationResponse : public Response {
  NotificationConfig config;

  GetBucketNotificationResponse(NotificationConfig config) {
    this->config = config;
  }

  GetBucketNotificationResponse(error::Error err) : Response(err) {}

  GetBucketNotificationResponse(const Response& resp) : Response(resp) {}

  static GetBucketNotificationResponse ParseXML(std::string_view data);
};  // struct GetBucketNotificationResponse

using SetBucketNotificationResponse = Response;

using DeleteBucketEncryptionResponse = Response;

struct GetBucketEncryptionResponse : public Response {
  SseConfig config;

  GetBucketEncryptionResponse(SseConfig sseconfig) { this->config = config; }

  GetBucketEncryptionResponse(error::Error err) : Response(err) {}

  GetBucketEncryptionResponse(const Response& resp) : Response(resp) {}

  static GetBucketEncryptionResponse ParseXML(std::string_view data);
};  // struct GetBucketEncryptionResponse

using SetBucketEncryptionResponse = Response;

struct GetBucketVersioningResponse : public Response {
  Boolean status;
  Boolean mfa_delete;

  GetBucketVersioningResponse() {}

  GetBucketVersioningResponse(error::Error err) : Response(err) {}

  GetBucketVersioningResponse(const Response& resp) : Response(resp) {}

  std::string Status() {
    if (!status) return "Off";
    return status.Get() ? "Enabled" : "Suspended";
  }
  std::string MfaDelete() {
    if (!mfa_delete) return "";
    return mfa_delete.Get() ? "Enabled" : "Disabled";
  }
};  // struct GetBucketVersioningResponse

using SetBucketVersioningResponse = Response;

using DeleteBucketReplicationResponse = Response;

struct GetBucketReplicationResponse : public Response {
  ReplicationConfig config;

  GetBucketReplicationResponse(ReplicationConfig config) {
    this->config = config;
  }

  GetBucketReplicationResponse(error::Error err) : Response(err) {}

  GetBucketReplicationResponse(const Response& resp) : Response(resp) {}

  static GetBucketReplicationResponse ParseXML(std::string_view data);
};  // struct GetBucketReplicationResponse

using SetBucketReplicationResponse = Response;

using DeleteBucketLifecycleResponse = Response;

struct GetBucketLifecycleResponse : public Response {
  LifecycleConfig config;

  GetBucketLifecycleResponse(LifecycleConfig value) { this->config = config; }

  GetBucketLifecycleResponse(error::Error err) : Response(err) {}

  GetBucketLifecycleResponse(const Response& resp) : Response(resp) {}

  static GetBucketLifecycleResponse ParseXML(std::string_view data);
};  // struct GetBucketLifecycleResponse

using SetBucketLifecycleResponse = Response;

using DeleteBucketTagsResponse = Response;

struct GetBucketTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetBucketTagsResponse(std::map<std::string, std::string> tags) {
    this->tags = tags;
  }

  GetBucketTagsResponse(error::Error err) : Response(err) {}

  GetBucketTagsResponse(const Response& resp) : Response(resp) {}

  static GetBucketTagsResponse ParseXML(std::string_view data);
};  // struct GetBucketTagsResponse

using SetBucketTagsResponse = Response;

using DeleteObjectLockConfigResponse = Response;

struct GetObjectLockConfigResponse : public Response {
  ObjectLockConfig config;

  GetObjectLockConfigResponse(ObjectLockConfig config) {
    this->config = config;
  }

  GetObjectLockConfigResponse(error::Error err) : Response(err) {}

  GetObjectLockConfigResponse(const Response& resp) : Response(resp) {}
};  // struct GetObjectLockConfigResponse

using SetObjectLockConfigResponse = Response;

using DeleteObjectTagsResponse = Response;

struct GetObjectTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetObjectTagsResponse(std::map<std::string, std::string> tags) {
    this->tags = tags;
  }

  GetObjectTagsResponse(error::Error err) : Response(err) {}

  GetObjectTagsResponse(const Response& resp) : Response(resp) {}

  static GetObjectTagsResponse ParseXML(std::string_view data);
};  // struct GetObjectTagsResponse

using SetObjectTagsResponse = Response;

using EnableObjectLegalHoldResponse = Response;

using DisableObjectLegalHoldResponse = Response;

struct IsObjectLegalHoldEnabledResponse : public Response {
  bool enabled = false;

  IsObjectLegalHoldEnabledResponse(bool enabled) { this->enabled = enabled; }

  IsObjectLegalHoldEnabledResponse(error::Error err) : Response(err) {}

  IsObjectLegalHoldEnabledResponse(const Response& resp) : Response(resp) {}
};  // struct IsObjectLegalHoldEnabledResponse

struct GetObjectRetentionResponse : public Response {
  RetentionMode retention_mode;
  utils::Time retain_until_date;

  GetObjectRetentionResponse() {}

  GetObjectRetentionResponse(error::Error err) : Response(err) {}

  GetObjectRetentionResponse(const Response& resp) : Response(resp) {}
};  // struct GetObjectRetentionResponse

using SetObjectRetentionResponse = Response;

struct GetPresignedObjectUrlResponse : public Response {
  std::string url;

  GetPresignedObjectUrlResponse(std::string url) { this->url = url; }

  GetPresignedObjectUrlResponse(error::Error err) : Response(err) {}

  GetPresignedObjectUrlResponse(const Response& resp) : Response(resp) {}
};  // struct GetPresignedObjectUrlResponse

struct GetPresignedPostFormDataResponse : public Response {
  std::map<std::string, std::string> form_data;

  GetPresignedPostFormDataResponse(
      std::map<std::string, std::string> form_data) {
    this->form_data = form_data;
  }

  GetPresignedPostFormDataResponse(error::Error err) : Response(err) {}

  GetPresignedPostFormDataResponse(const Response& resp) : Response(resp) {}
};  // struct GetPresignedPostFormDataResponse
}  // namespace s3
}  // namespace minio

#endif  // #ifndef _MINIO_S3_RESPONSE_H
