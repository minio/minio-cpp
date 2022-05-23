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

#ifndef _MINIO_S3_CLIENT_H
#define _MINIO_S3_CLIENT_H

#include <fstream>

#include "args.h"
#include "request-builder.h"
#include "response.h"

namespace minio {
namespace s3 {
utils::Multimap GetCommonListObjectsQueryParams(std::string delimiter,
                                                std::string encoding_type,
                                                unsigned int max_keys,
                                                std::string prefix);

class ListObjectsResult;

/**
 * Simple Storage Service (aka S3) client to perform bucket and object
 * operations asynchronously.
 */
class Client {
 private:
  http::BaseUrl& base_url_;
  creds::Provider* provider_ = NULL;
  std::map<std::string, std::string> region_map_;
  bool debug_ = false;
  bool ignore_cert_check_ = false;

 public:
  Client(http::BaseUrl& base_url, creds::Provider* provider = NULL);

  void Debug(bool flag) { debug_ = flag; }

  void IgnoreCertCheck(bool flag) { ignore_cert_check_ = flag; }

  void HandleRedirectResponse(std::string& code, std::string& message,
                              int status_code, http::Method method,
                              utils::Multimap headers,
                              std::string_view bucket_name, bool retry = false);
  Response GetErrorResponse(http::Response resp, std::string_view resource,
                            http::Method method, std::string_view bucket_name,
                            std::string_view object_name);
  Response execute(RequestBuilder& builder);
  Response Execute(RequestBuilder& builder);

  // S3 APIs
  ListObjectsResponse ListObjectsV1(ListObjectsV1Args args);
  ListObjectsResponse ListObjectsV2(ListObjectsV2Args args);
  ListObjectsResponse ListObjectVersions(ListObjectVersionsArgs args);

  // Bucket operations
  GetRegionResponse GetRegion(std::string_view bucket_name,
                              std::string_view region = "");
  MakeBucketResponse MakeBucket(MakeBucketArgs args);
  ListBucketsResponse ListBuckets(ListBucketsArgs args);
  ListBucketsResponse ListBuckets();
  BucketExistsResponse BucketExists(BucketExistsArgs args);
  RemoveBucketResponse RemoveBucket(RemoveBucketArgs args);

  // Object operations
  AbortMultipartUploadResponse AbortMultipartUpload(
      AbortMultipartUploadArgs args);
  CompleteMultipartUploadResponse CompleteMultipartUpload(
      CompleteMultipartUploadArgs args);
  CreateMultipartUploadResponse CreateMultipartUpload(
      CreateMultipartUploadArgs args);
  PutObjectResponse PutObject(PutObjectApiArgs args);
  UploadPartResponse UploadPart(UploadPartArgs args);
  UploadPartCopyResponse UploadPartCopy(UploadPartCopyArgs args);
  StatObjectResponse StatObject(StatObjectArgs args);
  RemoveObjectResponse RemoveObject(RemoveObjectArgs args);
  DownloadObjectResponse DownloadObject(DownloadObjectArgs args);
  GetObjectResponse GetObject(GetObjectArgs args);
  ListObjectsResult ListObjects(ListObjectsArgs args);
  PutObjectResponse PutObject(PutObjectArgs& args, std::string& upload_id,
                              char* buf);
  PutObjectResponse PutObject(PutObjectArgs args);
  CopyObjectResponse CopyObject(CopyObjectArgs args);
  StatObjectResponse CalculatePartCount(size_t& part_count,
                                        std::list<ComposeSource> sources);
  ComposeObjectResponse ComposeObject(ComposeObjectArgs args,
                                      std::string& upload_id);
  ComposeObjectResponse ComposeObject(ComposeObjectArgs args);
  UploadObjectResponse UploadObject(UploadObjectArgs args);
};  // class Client

class ListObjectsResult {
 private:
  Client* client_ = NULL;
  ListObjectsArgs* args_ = NULL;
  bool failed_ = false;
  ListObjectsResponse resp_;
  std::list<Item>::iterator itr_;

  void Populate();

 public:
  ListObjectsResult(error::Error err);
  ListObjectsResult(Client* client, ListObjectsArgs* args);
  Item& operator*() const { return *itr_; }
  operator bool() const { return itr_ != resp_.contents.end(); }
  ListObjectsResult& operator++() {
    itr_++;
    if (!failed_ && itr_ == resp_.contents.end() && resp_.is_truncated) {
      Populate();
    }
    return *this;
  }
  ListObjectsResult operator++(int) {
    ListObjectsResult curr = *this;
    ++(*this);
    return curr;
  }
};  // class ListObjectsResult
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_S3_CLIENT_H
