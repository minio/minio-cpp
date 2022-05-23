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
  std::string error;

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
  Response(error::Error err);
  Response(const Response& response);
  operator bool() const {
    return error.empty() && code.empty() && message.empty() &&
           (status_code == 0 || status_code >= 200 && status_code <= 299);
  }
  std::string GetError();
  static Response ParseXML(std::string_view data, int status_code,
                           utils::Multimap headers);
};  // struct Response

struct GetRegionResponse : public Response {
  std::string region;

  GetRegionResponse(std::string regionvalue);
  GetRegionResponse(error::Error err);
  GetRegionResponse(const Response& response);
};  // struct GetRegionResponse

using MakeBucketResponse = Response;

struct ListBucketsResponse : public Response {
  std::list<Bucket> buckets;

  ListBucketsResponse(std::list<Bucket> bucketlist);
  ListBucketsResponse(error::Error err);
  ListBucketsResponse(const Response& response);
  static ListBucketsResponse ParseXML(std::string_view data);
};  // struct ListBucketsResponse

struct BucketExistsResponse : public Response {
  bool exist = false;

  BucketExistsResponse(bool existflag);
  BucketExistsResponse(error::Error err);
  BucketExistsResponse(const Response& response);
};  // struct BucketExistsResponse

using RemoveBucketResponse = Response;

using AbortMultipartUploadResponse = Response;

struct CompleteMultipartUploadResponse : public Response {
  std::string location;
  std::string etag;
  std::string version_id;

  CompleteMultipartUploadResponse();
  CompleteMultipartUploadResponse(error::Error err);
  CompleteMultipartUploadResponse(const Response& response);
  static CompleteMultipartUploadResponse ParseXML(std::string_view data,
                                                  std::string version_id);
};  // struct CompleteMultipartUploadResponse

struct CreateMultipartUploadResponse : public Response {
  std::string upload_id;

  CreateMultipartUploadResponse(std::string uploadid);
  CreateMultipartUploadResponse(error::Error err);
  CreateMultipartUploadResponse(const Response& response);
};  // struct CreateMultipartUploadResponse

struct PutObjectResponse : public Response {
  std::string etag;
  std::string version_id;

  PutObjectResponse();
  PutObjectResponse(error::Error err);
  PutObjectResponse(const Response& response);
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

  StatObjectResponse();
  StatObjectResponse(error::Error err);
  StatObjectResponse(const Response& response);
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

  Item();
  Item(error::Error err);
  Item(const Response& response);
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

  ListObjectsResponse();
  ListObjectsResponse(error::Error err);
  ListObjectsResponse(const Response& response);
  static ListObjectsResponse ParseXML(std::string_view data, bool version);
};  // struct ListObjectsResponse

using CopyObjectResponse = PutObjectResponse;

using ComposeObjectResponse = PutObjectResponse;

using UploadObjectResponse = PutObjectResponse;
}  // namespace s3
}  // namespace minio

#endif  // #ifndef _MINIO_S3_RESPONSE_H
