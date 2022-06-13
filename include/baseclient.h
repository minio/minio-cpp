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

#ifndef _MINIO_S3_BASE_CLIENT_H
#define _MINIO_S3_BASE_CLIENT_H

#include "args.h"
#include "config.h"
#include "request.h"
#include "response.h"
#include "select.h"

namespace minio {
namespace s3 {
utils::Multimap GetCommonListObjectsQueryParams(std::string& delimiter,
                                                std::string& encoding_type,
                                                unsigned int max_keys,
                                                std::string& prefix);

/**
 * Base client to perform S3 APIs.
 */
class BaseClient {
 protected:
  BaseUrl& base_url_;
  creds::Provider* provider_ = NULL;
  std::map<std::string, std::string> region_map_;
  bool debug_ = false;
  bool ignore_cert_check_ = false;
  std::string ssl_cert_file_;
  std::string user_agent_ = DEFAULT_USER_AGENT;

 public:
  BaseClient(BaseUrl& base_url, creds::Provider* provider = NULL);

  void Debug(bool flag) { debug_ = flag; }

  void IgnoreCertCheck(bool flag) { ignore_cert_check_ = flag; }

  void SetSslCertFile(std::string ssl_cert_file) {
    ssl_cert_file_ = ssl_cert_file;
  }

  error::Error SetAppInfo(std::string_view app_name,
                          std::string_view app_version);

  void HandleRedirectResponse(std::string& code, std::string& message,
                              int status_code, http::Method method,
                              utils::Multimap headers, std::string& bucket_name,
                              bool retry = false);
  Response GetErrorResponse(http::Response resp, std::string_view resource,
                            http::Method method, std::string& bucket_name,
                            std::string& object_name);
  Response execute(Request& req);
  Response Execute(Request& req);
  GetRegionResponse GetRegion(std::string& bucket_name, std::string& region);

  AbortMultipartUploadResponse AbortMultipartUpload(
      AbortMultipartUploadArgs args);
  BucketExistsResponse BucketExists(BucketExistsArgs args);
  CompleteMultipartUploadResponse CompleteMultipartUpload(
      CompleteMultipartUploadArgs args);
  CreateMultipartUploadResponse CreateMultipartUpload(
      CreateMultipartUploadArgs args);
  DeleteBucketEncryptionResponse DeleteBucketEncryption(
      DeleteBucketEncryptionArgs args);
  DisableObjectLegalHoldResponse DisableObjectLegalHold(
      DisableObjectLegalHoldArgs args);
  DeleteBucketLifecycleResponse DeleteBucketLifecycle(
      DeleteBucketLifecycleArgs args);
  DeleteBucketNotificationResponse DeleteBucketNotification(
      DeleteBucketNotificationArgs args);
  DeleteBucketPolicyResponse DeleteBucketPolicy(DeleteBucketPolicyArgs args);
  DeleteBucketReplicationResponse DeleteBucketReplication(
      DeleteBucketReplicationArgs args);
  DeleteBucketTagsResponse DeleteBucketTags(DeleteBucketTagsArgs args);
  DeleteObjectLockConfigResponse DeleteObjectLockConfig(
      DeleteObjectLockConfigArgs args);
  DeleteObjectTagsResponse DeleteObjectTags(DeleteObjectTagsArgs args);
  EnableObjectLegalHoldResponse EnableObjectLegalHold(
      EnableObjectLegalHoldArgs args);
  GetBucketEncryptionResponse GetBucketEncryption(GetBucketEncryptionArgs args);
  GetBucketLifecycleResponse GetBucketLifecycle(GetBucketLifecycleArgs args);
  GetBucketNotificationResponse GetBucketNotification(
      GetBucketNotificationArgs args);
  GetBucketPolicyResponse GetBucketPolicy(GetBucketPolicyArgs args);
  GetBucketReplicationResponse GetBucketReplication(
      GetBucketReplicationArgs args);
  GetBucketTagsResponse GetBucketTags(GetBucketTagsArgs args);
  GetBucketVersioningResponse GetBucketVersioning(GetBucketVersioningArgs args);
  GetObjectResponse GetObject(GetObjectArgs args);
  GetObjectLockConfigResponse GetObjectLockConfig(GetObjectLockConfigArgs args);
  GetObjectRetentionResponse GetObjectRetention(GetObjectRetentionArgs args);
  GetObjectTagsResponse GetObjectTags(GetObjectTagsArgs args);
  GetPresignedObjectUrlResponse GetPresignedObjectUrl(
      GetPresignedObjectUrlArgs args);
  GetPresignedPostFormDataResponse GetPresignedPostFormData(PostPolicy policy);
  IsObjectLegalHoldEnabledResponse IsObjectLegalHoldEnabled(
      IsObjectLegalHoldEnabledArgs args);
  ListBucketsResponse ListBuckets(ListBucketsArgs args);
  ListBucketsResponse ListBuckets();
  ListenBucketNotificationResponse ListenBucketNotification(
      ListenBucketNotificationArgs args);
  ListObjectsResponse ListObjectsV1(ListObjectsV1Args args);
  ListObjectsResponse ListObjectsV2(ListObjectsV2Args args);
  ListObjectsResponse ListObjectVersions(ListObjectVersionsArgs args);
  MakeBucketResponse MakeBucket(MakeBucketArgs args);
  PutObjectResponse PutObject(PutObjectApiArgs args);
  RemoveBucketResponse RemoveBucket(RemoveBucketArgs args);
  RemoveObjectResponse RemoveObject(RemoveObjectArgs args);
  RemoveObjectsResponse RemoveObjects(RemoveObjectsApiArgs args);
  SetBucketEncryptionResponse SetBucketEncryption(SetBucketEncryptionArgs args);
  SetBucketLifecycleResponse SetBucketLifecycle(SetBucketLifecycleArgs args);
  SetBucketNotificationResponse SetBucketNotification(
      SetBucketNotificationArgs args);
  SetBucketPolicyResponse SetBucketPolicy(SetBucketPolicyArgs args);
  SetBucketReplicationResponse SetBucketReplication(
      SetBucketReplicationArgs args);
  SetBucketTagsResponse SetBucketTags(SetBucketTagsArgs args);
  SetBucketVersioningResponse SetBucketVersioning(SetBucketVersioningArgs args);
  SetObjectLockConfigResponse SetObjectLockConfig(SetObjectLockConfigArgs args);
  SetObjectRetentionResponse SetObjectRetention(SetObjectRetentionArgs args);
  SetObjectTagsResponse SetObjectTags(SetObjectTagsArgs args);
  SelectObjectContentResponse SelectObjectContent(SelectObjectContentArgs args);
  StatObjectResponse StatObject(StatObjectArgs args);
  UploadPartResponse UploadPart(UploadPartArgs args);
  UploadPartCopyResponse UploadPartCopy(UploadPartCopyArgs args);
};  // class BaseClient
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_S3_BASE_CLIENT_H
