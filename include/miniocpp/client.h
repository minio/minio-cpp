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

#ifndef MINIO_CPP_CLIENT_H_INCLUDED
#define MINIO_CPP_CLIENT_H_INCLUDED

#include <future>
#include <list>
#include <string>

#include "args.h"
#include "baseclient.h"
#include "error.h"
#include "providers.h"
#include "request.h"
#include "response.h"
#include "result.h"

namespace minio::s3 {

class Client;

class ListObjectsResult {
 private:
  Client* client_ = nullptr;
  ListObjectsArgs args_;
  bool failed_ = false;
  std::shared_ptr<ListObjectsResponse> resp_;
  std::list<Item>::iterator itr_;
  std::shared_ptr<std::shared_future<std::shared_ptr<ListObjectsResponse>>>
      prefetch_future_;

  void Populate();
  void StartPrefetch();
  void UpdatePaginationArgs();

 public:
  explicit ListObjectsResult(error::Error err);
  ListObjectsResult(Client* const client, const ListObjectsArgs& args);
  ListObjectsResult(Client* const client, ListObjectsArgs&& args);
  ~ListObjectsResult() = default;
  ListObjectsResult(const ListObjectsResult&) = default;
  ListObjectsResult& operator=(const ListObjectsResult&) = default;
  ListObjectsResult(ListObjectsResult&&) = default;
  ListObjectsResult& operator=(ListObjectsResult&&) = default;

  Item& operator*() const { return *itr_; }
  explicit operator bool() {
    if (prefetch_future_ && (!resp_ || resp_->contents.empty())) Populate();
    return itr_ != resp_->contents.end();
  }
  explicit operator bool() const { return itr_ != resp_->contents.end(); }

  ListObjectsResult& operator++() {
    itr_++;
    if (!failed_ && itr_ == resp_->contents.end() && resp_->is_truncated) {
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
  Client* client_ = nullptr;
  RemoveObjectsArgs args_;
  bool done_ = false;
  RemoveObjectsResponse resp_;
  std::list<DeleteError>::iterator itr_;

  void Populate();

 public:
  explicit RemoveObjectsResult(error::Error err);
  RemoveObjectsResult(Client* const client, const RemoveObjectsArgs& args);
  RemoveObjectsResult(Client* const client, RemoveObjectsArgs&& args);
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
class Client : public BaseClient {
 protected:
  Result<StatObjectResponse> CalculatePartCount(
      size_t& part_count, std::list<ComposeSource> sources);
  Result<ComposeObjectResponse> ComposeObject(ComposeObjectArgs args,
                                              std::string& upload_id);
  Result<PutObjectResponse> PutObject(PutObjectArgs args,
                                      std::string& upload_id, char* buf);

#ifdef MINIO_CPP_RDMA
  // Process-wide RDMA client, one instance for the whole process, lazily
  // initialised on first use via std::call_once. Reasons this has to be
  // process-wide rather than per-Client instance:
  //   (1) libcuobjclient drives libcufile which maintains process-global
  //       state (cufile.json, device/peer cache, health monitor threads,
  //       multipath registration). Two concurrent constructors corrupt
  //       the glibc heap ("malloc(): invalid size (unsorted)").
  //   (2) Even after init, reusing a single client avoids the per-call
  //       connect+register race that used to make isConnected() flap
  //       under concurrency and silently skip RDMA in favour of HTTP.
  // See SharedRDMAClient() in client.cc for the definition.
  static cuObjClient& SharedRDMAClient();
#endif

 public:
  explicit Client(BaseUrl& base_url, creds::Provider* const provider = nullptr);
  ~Client() = default;

  Result<ComposeObjectResponse> ComposeObject(ComposeObjectArgs args);
  Result<CopyObjectResponse> CopyObject(CopyObjectArgs args);
  Result<DownloadObjectResponse> DownloadObject(DownloadObjectArgs args);
  ListObjectsResult ListObjects(ListObjectsArgs args);
  Result<PutObjectResponse> PutObject(PutObjectArgs args);
  Result<GetObjectResponse> GetObject(GetObjectArgs args);
  Result<UploadObjectResponse> UploadObject(UploadObjectArgs args);
  RemoveObjectsResult RemoveObjects(RemoveObjectsArgs args);

  // Async overloads — return std::future<T> backed by std::async.
  //
  // Lifetime note for PutObjectAsync: the caller must ensure
  // args.stream (if set) outlives the returned std::future<T>,
  // exactly as it must for the synchronous PutObject call.
  std::future<Result<ComposeObjectResponse>> ComposeObjectAsync(
      ComposeObjectArgs args);
  std::future<Result<CopyObjectResponse>> CopyObjectAsync(CopyObjectArgs args);
  std::future<Result<DownloadObjectResponse>> DownloadObjectAsync(
      DownloadObjectArgs args);
  std::future<Result<GetObjectResponse>> GetObjectAsync(GetObjectArgs args);
  std::future<ListObjectsResult> ListObjectsAsync(ListObjectsArgs args);
  std::future<Result<PutObjectResponse>> PutObjectAsync(PutObjectArgs args);
  std::future<Result<UploadObjectResponse>> UploadObjectAsync(
      UploadObjectArgs args);
};  // class Client

}  // namespace minio::s3

#endif  // MINIO_CPP_CLIENT_H_INCLUDED
