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

#ifndef MINIO_CPP_BASECLIENT_H_INCLUDED
#define MINIO_CPP_BASECLIENT_H_INCLUDED

#include <future>
#include <map>
#include <shared_mutex>
#include <string>
#include <type_traits>

#include "args.h"
#include "config.h"
#include "error.h"
#include "http.h"
#include "providers.h"
#include "request.h"
#include "response.h"
#include "result.h"
#include "utils.h"

#ifdef MINIO_CPP_RDMA
#include "nvidia-cuobjclient.h"
#include "rdma.h"
#endif

#if defined(_WIN32) && defined(GetObject)
#pragma push_macro("GetObject")
#undef GetObject
#define MINIO_CPP_GET_OBJECT_DEFINED
#endif

namespace minio::s3 {

utils::Multimap GetCommonListObjectsQueryParams(
    const std::string& delimiter, const std::string& encoding_type,
    unsigned int max_keys, const std::string& prefix);

/**
 * Base client to perform S3 APIs.
 */
class BaseClient {
 protected:
  BaseUrl base_url_;
  creds::Provider* const provider_ = nullptr;
  std::map<std::string, std::string> region_map_;
  mutable std::shared_mutex region_map_mutex_;
  bool debug_ = false;
  bool ignore_cert_check_ = false;
  std::string ssl_cert_file_;
  std::string user_agent_ = DEFAULT_USER_AGENT;

 public:
  explicit BaseClient(BaseUrl base_url,
                      creds::Provider* const provider = nullptr);

  virtual ~BaseClient() = default;

  // Read-only access to the configured BaseUrl. Used by FFI shims that need
  // the region/endpoint at call time without re-plumbing it through every
  // call site (e.g. PutObjectRDMA / GetObjectRDMA in minio-go's C glue, where
  // it propagates onto PutObjectRDMAArgs so the HTTP fallback inherits a
  // known region instead of paying a GetRegion() roundtrip).
  const BaseUrl& GetBaseUrl() const { return base_url_; }

  void Debug(bool flag) { debug_ = flag; }

  void IgnoreCertCheck(bool flag) { ignore_cert_check_ = flag; }

  void SetSslCertFile(std::string ssl_cert_file) {
    ssl_cert_file_ = std::move(ssl_cert_file);
  }

  error::Error SetAppInfo(std::string_view app_name,
                          std::string_view app_version);

  void HandleRedirectResponse(std::string& code, std::string& message,
                              int status_code, http::Method method,
                              const utils::Multimap& headers,
                              const std::string& bucket_name,
                              bool retry = false);
  Result<Response> GetErrorResponse(http::Response resp,
                                    std::string_view resource,
                                    http::Method method,
                                    const std::string& bucket_name,
                                    const std::string& object_name);
  Result<Response> execute(Request& req);
  Result<Response> Execute(Request& req);
  Result<GetRegionResponse> GetRegion(const std::string& bucket_name,
                                      const std::string& region);

  Result<AbortMultipartUploadResponse> AbortMultipartUpload(
      AbortMultipartUploadArgs args);
  Result<BucketExistsResponse> BucketExists(BucketExistsArgs args);
  Result<CompleteMultipartUploadResponse> CompleteMultipartUpload(
      CompleteMultipartUploadArgs args);
  Result<CreateMultipartUploadResponse> CreateMultipartUpload(
      CreateMultipartUploadArgs args);
  Result<DeleteBucketEncryptionResponse> DeleteBucketEncryption(
      DeleteBucketEncryptionArgs args);
  Result<DisableObjectLegalHoldResponse> DisableObjectLegalHold(
      DisableObjectLegalHoldArgs args);
  Result<DeleteBucketLifecycleResponse> DeleteBucketLifecycle(
      DeleteBucketLifecycleArgs args);
  Result<DeleteBucketNotificationResponse> DeleteBucketNotification(
      DeleteBucketNotificationArgs args);
  Result<DeleteBucketPolicyResponse> DeleteBucketPolicy(
      DeleteBucketPolicyArgs args);
  Result<DeleteBucketReplicationResponse> DeleteBucketReplication(
      DeleteBucketReplicationArgs args);
  Result<DeleteBucketTagsResponse> DeleteBucketTags(DeleteBucketTagsArgs args);
  Result<DeleteObjectLockConfigResponse> DeleteObjectLockConfig(
      DeleteObjectLockConfigArgs args);
  Result<DeleteObjectTagsResponse> DeleteObjectTags(DeleteObjectTagsArgs args);
  Result<EnableObjectLegalHoldResponse> EnableObjectLegalHold(
      EnableObjectLegalHoldArgs args);
  Result<GetBucketEncryptionResponse> GetBucketEncryption(
      GetBucketEncryptionArgs args);
  Result<GetBucketLifecycleResponse> GetBucketLifecycle(
      GetBucketLifecycleArgs args);
  Result<GetBucketNotificationResponse> GetBucketNotification(
      GetBucketNotificationArgs args);
  Result<GetBucketPolicyResponse> GetBucketPolicy(GetBucketPolicyArgs args);
  Result<GetBucketReplicationResponse> GetBucketReplication(
      GetBucketReplicationArgs args);
  Result<GetBucketTagsResponse> GetBucketTags(GetBucketTagsArgs args);
  Result<GetBucketVersioningResponse> GetBucketVersioning(
      GetBucketVersioningArgs args);
  Result<GetObjectResponse> GetObject(GetObjectArgs args);
  Result<GetObjectLockConfigResponse> GetObjectLockConfig(
      GetObjectLockConfigArgs args);
  Result<GetObjectRetentionResponse> GetObjectRetention(
      GetObjectRetentionArgs args);
  Result<GetObjectTagsResponse> GetObjectTags(GetObjectTagsArgs args);
  Result<GetPresignedObjectUrlResponse> GetPresignedObjectUrl(
      GetPresignedObjectUrlArgs args);
  Result<GetPresignedPostFormDataResponse> GetPresignedPostFormData(
      PostPolicy policy);
  Result<IsObjectLegalHoldEnabledResponse> IsObjectLegalHoldEnabled(
      IsObjectLegalHoldEnabledArgs args);
  Result<ListBucketsResponse> ListBuckets(ListBucketsArgs args);
  Result<ListBucketsResponse> ListBuckets();
  Result<ListenBucketNotificationResponse> ListenBucketNotification(
      ListenBucketNotificationArgs args);
  Result<ListObjectsResponse> ListObjectsV1(ListObjectsV1Args args);
  Result<ListObjectsResponse> ListObjectsV2(ListObjectsV2Args args);
  Result<ListObjectsResponse> ListObjectVersions(ListObjectVersionsArgs args);
  Result<MakeBucketResponse> MakeBucket(MakeBucketArgs args);
  Result<PutObjectResponse> PutObject(PutObjectApiArgs args);
  Result<RemoveBucketResponse> RemoveBucket(RemoveBucketArgs args);
  Result<RemoveObjectResponse> RemoveObject(RemoveObjectArgs args);
  Result<RemoveObjectsResponse> RemoveObjects(RemoveObjectsApiArgs args);
  Result<SetBucketEncryptionResponse> SetBucketEncryption(
      SetBucketEncryptionArgs args);
  Result<SetBucketLifecycleResponse> SetBucketLifecycle(
      SetBucketLifecycleArgs args);
  Result<SetBucketNotificationResponse> SetBucketNotification(
      SetBucketNotificationArgs args);
  Result<SetBucketPolicyResponse> SetBucketPolicy(SetBucketPolicyArgs args);
  Result<SetBucketReplicationResponse> SetBucketReplication(
      SetBucketReplicationArgs args);
  Result<SetBucketTagsResponse> SetBucketTags(SetBucketTagsArgs args);
  Result<SetBucketVersioningResponse> SetBucketVersioning(
      SetBucketVersioningArgs args);
  Result<SetObjectLockConfigResponse> SetObjectLockConfig(
      SetObjectLockConfigArgs args);
  Result<SetObjectRetentionResponse> SetObjectRetention(
      SetObjectRetentionArgs args);
  Result<SetObjectTagsResponse> SetObjectTags(SetObjectTagsArgs args);
  Result<SelectObjectContentResponse> SelectObjectContent(
      SelectObjectContentArgs args);
  Result<StatObjectResponse> StatObject(StatObjectArgs args);
  Result<UploadPartResponse> UploadPart(UploadPartArgs args);
  Result<UploadPartCopyResponse> UploadPartCopy(UploadPartCopyArgs args);

  // Async overloads — return std::future<Result<T>> backed by std::async.
  // All sync methods now also return Result<T>.
  std::future<Result<AbortMultipartUploadResponse>> AbortMultipartUploadAsync(
      AbortMultipartUploadArgs args);
  std::future<Result<BucketExistsResponse>> BucketExistsAsync(
      BucketExistsArgs args);
  std::future<Result<CompleteMultipartUploadResponse>>
  CompleteMultipartUploadAsync(CompleteMultipartUploadArgs args);
  std::future<Result<CreateMultipartUploadResponse>> CreateMultipartUploadAsync(
      CreateMultipartUploadArgs args);
  std::future<Result<DeleteBucketEncryptionResponse>>
  DeleteBucketEncryptionAsync(DeleteBucketEncryptionArgs args);
  std::future<Result<DeleteBucketLifecycleResponse>> DeleteBucketLifecycleAsync(
      DeleteBucketLifecycleArgs args);
  std::future<Result<DeleteBucketNotificationResponse>>
  DeleteBucketNotificationAsync(DeleteBucketNotificationArgs args);
  std::future<Result<DeleteBucketPolicyResponse>> DeleteBucketPolicyAsync(
      DeleteBucketPolicyArgs args);
  std::future<Result<DeleteBucketReplicationResponse>>
  DeleteBucketReplicationAsync(DeleteBucketReplicationArgs args);
  std::future<Result<DeleteBucketTagsResponse>> DeleteBucketTagsAsync(
      DeleteBucketTagsArgs args);
  std::future<Result<DeleteObjectLockConfigResponse>>
  DeleteObjectLockConfigAsync(DeleteObjectLockConfigArgs args);
  std::future<Result<DeleteObjectTagsResponse>> DeleteObjectTagsAsync(
      DeleteObjectTagsArgs args);
  std::future<Result<DisableObjectLegalHoldResponse>>
  DisableObjectLegalHoldAsync(DisableObjectLegalHoldArgs args);
  std::future<Result<EnableObjectLegalHoldResponse>> EnableObjectLegalHoldAsync(
      EnableObjectLegalHoldArgs args);
  std::future<Result<GetBucketEncryptionResponse>> GetBucketEncryptionAsync(
      GetBucketEncryptionArgs args);
  std::future<Result<GetBucketLifecycleResponse>> GetBucketLifecycleAsync(
      GetBucketLifecycleArgs args);
  std::future<Result<GetBucketNotificationResponse>> GetBucketNotificationAsync(
      GetBucketNotificationArgs args);
  std::future<Result<GetBucketPolicyResponse>> GetBucketPolicyAsync(
      GetBucketPolicyArgs args);
  std::future<Result<GetBucketReplicationResponse>> GetBucketReplicationAsync(
      GetBucketReplicationArgs args);
  std::future<Result<GetBucketTagsResponse>> GetBucketTagsAsync(
      GetBucketTagsArgs args);
  std::future<Result<GetBucketVersioningResponse>> GetBucketVersioningAsync(
      GetBucketVersioningArgs args);
  std::future<Result<GetObjectResponse>> GetObjectAsync(GetObjectArgs args);
  std::future<Result<GetObjectLockConfigResponse>> GetObjectLockConfigAsync(
      GetObjectLockConfigArgs args);
  std::future<Result<GetObjectRetentionResponse>> GetObjectRetentionAsync(
      GetObjectRetentionArgs args);
  std::future<Result<GetObjectTagsResponse>> GetObjectTagsAsync(
      GetObjectTagsArgs args);
  std::future<Result<GetPresignedObjectUrlResponse>> GetPresignedObjectUrlAsync(
      GetPresignedObjectUrlArgs args);
  std::future<Result<GetPresignedPostFormDataResponse>>
  GetPresignedPostFormDataAsync(PostPolicy policy);
  std::future<Result<IsObjectLegalHoldEnabledResponse>>
  IsObjectLegalHoldEnabledAsync(IsObjectLegalHoldEnabledArgs args);
  std::future<Result<ListBucketsResponse>> ListBucketsAsync(
      ListBucketsArgs args);
  std::future<Result<ListBucketsResponse>> ListBucketsAsync();
  std::future<Result<ListenBucketNotificationResponse>>
  ListenBucketNotificationAsync(ListenBucketNotificationArgs args);
  std::future<Result<ListObjectsResponse>> ListObjectsV1Async(
      ListObjectsV1Args args);
  std::future<Result<ListObjectsResponse>> ListObjectsV2Async(
      ListObjectsV2Args args);
  std::future<Result<ListObjectsResponse>> ListObjectVersionsAsync(
      ListObjectVersionsArgs args);
  std::future<Result<MakeBucketResponse>> MakeBucketAsync(MakeBucketArgs args);
  std::future<Result<PutObjectResponse>> PutObjectAsync(PutObjectApiArgs args);
  std::future<Result<RemoveBucketResponse>> RemoveBucketAsync(
      RemoveBucketArgs args);
  std::future<Result<RemoveObjectResponse>> RemoveObjectAsync(
      RemoveObjectArgs args);
  std::future<Result<RemoveObjectsResponse>> RemoveObjectsAsync(
      RemoveObjectsApiArgs args);
  std::future<Result<SelectObjectContentResponse>> SelectObjectContentAsync(
      SelectObjectContentArgs args);
  std::future<Result<SetBucketEncryptionResponse>> SetBucketEncryptionAsync(
      SetBucketEncryptionArgs args);
  std::future<Result<SetBucketLifecycleResponse>> SetBucketLifecycleAsync(
      SetBucketLifecycleArgs args);
  std::future<Result<SetBucketNotificationResponse>> SetBucketNotificationAsync(
      SetBucketNotificationArgs args);
  std::future<Result<SetBucketPolicyResponse>> SetBucketPolicyAsync(
      SetBucketPolicyArgs args);
  std::future<Result<SetBucketReplicationResponse>> SetBucketReplicationAsync(
      SetBucketReplicationArgs args);
  std::future<Result<SetBucketTagsResponse>> SetBucketTagsAsync(
      SetBucketTagsArgs args);
  std::future<Result<SetBucketVersioningResponse>> SetBucketVersioningAsync(
      SetBucketVersioningArgs args);
  std::future<Result<SetObjectLockConfigResponse>> SetObjectLockConfigAsync(
      SetObjectLockConfigArgs args);
  std::future<Result<SetObjectRetentionResponse>> SetObjectRetentionAsync(
      SetObjectRetentionArgs args);
  std::future<Result<SetObjectTagsResponse>> SetObjectTagsAsync(
      SetObjectTagsArgs args);
  std::future<Result<StatObjectResponse>> StatObjectAsync(StatObjectArgs args);
  std::future<Result<UploadPartResponse>> UploadPartAsync(UploadPartArgs args);
  std::future<Result<UploadPartCopyResponse>> UploadPartCopyAsync(
      UploadPartCopyArgs args);

  // Windows API fix:
  //
  // Windows API headers define `GetObject()` as a macro that expands to either
  // `GetObjectA()` or `GetObjectW()`. This means that users can get link errors
  // in case that one compilation unit used `GetObject()` macro and other
  // didn't. This fixes the issue by providing both functions `GetObject()` can
  // expand to as inline wrappers.
#if defined(_WIN32)
  inline Result<GetObjectResponse> GetObjectA(const GetObjectArgs& args) {
    return GetObject(args);
  }

  inline Result<GetObjectResponse> GetObjectW(const GetObjectArgs& args) {
    return GetObject(args);
  }
#endif  // _WIN32

};  // class BaseClient

}  // namespace minio::s3

#if defined(_WIN32) && defined(MINIO_CPP_GET_OBJECT_DEFINED)
#undef MINIO_CPP_GET_OBJECT_DEFINED
#pragma pop_macro("GetObject")
#endif

#endif  // MINIO_CPP_BASECLIENT_H_INCLUDED
