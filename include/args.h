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

#include "http.h"
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

  PutObjectArgs(std::istream &stream, long objectsize, long partsize);
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
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_S3_ARGS_H
