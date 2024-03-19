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

#include <list>
#include <memory>
#include <string>

#include "args.h"
#include "baseclient.h"
#include "error.h"
#include "providers.h"
#include "request.h"
#include "response.h"

namespace minio {
namespace s3 {
class Client;

class ListObjectsResult {
 private:
  std::shared_ptr<Client> client_ = nullptr;
  ListObjectsArgs args_;
  bool failed_ = false;
  ListObjectsResponse resp_;
  std::list<Item>::iterator itr_;

  void Populate();

 public:
  explicit ListObjectsResult(error::Error err);
  ListObjectsResult(std::shared_ptr<Client> client,
                    const ListObjectsArgs& args);
  ListObjectsResult(std::shared_ptr<Client> client, ListObjectsArgs&& args);
  ~ListObjectsResult() = default;

  Item& operator*() const { return *itr_; }
  explicit operator bool() const { return itr_ != resp_.contents.end(); }

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

class RemoveObjectsResult {
 private:
  std::shared_ptr<Client> client_ = nullptr;
  RemoveObjectsArgs args_;
  bool done_ = false;
  RemoveObjectsResponse resp_;
  std::list<DeleteError>::iterator itr_;

  void Populate();

 public:
  explicit RemoveObjectsResult(error::Error err);
  RemoveObjectsResult(std::shared_ptr<Client> client,
                      const RemoveObjectsArgs& args);
  RemoveObjectsResult(std::shared_ptr<Client> client, RemoveObjectsArgs&& args);
  ~RemoveObjectsResult() = default;

  DeleteError& operator*() const { return *itr_; }
  explicit operator bool() const { return itr_ != resp_.errors.end(); }
  RemoveObjectsResult& operator++() {
    itr_++;
    if (!done_ && itr_ == resp_.errors.end()) {
      Populate();
    }
    return *this;
  }
  RemoveObjectsResult operator++(int) {
    RemoveObjectsResult curr = *this;
    ++(*this);
    return curr;
  }
};  // class RemoveObjectsResult

/**
 * Simple Storage Service (aka S3) client to perform bucket and object
 * operations.
 */
class Client : public BaseClient, std::enable_shared_from_this<Client> {
 protected:
  StatObjectResponse CalculatePartCount(size_t& part_count,
                                        std::list<ComposeSource> sources);
  ComposeObjectResponse ComposeObject(ComposeObjectArgs args,
                                      std::string& upload_id);
  PutObjectResponse PutObject(PutObjectArgs args, std::string& upload_id,
                              char* buf);

 public:
  explicit Client(const BaseUrl& base_url,
                  std::shared_ptr<creds::Provider> provider = nullptr);
  ~Client() = default;

  ComposeObjectResponse ComposeObject(ComposeObjectArgs args);
  CopyObjectResponse CopyObject(CopyObjectArgs args);
  DownloadObjectResponse DownloadObject(DownloadObjectArgs args);
  ListObjectsResult ListObjects(ListObjectsArgs args);
  PutObjectResponse PutObject(PutObjectArgs args);
  UploadObjectResponse UploadObject(UploadObjectArgs args);
  RemoveObjectsResult RemoveObjects(RemoveObjectsArgs args);
};  // class Client
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_S3_CLIENT_H
