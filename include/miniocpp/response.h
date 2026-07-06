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

#ifndef MINIO_CPP_RESPONSE_H_INCLUDED
#define MINIO_CPP_RESPONSE_H_INCLUDED

#include <list>
#include <map>
#include <memory>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <type_traits>

#include "error.h"
#include "result.h"
#include "types.h"
#include "utils.h"

namespace minio::s3 {

struct Response {
 public:
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
  std::string etag;

  Response();

  Response(const Response& resp) = default;
  Response& operator=(const Response& resp) = default;

  Response(Response&& resp) = default;
  Response& operator=(Response&& resp) = default;

  ~Response();

  static Result<Response> ParseXML(std::string_view data, int status_code,
                                   utils::Multimap headers);
};  // struct Response

#define MINIO_S3_DERIVE_FROM_RESPONSE(DerivedName)                 \
  struct DerivedName : public Response {                           \
    DerivedName() = default;                                       \
    ~DerivedName() = default;                                      \
                                                                   \
    DerivedName(const DerivedName&) = default;                     \
    DerivedName& operator=(const DerivedName&) = default;          \
                                                                   \
    DerivedName(DerivedName&&) = default;                          \
    DerivedName& operator=(DerivedName&&) = default;               \
                                                                   \
    explicit DerivedName(const Response& resp) : Response(resp) {} \
  };

#define MINIO_S3_DERIVE_FROM_PUT_OBJECT_RESPONSE(DerivedName)               \
  struct DerivedName : public PutObjectResponse {                           \
    DerivedName() = default;                                                \
    ~DerivedName() = default;                                               \
                                                                            \
    DerivedName(const DerivedName&) = default;                              \
    DerivedName& operator=(const DerivedName&) = default;                   \
                                                                            \
    DerivedName(DerivedName&&) = default;                                   \
    DerivedName& operator=(DerivedName&&) = default;                        \
                                                                            \
    explicit DerivedName(const PutObjectResponse& resp)                     \
        : PutObjectResponse(resp) {}                                        \
    explicit DerivedName(const Response& resp) : PutObjectResponse(resp) {} \
                                                                            \
    explicit DerivedName(const CompleteMultipartUploadResponse& resp)       \
        : PutObjectResponse(resp) {}                                        \
  };

struct GetRegionResponse : public Response {
  std::string region;

  explicit GetRegionResponse(std::string region) : region(std::move(region)) {}

  explicit GetRegionResponse(const Response& resp) : Response(resp) {}

  ~GetRegionResponse() = default;
};  // struct GetRegionResponse

MINIO_S3_DERIVE_FROM_RESPONSE(MakeBucketResponse)

struct ListBucketsResponse : public Response {
  std::list<Bucket> buckets;

  explicit ListBucketsResponse(std::list<Bucket> buckets)
      : buckets(std::move(buckets)) {}

  explicit ListBucketsResponse(const Response& resp) : Response(resp) {}

  ~ListBucketsResponse() = default;

  static Result<ListBucketsResponse> ParseXML(std::string_view data);
};  // struct ListBucketsResponse

struct BucketExistsResponse : public Response {
  bool exist = false;

  explicit BucketExistsResponse(bool exist) : exist(exist) {}

  explicit BucketExistsResponse(const Response& resp) : Response(resp) {}

  ~BucketExistsResponse() = default;
};  // struct BucketExistsResponse

MINIO_S3_DERIVE_FROM_RESPONSE(RemoveBucketResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(AbortMultipartUploadResponse)

struct CompleteMultipartUploadResponse : public Response {
  std::string location;
  std::string etag;
  std::string version_id;
  std::string checksumCRC32;
  std::string checksumCRC32C;
  std::string checksumSHA1;
  std::string checksumSHA256;
  std::string checksum_crc64nvme;

  CompleteMultipartUploadResponse() = default;

  explicit CompleteMultipartUploadResponse(const Response& resp)
      : Response(resp) {}

  ~CompleteMultipartUploadResponse() = default;

  static Result<CompleteMultipartUploadResponse> ParseXML(
      std::string_view data, std::string version_id);
};  // struct CompleteMultipartUploadResponse

struct CreateMultipartUploadResponse : public Response {
  std::string upload_id;

  explicit CreateMultipartUploadResponse(std::string upload_id)
      : upload_id(std::move(upload_id)) {}

  explicit CreateMultipartUploadResponse(const Response& resp)
      : Response(resp) {}

  ~CreateMultipartUploadResponse() = default;
};  // struct CreateMultipartUploadResponse

struct PutObjectResponse : public Response {
  std::string etag;
  std::string version_id;
  std::string checksumCRC32;
  std::string checksumCRC32C;
  std::string checksumSHA1;
  std::string checksumSHA256;
  std::string checksum_crc64nvme;

  PutObjectResponse() = default;

  explicit PutObjectResponse(const Response& resp) : Response(resp) {}

  explicit PutObjectResponse(const CompleteMultipartUploadResponse& resp)
      : Response(resp), etag(resp.etag), version_id(resp.version_id) {}

  ~PutObjectResponse() = default;
};  // struct PutObjectResponse

MINIO_S3_DERIVE_FROM_PUT_OBJECT_RESPONSE(UploadPartResponse)
MINIO_S3_DERIVE_FROM_PUT_OBJECT_RESPONSE(UploadPartCopyResponse)

struct StatObjectResponse : public Response {
  std::string version_id;
  std::string etag;
  size_t size = 0;
  utils::UtcTime last_modified;
  RetentionMode retention_mode;
  utils::UtcTime retention_retain_until_date;
  LegalHold legal_hold;
  bool delete_marker;
  utils::Multimap user_metadata;

  StatObjectResponse() = default;

  explicit StatObjectResponse(const Response& resp) : Response(resp) {}

  ~StatObjectResponse() = default;
};  // struct StatObjectResponse

MINIO_S3_DERIVE_FROM_RESPONSE(RemoveObjectResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DownloadObjectResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(GetObjectResponse)

struct Item : public Response {
  // etag and name stored in owning ListObjectsResponse's owned_.
  std::string_view etag;  // except DeleteMarker
  std::string_view name;
  utils::UtcTime last_modified;
  // Fields below point into owning ListObjectsResponse's xml_document memory.
  std::string_view owner_id;
  std::string_view owner_name;
  size_t size = 0;  // except DeleteMarker
  std::string_view storage_class;
  bool is_latest = false;       // except ListObjects V1/V2
  std::string_view version_id;  // except ListObjects V1/V2
  std::map<std::string, std::string> user_metadata;
  bool is_prefix = false;
  bool is_delete_marker = false;
  std::string_view encoding_type;

  Item() = default;

  explicit Item(const Response& resp) : Response(resp) {}

  ~Item() = default;
};  // struct Item

struct ListObjectsResponse : public Response {
  // Common
  std::string_view name;
  std::string_view encoding_type;
  std::string_view prefix;
  std::string_view delimiter;
  bool is_truncated;
  unsigned int max_keys;
  std::list<Item> contents;

  // Owned XML document for zero-copy string_view backing.
  std::shared_ptr<pugi::xml_document> doc_;
  // Owned strings for non-XML-assigned string_view backing storage.
  std::list<std::string> owned_;

  // ListObjectsV1
  std::string_view marker;
  std::string_view next_marker;

  // ListObjectsV2
  unsigned int key_count;
  std::string_view start_after;
  std::string_view continuation_token;
  std::string_view next_continuation_token;

  // ListObjectVersions
  std::string_view key_marker;
  std::string_view next_key_marker;
  std::string_view version_id_marker;
  std::string_view next_version_id_marker;

  ListObjectsResponse() = default;
  ListObjectsResponse(const ListObjectsResponse&) = delete;
  ListObjectsResponse& operator=(const ListObjectsResponse&) = delete;
  ListObjectsResponse(ListObjectsResponse&&) = default;
  ListObjectsResponse& operator=(ListObjectsResponse&&) = default;

  explicit ListObjectsResponse(const Response& resp) : Response(resp) {}

  static Result<ListObjectsResponse> ParseXML(std::string_view data,
                                              bool version);
};  // struct ListObjectsResponse

MINIO_S3_DERIVE_FROM_PUT_OBJECT_RESPONSE(CopyObjectResponse)
MINIO_S3_DERIVE_FROM_PUT_OBJECT_RESPONSE(ComposeObjectResponse)
MINIO_S3_DERIVE_FROM_PUT_OBJECT_RESPONSE(UploadObjectResponse)

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

  explicit DeleteError(const Response& resp) : Response(resp) {}

  ~DeleteError() = default;
};  // struct DeleteError

struct RemoveObjectsResponse : public Response {
  std::list<DeletedObject> objects;
  std::list<DeleteError> errors;

  RemoveObjectsResponse() = default;

  explicit RemoveObjectsResponse(const Response& resp) : Response(resp) {}

  ~RemoveObjectsResponse() = default;

  static Result<RemoveObjectsResponse> ParseXML(std::string_view data);
};  // struct RemoveObjectsResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SelectObjectContentResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(ListenBucketNotificationResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteBucketPolicyResponse)

struct GetBucketPolicyResponse : public Response {
  std::string policy;

  explicit GetBucketPolicyResponse(std::string policy)
      : policy(std::move(policy)) {}

  explicit GetBucketPolicyResponse(const Response& resp) : Response(resp) {}

  ~GetBucketPolicyResponse() = default;
};  // struct GetBucketPolicyResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetBucketPolicyResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteBucketNotificationResponse)

struct GetBucketNotificationResponse : public Response {
  NotificationConfig config;

  explicit GetBucketNotificationResponse(NotificationConfig config)
      : config(std::move(config)) {}

  explicit GetBucketNotificationResponse(const Response& resp)
      : Response(resp) {}

  ~GetBucketNotificationResponse() = default;

  static Result<GetBucketNotificationResponse> ParseXML(std::string_view data);
};  // struct GetBucketNotificationResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetBucketNotificationResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteBucketEncryptionResponse)

struct GetBucketEncryptionResponse : public Response {
  SseConfig config;

  explicit GetBucketEncryptionResponse(SseConfig config)
      : config(std::move(config)) {}

  explicit GetBucketEncryptionResponse(const Response& resp) : Response(resp) {}

  ~GetBucketEncryptionResponse() = default;

  static Result<GetBucketEncryptionResponse> ParseXML(std::string_view data);
};  // struct GetBucketEncryptionResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetBucketEncryptionResponse)

struct GetBucketVersioningResponse : public Response {
  Boolean status;
  Boolean mfa_delete;

  GetBucketVersioningResponse() = default;

  explicit GetBucketVersioningResponse(const Response& resp) : Response(resp) {}

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

MINIO_S3_DERIVE_FROM_RESPONSE(SetBucketVersioningResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteBucketReplicationResponse)

struct GetBucketReplicationResponse : public Response {
  ReplicationConfig config;

  explicit GetBucketReplicationResponse(ReplicationConfig config)
      : config(std::move(config)) {}

  explicit GetBucketReplicationResponse(const Response& resp)
      : Response(resp) {}

  ~GetBucketReplicationResponse() = default;

  static Result<GetBucketReplicationResponse> ParseXML(std::string_view data);
};  // struct GetBucketReplicationResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetBucketReplicationResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteBucketLifecycleResponse)

struct GetBucketLifecycleResponse : public Response {
  LifecycleConfig config;

  explicit GetBucketLifecycleResponse(LifecycleConfig config)
      : config(std::move(config)) {}

  explicit GetBucketLifecycleResponse(const Response& resp) : Response(resp) {}

  static Result<GetBucketLifecycleResponse> ParseXML(std::string_view data);
};  // struct GetBucketLifecycleResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetBucketLifecycleResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteBucketTagsResponse)

struct GetBucketTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetBucketTagsResponse(std::map<std::string, std::string> tags)
      : tags(std::move(tags)) {}

  explicit GetBucketTagsResponse(const Response& resp) : Response(resp) {}

  ~GetBucketTagsResponse() = default;

  static Result<GetBucketTagsResponse> ParseXML(std::string_view data);
};  // struct GetBucketTagsResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetBucketTagsResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteObjectLockConfigResponse)

struct GetObjectLockConfigResponse : public Response {
  ObjectLockConfig config;

  explicit GetObjectLockConfigResponse(ObjectLockConfig config)
      : config(std::move(config)) {}

  explicit GetObjectLockConfigResponse(const Response& resp) : Response(resp) {}

  ~GetObjectLockConfigResponse() = default;
};  // struct GetObjectLockConfigResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetObjectLockConfigResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DeleteObjectTagsResponse)

struct GetObjectTagsResponse : public Response {
  std::map<std::string, std::string> tags;

  GetObjectTagsResponse(std::map<std::string, std::string> tags)
      : tags(std::move(tags)) {}

  explicit GetObjectTagsResponse(const Response& resp) : Response(resp) {}

  ~GetObjectTagsResponse() = default;

  static Result<GetObjectTagsResponse> ParseXML(std::string_view data);
};  // struct GetObjectTagsResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetObjectTagsResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(EnableObjectLegalHoldResponse)
MINIO_S3_DERIVE_FROM_RESPONSE(DisableObjectLegalHoldResponse)

struct IsObjectLegalHoldEnabledResponse : public Response {
  bool enabled = false;

  explicit IsObjectLegalHoldEnabledResponse(bool enabled) : enabled(enabled) {}

  explicit IsObjectLegalHoldEnabledResponse(const Response& resp)
      : Response(resp) {}

  ~IsObjectLegalHoldEnabledResponse() = default;
};  // struct IsObjectLegalHoldEnabledResponse

struct GetObjectRetentionResponse : public Response {
  RetentionMode retention_mode;
  utils::UtcTime retain_until_date;

  GetObjectRetentionResponse() = default;

  explicit GetObjectRetentionResponse(const Response& resp) : Response(resp) {}

  ~GetObjectRetentionResponse() = default;
};  // struct GetObjectRetentionResponse

MINIO_S3_DERIVE_FROM_RESPONSE(SetObjectRetentionResponse)

struct GetPresignedObjectUrlResponse : public Response {
  std::string url;

  explicit GetPresignedObjectUrlResponse(std::string url)
      : url(std::move(url)) {}

  explicit GetPresignedObjectUrlResponse(const Response& resp)
      : Response(resp) {}

  ~GetPresignedObjectUrlResponse() = default;
};  // struct GetPresignedObjectUrlResponse

struct GetPresignedPostFormDataResponse : public Response {
  std::map<std::string, std::string> form_data;

  GetPresignedPostFormDataResponse(std::map<std::string, std::string> form_data)
      : form_data(std::move(form_data)) {}

  explicit GetPresignedPostFormDataResponse(const Response& resp)
      : Response(resp) {}

  ~GetPresignedPostFormDataResponse() = default;
};  // struct GetPresignedPostFormDataResponse

#undef MINIO_S3_DERIVE_FROM_PUT_OBJECT_RESPONSE
#undef MINIO_S3_DERIVE_FROM_RESPONSE

}  // namespace minio::s3

#endif  // MINIO_CPP_RESPONSE_H_INCLUDED
