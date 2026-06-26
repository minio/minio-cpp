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

#include <miniocpp/args.h>
#include <miniocpp/client.h>
#include <miniocpp/http.h>
#include <miniocpp/providers.h>
#include <miniocpp/request.h>
#include <miniocpp/response.h>
#include <miniocpp/types.h>
#include <miniocpp/utils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iosfwd>
#include <iostream>
#include <list>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>

thread_local static std::mt19937 rg{std::random_device{}()};

const static std::string charset =
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

thread_local static std::uniform_int_distribution<std::string::size_type> pick(
    0, charset.length() - 2);

class RandomBuf : public std::streambuf {
 private:
  size_t size_;
  std::array<char, 64> buf_;

 protected:
  int_type underflow() override {
    if (size_ == 0) return EOF;

    size_t size = std::min<size_t>(size_, buf_.size());
    auto* const data = buf_.data();
    setg(data, data, data + size);
    for (size_t i = 0; i < size; ++i) buf_[i] = charset[pick(rg)];
    size_ -= size;
    return 0;
  }

 public:
  RandomBuf(size_t size) : size_(size) {}
};

class RandCharStream : private RandomBuf, public std::istream {
 public:
  explicit RandCharStream(size_t size);
};

RandCharStream::RandCharStream(size_t size)
    : RandomBuf(size), std::istream(this) {}

std::string RandomString(std::string chrs, std::string::size_type length) {
  thread_local static std::uniform_int_distribution<std::string::size_type>
      pick(0, chrs.length() - 2);

  std::string s;
  s.reserve(length);
  while (length--) s += chrs[pick(rg)];
  return s;
}

std::string RandBucketName() {
  return RandomString("0123456789abcdefghijklmnopqrstuvwxyz", 8);
}

std::string RandObjectName() { return RandomString(charset, 8); }

struct MakeBucketError : public std::runtime_error {
  MakeBucketError(std::string err) : runtime_error(err) {}
};

struct RemoveBucketError : public std::runtime_error {
  RemoveBucketError(std::string err) : runtime_error(err) {}
};

struct BucketExistsError : public std::runtime_error {
  BucketExistsError(std::string err) : runtime_error(err) {}
};

class Tests {
 private:
  minio::s3::Client& client_;
  std::string bucket_name_;

 public:
  Tests(minio::s3::Client& client) : client_(client) {
    bucket_name_ = RandBucketName();
    minio::s3::MakeBucketArgs args;
    args.bucket = bucket_name_;
    minio::s3::MakeBucketResponse resp = client_.MakeBucket(args);
    if (!resp) {
      throw std::runtime_error("MakeBucket(): " + resp.Error().String());
    }
  }

  ~Tests() noexcept(false) {
    minio::s3::RemoveBucketArgs args;
    args.bucket = bucket_name_;
    minio::s3::RemoveBucketResponse resp = client_.RemoveBucket(args);
    if (!resp) {
      throw std::runtime_error("RemoveBucket(): " + resp.Error().String());
    }
  }

  void MakeBucket(std::string bucket_name) noexcept(false) {
    minio::s3::MakeBucketArgs args;
    args.bucket = bucket_name;
    minio::s3::MakeBucketResponse resp = client_.MakeBucket(args);
    if (resp) return;
    throw MakeBucketError("MakeBucket(): " + resp.Error().String());
  }

  void RemoveBucket(std::string bucket_name) noexcept(false) {
    minio::s3::RemoveBucketArgs args;
    args.bucket = bucket_name;
    minio::s3::RemoveBucketResponse resp = client_.RemoveBucket(args);
    if (resp) return;
    throw RemoveBucketError("RemoveBucket(): " + resp.Error().String());
  }

  void RemoveObject(std::string bucket_name, std::string object_name) {
    minio::s3::RemoveObjectArgs args;
    args.bucket = bucket_name;
    args.object = object_name;
    minio::s3::RemoveObjectResponse resp = client_.RemoveObject(args);
    if (!resp) {
      throw std::runtime_error("RemoveObject(): " + resp.Error().String());
    }
  }

  void RemoveObjects(std::list<std::string> objects) {
    minio::s3::RemoveObjectsArgs args;
    args.bucket = bucket_name_;

    std::list<minio::s3::DeleteObject> delete_objects;
    for (auto& object : objects) {
      delete_objects.push_back(minio::s3::DeleteObject{object});
    }

    std::list<minio::s3::DeleteObject>::iterator i = delete_objects.begin();
    args.func = [&delete_objects = delete_objects,
                 &i = i](minio::s3::DeleteObject& object) -> bool {
      if (i == delete_objects.end()) return false;
      object = *i;
      i++;
      return true;
    };

    minio::s3::RemoveObjectsResult result = client_.RemoveObjects(args);
    std::string msg;
    for (; result; result++) {
      minio::s3::DeleteError err = *result;
      if (!err) {
        throw std::runtime_error("RemoveObjects(): " + err.Error().String());
      }

      if (msg.empty()) msg = "unable to remove object(s)";
      msg += "; " + err.object_name;
      if (!err.version_id.empty()) msg += "?versionId=" + err.version_id;
    }
    if (!msg.empty()) throw std::runtime_error("RemoveObjects(): " + msg);
  }

  void MakeBucket() {
    std::cout << "MakeBucket()" << std::endl;

    std::string bucket_name = RandBucketName();
    MakeBucket(bucket_name);
    RemoveBucket(bucket_name);
  }

  void RemoveBucket() {
    std::cout << "RemoveBucket()" << std::endl;

    std::string bucket_name = RandBucketName();
    MakeBucket(bucket_name);
    RemoveBucket(bucket_name);
  }

  void BucketExists() {
    std::cout << "BucketExists()" << std::endl;

    std::string bucket_name = RandBucketName();
    try {
      MakeBucket(bucket_name);
      minio::s3::BucketExistsArgs args;
      args.bucket = bucket_name;
      minio::s3::BucketExistsResponse resp = client_.BucketExists(args);
      if (!resp) {
        throw BucketExistsError("BucketExists(): " + resp.Error().String());
      }
      if (!resp.exist) {
        throw std::runtime_error("BucketExists(): expected: true; got: false");
      }
      RemoveBucket(bucket_name);
    } catch (const MakeBucketError&) {
      throw;
    } catch (const std::runtime_error&) {
      RemoveBucket(bucket_name);
      throw;
    }
  }

  void ListBuckets() {
    std::cout << "ListBuckets()" << std::endl;

    std::list<std::string> bucket_names;
    try {
      for (int i = 0; i < 3; i++) {
        std::string bucket_name = RandBucketName();
        MakeBucket(bucket_name);
        bucket_names.push_back(bucket_name);
      }

      minio::s3::ListBucketsResponse resp = client_.ListBuckets();
      if (!resp) {
        throw std::runtime_error("ListBuckets(): " + resp.Error().String());
      }

      std::size_t c = 0;
      for (auto& bucket : resp.buckets) {
        if (std::find(bucket_names.begin(), bucket_names.end(), bucket.name) !=
            bucket_names.end()) {
          c++;
        }
      }
      if (c != bucket_names.size()) {
        throw std::runtime_error(
            "ListBuckets(): expected: " + std::to_string(bucket_names.size()) +
            "; got: " + std::to_string(c));
      }
      for (auto& bucket_name : bucket_names) RemoveBucket(bucket_name);
    } catch (const std::runtime_error&) {
      for (auto& bucket_name : bucket_names) RemoveBucket(bucket_name);
      throw;
    }
  }

  void StatObject() {
    std::cout << "StatObject()" << std::endl;

    std::string object_name = RandObjectName();

    std::string data = "StatObject()";
    std::stringstream ss(data);
    minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()), 0);
    args.bucket = bucket_name_;
    args.object = object_name;
    minio::s3::PutObjectResponse resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.Error().String());
    }
    try {
      minio::s3::StatObjectArgs args;
      args.bucket = bucket_name_;
      args.object = object_name;
      minio::s3::StatObjectResponse resp = client_.StatObject(args);
      if (!resp) {
        throw std::runtime_error("StatObject(): " + resp.Error().String());
      }
      if (resp.size != data.length()) {
        throw std::runtime_error(
            "StatObject(): expected: " + std::to_string(data.length()) +
            "; got: " + std::to_string(resp.size));
      }
      RemoveObject(bucket_name_, object_name);
    } catch (const std::runtime_error&) {
      RemoveObject(bucket_name_, object_name);
      throw;
    }
  }

  void RemoveObject() {
    std::cout << "RemoveObject()" << std::endl;

    std::string object_name = RandObjectName();
    std::string data = "RemoveObject()";
    std::stringstream ss(data);
    minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()), 0);
    args.bucket = bucket_name_;
    args.object = object_name;
    minio::s3::PutObjectResponse resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.Error().String());
    }
    RemoveObject(bucket_name_, object_name);
  }

  void DownloadObject() {
    std::cout << "DownloadObject()" << std::endl;

    std::string object_name = RandObjectName();

    std::string data = "DownloadObject()";
    std::stringstream ss(data);
    minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()), 0);
    args.bucket = bucket_name_;
    args.object = object_name;
    minio::s3::PutObjectResponse resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.Error().String());
    }

    try {
      std::string filename = RandObjectName();
      minio::s3::DownloadObjectArgs args;
      args.bucket = bucket_name_;
      args.object = object_name;
      args.filename = filename;
      minio::s3::DownloadObjectResponse resp = client_.DownloadObject(args);
      if (!resp) {
        throw std::runtime_error("DownloadObject(): " + resp.Error().String());
      }

      std::ifstream file(filename);
      file.seekg(0, std::ios::end);
      size_t length = file.tellg();
      file.seekg(0, std::ios::beg);
      char* buf = new char[length];
      file.read(buf, length);
      file.close();

      if (data != std::string(buf, length)) {
        throw std::runtime_error("DownloadObject(): expected: " + data +
                                 "; got: " + buf);
      }
      std::filesystem::remove(filename);
      RemoveObject(bucket_name_, object_name);
    } catch (const std::runtime_error&) {
      RemoveObject(bucket_name_, object_name);
      throw;
    }
  }

  void GetObject() {
    std::cout << "GetObject()" << std::endl;

    std::string object_name = RandObjectName();

    std::string data = "GetObject()";
    std::stringstream ss(data);
    minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()), 0);
    args.bucket = bucket_name_;
    args.object = object_name;
    minio::s3::PutObjectResponse resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.Error().String());
    }

    try {
      minio::s3::GetObjectArgs args;
      args.bucket = bucket_name_;
      args.object = object_name;
      std::string content;
      args.datafunc =
          [&content = content](minio::http::DataFunctionArgs args) -> bool {
        content += args.datachunk;
        return true;
      };
      minio::s3::GetObjectResponse resp = client_.GetObject(args);
      if (!resp) {
        throw std::runtime_error("GetObject(): " + resp.Error().String());
      }
      if (data != content) {
        throw std::runtime_error("GetObject(): expected: " + data +
                                 "; got: " + content);
      }
      RemoveObject(bucket_name_, object_name);
    } catch (const std::runtime_error&) {
      RemoveObject(bucket_name_, object_name);
      throw;
    }
  }

  void listObjects(std::string testname, int count) {
    std::cout << testname << std::endl;

    std::list<std::string> object_names;
    try {
      for (int i = 0; i < count; i++) {
        std::string object_name = RandObjectName();
        std::stringstream ss;
        minio::s3::PutObjectArgs args(ss, 0, 0);
        args.bucket = bucket_name_;
        args.object = object_name;
        minio::s3::PutObjectResponse resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.Error().String());
        }
        object_names.push_back(object_name);
      }

      std::size_t c = 0;
      minio::s3::ListObjectsArgs args;
      args.bucket = bucket_name_;
      minio::s3::ListObjectsResult result = client_.ListObjects(args);
      for (; result; result++) {
        minio::s3::Item item = *result;
        if (!item) {
          throw std::runtime_error("ListObjects(): " + item.Error().String());
        }
        if (std::find(object_names.begin(), object_names.end(), item.name) !=
            object_names.end()) {
          c++;
        }
      }

      if (c != object_names.size()) {
        throw std::runtime_error(
            "ListObjects(): expected: " + std::to_string(object_names.size()) +
            "; got: " + std::to_string(c));
      }
      RemoveObjects(object_names);
    } catch (const std::runtime_error&) {
      RemoveObjects(object_names);
      throw;
    }
  }

  void ListObjects() { listObjects("ListObjects()", 3); }

  void ListObjects1010() { listObjects("ListObjects() 1010 objects", 1010); }

  void PutObject() {
    std::cout << "PutObject()" << std::endl;

    {
      std::string object_name = RandObjectName();
      std::string data = "PutObject()";
      std::stringstream ss(data);
      minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()),
                                    0);
      args.bucket = bucket_name_;
      args.object = object_name;
      minio::s3::PutObjectResponse resp = client_.PutObject(args);
      if (!resp) {
        throw std::runtime_error("PutObject(): " + resp.Error().String());
      }
      RemoveObject(bucket_name_, object_name);
    }

    {
      std::string object_name = RandObjectName();
      size_t size = 67108865;  // (64MiB + 1) bytes
      RandCharStream stream(size);
      minio::s3::PutObjectArgs args(stream, static_cast<uint64_t>(size), 0);
      args.bucket = bucket_name_;
      args.object = object_name;
      minio::s3::PutObjectResponse resp = client_.PutObject(args);
      if (!resp) {
        throw std::runtime_error("<Multipart> PutObject(): " +
                                 resp.Error().String());
      }
      if (resp.etag == "") {
        throw std::runtime_error("<Multipart> PutObject(): etag is missing");
      }
      RemoveObject(bucket_name_, object_name);
    }
  }

  void CopyObject() {
    std::cout << "CopyObject()" << std::endl;

    std::string object_name = RandObjectName();
    std::string src_object_name = RandObjectName();
    std::string data = "CopyObject()";
    std::stringstream ss(data);
    minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()), 0);
    args.bucket = bucket_name_;
    args.object = src_object_name;
    minio::s3::PutObjectResponse resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.Error().String());
    }

    try {
      minio::s3::CopySource source;
      source.bucket = bucket_name_;
      source.object = src_object_name;
      minio::s3::CopyObjectArgs args;
      args.bucket = bucket_name_;
      args.object = object_name;
      args.source = source;
      minio::s3::CopyObjectResponse resp = client_.CopyObject(args);
      if (!resp) {
        throw std::runtime_error("CopyObject(): " + resp.Error().String());
      }
      RemoveObject(bucket_name_, src_object_name);
      RemoveObject(bucket_name_, object_name);
    } catch (const std::runtime_error&) {
      RemoveObject(bucket_name_, src_object_name);
      RemoveObject(bucket_name_, object_name);
      throw;
    }
  }

  void UploadObject() {
    std::cout << "UploadObject()" << std::endl;

    std::string data = "UploadObject()";
    std::string filename = RandObjectName();
    std::ofstream file(filename);
    file << data;
    file.close();

    std::string object_name = RandObjectName();
    minio::s3::UploadObjectArgs args;
    args.bucket = bucket_name_;
    args.object = object_name;
    args.filename = filename;
    minio::s3::UploadObjectResponse resp = client_.UploadObject(args);
    if (!resp) {
      throw std::runtime_error("UploadObject(): " + resp.Error().String());
    }
    std::filesystem::remove(filename);
    RemoveObject(bucket_name_, object_name);
  }

  void RemoveObjects() {
    std::cout << "RemoveObjects()" << std::endl;

    std::list<std::string> object_names;
    try {
      for (int i = 0; i < 3; i++) {
        std::string object_name = RandObjectName();
        std::stringstream ss;
        minio::s3::PutObjectArgs args(ss, 0, 0);
        args.bucket = bucket_name_;
        args.object = object_name;
        minio::s3::PutObjectResponse resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.Error().String());
        }
        object_names.push_back(object_name);
      }
      RemoveObjects(object_names);
    } catch (const std::runtime_error&) {
      RemoveObjects(object_names);
      throw;
    }
  }

  void SelectObjectContent() {
    std::cout << "SelectObjectContent()" << std::endl;

    std::string object_name = RandObjectName();

    std::string data =
        "1997,Ford,E350,\"ac, abs, moon\",3000.00\n"
        "1999,Chevy,\"Venture \"\"Extended Edition\"\"\",,4900.00\n"
        "1999,Chevy,\"Venture \"\"Extended Edition, Very Large\"\"\",,5000.00\n"
        "1996,Jeep,Grand Cherokee,\"MUST SELL!\n"
        "air, moon roof, loaded\",4799.00\n";
    std::stringstream ss("Year,Make,Model,Description,Price\n" + data);
    minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(ss.str().length()),
                                  0);
    args.bucket = bucket_name_;
    args.object = object_name;
    minio::s3::PutObjectResponse resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.Error().String());
    }

    std::string expression = "select * from S3Object";
    minio::s3::CsvInputSerialization csv_input;
    minio::s3::FileHeaderInfo file_header_info =
        minio::s3::FileHeaderInfo::kUse;
    csv_input.file_header_info =
        std::make_shared<minio::s3::FileHeaderInfo>(file_header_info);
    minio::s3::CsvOutputSerialization csv_output;
    minio::s3::QuoteFields quote_fields = minio::s3::QuoteFields::kAsNeeded;
    csv_output.quote_fields =
        std::make_shared<minio::s3::QuoteFields>(quote_fields);
    minio::s3::SelectRequest request(expression, &csv_input, &csv_output);

    try {
      std::string records;
      auto func = [&records = records](minio::s3::SelectResult result) -> bool {
        if (result.err) {
          throw std::runtime_error("SelectResult: " + result.err.String());
          return false;
        }
        records += result.records;
        return true;
      };
      minio::s3::SelectObjectContentArgs args(request, func);
      args.bucket = bucket_name_;
      args.object = object_name;
      minio::s3::SelectObjectContentResponse resp =
          client_.SelectObjectContent(args);
      if (!resp) {
        if (resp.code == "MethodNotAllowed") {
          std::cout << "  skipped: server does not implement S3 Select"
                    << std::endl;
          RemoveObject(bucket_name_, object_name);
          return;
        }
        throw std::runtime_error("SelectObjectContent(): " +
                                 resp.Error().String());
      }
      if (records != data) {
        throw std::runtime_error("expected: " + data + ", got: " + records);
      }
      RemoveObject(bucket_name_, object_name);
    } catch (const std::runtime_error&) {
      RemoveObject(bucket_name_, object_name);
      throw;
    }
  }

  void ListenBucketNotification() {
    std::cout << "ListenBucketNotification()" << std::endl;

    std::list<minio::s3::NotificationRecord> records;
    std::thread task{[&client_ = client_, &bucket_name_ = bucket_name_,
                      &records = records]() {
      minio::s3::ListenBucketNotificationArgs args;
      args.bucket = bucket_name_;
      args.func = [&records = records](
                      std::list<minio::s3::NotificationRecord> values) -> bool {
        records.insert(records.end(), values.begin(), values.end());
        return false;
      };
      minio::s3::ListenBucketNotificationResponse resp =
          client_.ListenBucketNotification(args);
      if (!resp) {
        throw std::runtime_error("ListenBucketNotification(): " +
                                 resp.Error().String());
      }
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::string object_name = RandObjectName();
    try {
      std::string data = "ListenBucketNotification()";
      std::stringstream ss(data);
      minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()),
                                    0);
      args.bucket = bucket_name_;
      args.object = object_name;
      minio::s3::PutObjectResponse resp = client_.PutObject(args);
      if (!resp) {
        throw std::runtime_error("PutObject(): " + resp.Error().String());
      }

      task.join();

      if (records.empty()) {
        throw std::runtime_error(
            "ListenBucketNotification(): records length: expected: 1, got: 0");
      }

      minio::s3::NotificationRecord record = records.front();
      if (record.event_name != "s3:ObjectCreated:Put") {
        throw std::runtime_error(
            "ListenBucketNotification(): record.event_name: expected: "
            "s3:ObjectCreated:Put, got: " +
            record.event_name);
      }

      if (record.s3.bucket.name != bucket_name_) {
        throw std::runtime_error(
            "ListenBucketNotification(): record.s3.bucket.name: expected: " +
            bucket_name_ + ", got: " + record.s3.bucket.name);
      }

      if (record.s3.object.key != object_name) {
        throw std::runtime_error(
            "ListenBucketNotification(): record.s3.object.name: expected: " +
            object_name + ", got: " + record.s3.object.key);
      }

      RemoveObject(bucket_name_, object_name);
    } catch (const std::runtime_error&) {
      RemoveObject(bucket_name_, object_name);
      throw;
    }
  }

  void PutObjectWithInflight() {
    std::cout << "PutObjectWithInflight()" << std::endl;
    auto remove_object_best_effort = [this](const std::string& object_name) {
      try {
        RemoveObject(bucket_name_, object_name);
      } catch (...) {
        // Keep original failure context from upload/download/verification.
      }
    };

    // Generate 100MB of random data.
    const size_t data_size = 100 * 1024 * 1024;
    std::string data;
    data.reserve(data_size);
    for (size_t i = 0; i < data_size; ++i) {
      data += charset[pick(rg)];
    }

    std::string orig_md5 = minio::utils::Md5sumHash(data);

    unsigned int inflight_values[] = {1, 2, 4};
    for (auto max_inflight : inflight_values) {
      std::string object_name = RandObjectName();
      std::string label = "max_inflight_parts=" + std::to_string(max_inflight);

      // Upload.
      {
        std::stringstream ss(data);
        minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data_size), 0);
        args.bucket = bucket_name_;
        args.object = object_name;
        args.max_inflight_parts = max_inflight;

        minio::s3::PutObjectResponse resp = client_.PutObject(args);
        if (!resp) {
          remove_object_best_effort(object_name);
          throw std::runtime_error("PutObject(" + label +
                                   "): " + resp.Error().String());
        }
      }

      // Download and verify MD5.
      try {
        std::string content;
        minio::s3::GetObjectArgs gargs;
        gargs.bucket = bucket_name_;
        gargs.object = object_name;
        gargs.datafunc =
            [&content](minio::http::DataFunctionArgs args) -> bool {
          content += args.datachunk;
          return true;
        };

        minio::s3::GetObjectResponse get_resp = client_.GetObject(gargs);
        if (!get_resp) {
          throw std::runtime_error("GetObject(" + label +
                                   "): " + get_resp.Error().String());
        }

        std::string got_md5 = minio::utils::Md5sumHash(content);
        if (orig_md5 != got_md5) {
          throw std::runtime_error(label + ": MD5 mismatch");
        }

        RemoveObject(bucket_name_, object_name);
      } catch (const std::runtime_error&) {
        remove_object_best_effort(object_name);
        throw;
      }
    }
  }

  void TestAsyncOperations() {
    std::cout << "TestAsyncOperations()" << std::endl;

    // --- async MakeBucket + BucketExists ---
    {
      std::string bucket_name = RandBucketName();

      {
        minio::s3::MakeBucketArgs args;
        args.bucket = bucket_name;
        auto make_fut = client_.MakeBucketAsync(args);
        minio::s3::MakeBucketResponse make_resp = make_fut.get();
        if (!make_resp) {
          throw std::runtime_error("MakeBucketAsync(): " +
                                   make_resp.Error().String());
        }
      }

      {
        minio::s3::BucketExistsArgs args;
        args.bucket = bucket_name;
        auto exists_fut = client_.BucketExistsAsync(args);
        minio::s3::BucketExistsResponse exists_resp = exists_fut.get();
        if (!exists_resp) {
          throw std::runtime_error("BucketExistsAsync(): " +
                                   exists_resp.Error().String());
        }
        if (!exists_resp.exist) {
          throw std::runtime_error(
              "BucketExistsAsync(): expected bucket to exist");
        }
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        auto rm_fut = client_.RemoveBucketAsync(args);
        minio::s3::RemoveBucketResponse rm_resp = rm_fut.get();
        if (!rm_resp) {
          throw std::runtime_error("RemoveBucketAsync(): " +
                                   rm_resp.Error().String());
        }
      }
    }

    // --- async PutObject + GetObject + StatObject + RemoveObject ---
    {
      std::string object_name = RandObjectName();
      std::string data = "TestAsyncOperations";
      {
        std::stringstream ss(data);
        minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()),
                                      0);
        args.bucket = bucket_name_;
        args.object = object_name;
        auto put_fut = client_.PutObjectAsync(std::move(args));
        minio::s3::PutObjectResponse put_resp = put_fut.get();
        if (!put_resp) {
          throw std::runtime_error("PutObjectAsync(): " +
                                   put_resp.Error().String());
        }
      }

      // async StatObject
      try {
        {
          minio::s3::StatObjectArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          auto stat_fut = client_.StatObjectAsync(std::move(args));
          minio::s3::StatObjectResponse stat_resp = stat_fut.get();
          if (!stat_resp) {
            throw std::runtime_error("StatObjectAsync(): " +
                                     stat_resp.Error().String());
          }
          if (stat_resp.size != data.length()) {
            throw std::runtime_error("StatObjectAsync(): expected size: " +
                                     std::to_string(data.length()) + "; got: " +
                                     std::to_string(stat_resp.size));
          }
        }

        // async GetObject
        {
          std::string content;
          minio::s3::GetObjectArgs gargs;
          gargs.bucket = bucket_name_;
          gargs.object = object_name;
          gargs.datafunc =
              [&content](minio::http::DataFunctionArgs args) -> bool {
            content += args.datachunk;
            return true;
          };
          auto get_fut = client_.GetObjectAsync(std::move(gargs));
          minio::s3::GetObjectResponse get_resp = get_fut.get();
          if (!get_resp) {
            throw std::runtime_error("GetObjectAsync(): " +
                                     get_resp.Error().String());
          }
          if (data != content) {
            throw std::runtime_error("GetObjectAsync(): expected: " + data +
                                     "; got: " + content);
          }
        }

        // async RemoveObject
        {
          minio::s3::RemoveObjectArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          auto rm_fut = client_.RemoveObjectAsync(std::move(args));
          minio::s3::RemoveObjectResponse rm_resp = rm_fut.get();
          if (!rm_resp) {
            throw std::runtime_error("RemoveObjectAsync(): " +
                                     rm_resp.Error().String());
          }
        }
      } catch (const std::runtime_error&) {
        RemoveObject(bucket_name_, object_name);
        throw;
      }
    }

    // --- async ListBuckets ---
    {
      auto fut = client_.ListBucketsAsync(minio::s3::ListBucketsArgs());
      minio::s3::ListBucketsResponse resp = fut.get();
      if (!resp) {
        throw std::runtime_error("ListBucketsAsync(): " +
                                 resp.Error().String());
      }
    }

    // --- async CopyObject ---
    {
      std::string src_object = RandObjectName();
      std::string dst_object = RandObjectName();
      std::string data = "CopyObjectAsync-test";
      try {
        {
          std::stringstream ss(data);
          minio::s3::PutObjectArgs args(
              ss, static_cast<uint64_t>(data.length()), 0);
          args.bucket = bucket_name_;
          args.object = src_object;
          minio::s3::PutObjectResponse resp = client_.PutObject(args);
          if (!resp) {
            throw std::runtime_error("PutObject(): " + resp.Error().String());
          }
        }

        minio::s3::CopySource source;
        source.bucket = bucket_name_;
        source.object = src_object;
        minio::s3::CopyObjectArgs cargs;
        cargs.bucket = bucket_name_;
        cargs.object = dst_object;
        cargs.source = source;
        auto copy_fut = client_.CopyObjectAsync(std::move(cargs));
        minio::s3::CopyObjectResponse copy_resp = copy_fut.get();
        if (!copy_resp) {
          throw std::runtime_error("CopyObjectAsync(): " +
                                   copy_resp.Error().String());
        }

        RemoveObject(bucket_name_, src_object);
        RemoveObject(bucket_name_, dst_object);
      } catch (const std::runtime_error&) {
        RemoveObject(bucket_name_, src_object);
        RemoveObject(bucket_name_, dst_object);
        throw;
      }
    }

    // --- async UploadObject ---
    {
      std::string data = "UploadObjectAsync-test";
      std::string filename = RandObjectName();
      {
        std::ofstream file(filename);
        file << data;
      }

      std::string object_name = RandObjectName();
      try {
        minio::s3::UploadObjectArgs args;
        args.bucket = bucket_name_;
        args.object = object_name;
        args.filename = filename;
        auto up_fut = client_.UploadObjectAsync(std::move(args));
        minio::s3::UploadObjectResponse up_resp = up_fut.get();
        if (!up_resp) {
          throw std::runtime_error("UploadObjectAsync(): " +
                                   up_resp.Error().String());
        }
        std::filesystem::remove(filename);
        RemoveObject(bucket_name_, object_name);
      } catch (const std::runtime_error&) {
        std::filesystem::remove(filename);
        RemoveObject(bucket_name_, object_name);
        throw;
      }
    }

    // --- async GetPresignedObjectUrl ---
    {
      std::string object_name = RandObjectName();
      std::string data = "PresignedAsync";
      std::stringstream ss(data);
      {
        minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()),
                                      0);
        args.bucket = bucket_name_;
        args.object = object_name;
        minio::s3::PutObjectResponse resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.Error().String());
        }
      }

      try {
        minio::s3::GetPresignedObjectUrlArgs args;
        args.bucket = bucket_name_;
        args.object = object_name;
        args.method = minio::http::Method::kGet;
        auto url_fut = client_.GetPresignedObjectUrlAsync(std::move(args));
        minio::s3::GetPresignedObjectUrlResponse url_resp = url_fut.get();
        if (!url_resp) {
          throw std::runtime_error("GetPresignedObjectUrlAsync(): " +
                                   url_resp.Error().String());
        }
        if (url_resp.url.empty()) {
          throw std::runtime_error(
              "GetPresignedObjectUrlAsync(): url is empty");
        }
        RemoveObject(bucket_name_, object_name);
      } catch (const std::runtime_error&) {
        RemoveObject(bucket_name_, object_name);
        throw;
      }
    }

    // --- async Bucket versioning (set + get) ---
    {
      std::string bucket_name = RandBucketName();
      {
        minio::s3::MakeBucketArgs args;
        args.bucket = bucket_name;
        auto fut = client_.MakeBucketAsync(args);
        minio::s3::MakeBucketResponse resp = fut.get();
        if (!resp) {
          throw std::runtime_error("MakeBucketAsync(): " +
                                   resp.Error().String());
        }
      }

      try {
        // Set versioning enabled.
        {
          minio::s3::SetBucketVersioningArgs args;
          args.bucket = bucket_name;
          minio::s3::Boolean status(true);
          args.status = status;
          auto fut = client_.SetBucketVersioningAsync(std::move(args));
          minio::s3::SetBucketVersioningResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("SetBucketVersioningAsync(): " +
                                     resp.Error().String());
          }
        }

        // Get and verify versioning status.
        {
          minio::s3::GetBucketVersioningArgs args;
          args.bucket = bucket_name;
          auto fut = client_.GetBucketVersioningAsync(std::move(args));
          minio::s3::GetBucketVersioningResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("GetBucketVersioningAsync(): " +
                                     resp.Error().String());
          }
          if (!resp.status || !resp.status.Get()) {
            throw std::runtime_error(
                "GetBucketVersioningAsync(): expected Enabled");
          }
        }

        {
          minio::s3::RemoveBucketArgs args;
          args.bucket = bucket_name;
          auto fut = client_.RemoveBucketAsync(args);
          minio::s3::RemoveBucketResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("RemoveBucketAsync(): " +
                                     resp.Error().String());
          }
        }
      } catch (const std::runtime_error&) {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        client_.RemoveBucket(args);
        throw;
      }
    }

    // --- async Object tags (set + get + delete) ---
    {
      std::string object_name = RandObjectName();
      std::string data = "TagsAsync";
      std::stringstream ss(data);
      {
        minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()),
                                      0);
        args.bucket = bucket_name_;
        args.object = object_name;
        minio::s3::PutObjectResponse resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.Error().String());
        }
      }

      try {
        // Set tags.
        {
          minio::s3::SetObjectTagsArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          args.tags = {{"key1", "value1"}, {"key2", "value2"}};
          auto fut = client_.SetObjectTagsAsync(std::move(args));
          minio::s3::SetObjectTagsResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("SetObjectTagsAsync(): " +
                                     resp.Error().String());
          }
        }

        // Get and verify tags.
        {
          minio::s3::GetObjectTagsArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          auto fut = client_.GetObjectTagsAsync(std::move(args));
          minio::s3::GetObjectTagsResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("GetObjectTagsAsync(): " +
                                     resp.Error().String());
          }
          if (resp.tags.size() != 2 || resp.tags["key1"] != "value1" ||
              resp.tags["key2"] != "value2") {
            throw std::runtime_error("GetObjectTagsAsync(): tag mismatch");
          }
        }

        // Delete tags.
        {
          minio::s3::DeleteObjectTagsArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          auto fut = client_.DeleteObjectTagsAsync(std::move(args));
          minio::s3::DeleteObjectTagsResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("DeleteObjectTagsAsync(): " +
                                     resp.Error().String());
          }
        }

        RemoveObject(bucket_name_, object_name);
      } catch (const std::runtime_error&) {
        RemoveObject(bucket_name_, object_name);
        throw;
      }
    }

    // --- async Bucket tags (set + get + delete) ---
    {
      std::string bucket_name = RandBucketName();
      {
        minio::s3::MakeBucketArgs args;
        args.bucket = bucket_name;
        auto fut = client_.MakeBucketAsync(args);
        minio::s3::MakeBucketResponse resp = fut.get();
        if (!resp) {
          throw std::runtime_error("MakeBucketAsync(): " +
                                   resp.Error().String());
        }
      }

      try {
        // Set bucket tags.
        {
          minio::s3::SetBucketTagsArgs args;
          args.bucket = bucket_name;
          args.tags = {{"department", "engineering"}};
          auto fut = client_.SetBucketTagsAsync(std::move(args));
          minio::s3::SetBucketTagsResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("SetBucketTagsAsync(): " +
                                     resp.Error().String());
          }
        }

        // Get and verify bucket tags.
        {
          minio::s3::GetBucketTagsArgs args;
          args.bucket = bucket_name;
          auto fut = client_.GetBucketTagsAsync(std::move(args));
          minio::s3::GetBucketTagsResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("GetBucketTagsAsync(): " +
                                     resp.Error().String());
          }
          if (resp.tags.size() != 1 ||
              resp.tags["department"] != "engineering") {
            throw std::runtime_error("GetBucketTagsAsync(): tag mismatch");
          }
        }

        // Delete bucket tags.
        {
          minio::s3::DeleteBucketTagsArgs args;
          args.bucket = bucket_name;
          auto fut = client_.DeleteBucketTagsAsync(std::move(args));
          minio::s3::DeleteBucketTagsResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("DeleteBucketTagsAsync(): " +
                                     resp.Error().String());
          }
        }

        {
          minio::s3::RemoveBucketArgs args;
          args.bucket = bucket_name;
          auto fut = client_.RemoveBucketAsync(args);
          minio::s3::RemoveBucketResponse resp = fut.get();
          if (!resp) {
            throw std::runtime_error("RemoveBucketAsync(): " +
                                     resp.Error().String());
          }
        }
      } catch (const std::runtime_error&) {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        client_.RemoveBucket(args);
        throw;
      }
    }

    // --- async ListBuckets (no-arg overload) ---
    {
      auto fut = client_.ListBucketsAsync();
      minio::s3::ListBucketsResponse resp = fut.get();
      if (!resp) {
        throw std::runtime_error("ListBucketsAsync() (no-arg): " +
                                 resp.Error().String());
      }
    }

    // --- Error path: async call on non-existent object ---
    {
      minio::s3::StatObjectArgs args;
      args.bucket = bucket_name_;
      args.object = "__nonexistent_object_async_test__";
      auto fut = client_.StatObjectAsync(std::move(args));
      minio::s3::StatObjectResponse resp = fut.get();
      if (resp) {
        throw std::runtime_error(
            "StatObjectAsync() on nonexistent object: expected failure");
      }
      if (resp.Error().String().empty()) {
        throw std::runtime_error(
            "StatObjectAsync() on nonexistent object: expected error message");
      }
    }

    // --- Concurrency: start multiple async ops before any get() ---
    {
      std::string b1 = RandBucketName();
      std::string b2 = RandBucketName();
      std::string b3 = RandBucketName();

      minio::s3::MakeBucketArgs a1, a2, a3;
      a1.bucket = b1;
      a2.bucket = b2;
      a3.bucket = b3;

      auto f1 = client_.MakeBucketAsync(std::move(a1));
      auto f2 = client_.MakeBucketAsync(std::move(a2));
      auto f3 = client_.MakeBucketAsync(std::move(a3));

      auto cleanup = [this](const std::string& b1, const std::string& b2,
                            const std::string& b3) {
        for (auto& b : {b1, b2, b3}) {
          try {
            minio::s3::RemoveBucketArgs args;
            args.bucket = b;
            client_.RemoveBucket(args);
          } catch (...) {
          }
        }
      };

      try {
        auto r1 = f1.get();
        auto r2 = f2.get();
        auto r3 = f3.get();

        if (!r1 || !r2 || !r3) {
          cleanup(b1, b2, b3);
          throw std::runtime_error("concurrent MakeBucketAsync(): one failed");
        }

        minio::s3::BucketExistsArgs be1, be2, be3;
        be1.bucket = b1;
        be2.bucket = b2;
        be3.bucket = b3;
        auto fe1 = client_.BucketExistsAsync(std::move(be1));
        auto fe2 = client_.BucketExistsAsync(std::move(be2));
        auto fe3 = client_.BucketExistsAsync(std::move(be3));
        if (!fe1.get().exist || !fe2.get().exist || !fe3.get().exist) {
          cleanup(b1, b2, b3);
          throw std::runtime_error(
              "concurrent BucketExistsAsync(): expected true");
        }

        cleanup(b1, b2, b3);
      } catch (...) {
        cleanup(b1, b2, b3);
        throw;
      }
    }

    // --- Reference-holding arg: SetBucketEncryptionAsync ---
    {
      std::string bucket_name = RandBucketName();
      {
        minio::s3::MakeBucketArgs args;
        args.bucket = bucket_name;
        auto fut = client_.MakeBucketAsync(args);
        if (!fut.get()) {
          throw std::runtime_error("MakeBucketAsync(): failed");
        }
      }

      std::future<minio::s3::SetBucketEncryptionResponse> enc_fut;
      {
        // SseConfig lives only in this scope; async must own a copy.
        minio::s3::SseConfig sse_config = minio::s3::SseConfig::S3();
        minio::s3::SetBucketEncryptionArgs enc_args(sse_config);
        enc_args.bucket = bucket_name;
        enc_fut = client_.SetBucketEncryptionAsync(std::move(enc_args));
        // sse_config and enc_args go out of scope here.
      }

      minio::s3::SetBucketEncryptionResponse enc_resp = enc_fut.get();
      if (!enc_resp) {
        std::cout << "  SetBucketEncryptionAsync skipped: "
                  << enc_resp.Error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        client_.RemoveBucket(args);
      }
    }

    // --- Reference-holding arg: SetBucketLifecycleAsync ---
    {
      std::string bucket_name = RandBucketName();
      {
        minio::s3::MakeBucketArgs args;
        args.bucket = bucket_name;
        auto fut = client_.MakeBucketAsync(args);
        if (!fut.get()) {
          throw std::runtime_error("MakeBucketAsync(): failed");
        }
      }

      std::future<minio::s3::SetBucketLifecycleResponse> lc_fut;
      {
        minio::s3::LifecycleConfig lc_config;
        minio::s3::SetBucketLifecycleArgs lc_args(lc_config);
        lc_args.bucket = bucket_name;
        lc_fut = client_.SetBucketLifecycleAsync(std::move(lc_args));
        // lc_config and lc_args go out of scope here.
      }

      minio::s3::SetBucketLifecycleResponse lc_resp = lc_fut.get();
      if (!lc_resp) {
        std::cout << "  SetBucketLifecycleAsync skipped: "
                  << lc_resp.Error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        client_.RemoveBucket(args);
      }
    }

    // --- Reference-holding arg: SetBucketNotificationAsync ---
    {
      std::string bucket_name = RandBucketName();
      {
        minio::s3::MakeBucketArgs args;
        args.bucket = bucket_name;
        auto fut = client_.MakeBucketAsync(args);
        if (!fut.get()) {
          throw std::runtime_error("MakeBucketAsync(): failed");
        }
      }

      std::future<minio::s3::SetBucketNotificationResponse> notif_fut;
      {
        minio::s3::NotificationConfig notif_config;
        minio::s3::SetBucketNotificationArgs notif_args(notif_config);
        notif_args.bucket = bucket_name;
        notif_fut = client_.SetBucketNotificationAsync(std::move(notif_args));
        // notif_config and notif_args go out of scope here.
      }

      minio::s3::SetBucketNotificationResponse notif_resp = notif_fut.get();
      if (!notif_resp) {
        std::cout << "  SetBucketNotificationAsync skipped: "
                  << notif_resp.Error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        client_.RemoveBucket(args);
      }
    }

    // --- Reference-holding arg: SetBucketReplicationAsync ---
    {
      std::string bucket_name = RandBucketName();
      {
        minio::s3::MakeBucketArgs args;
        args.bucket = bucket_name;
        auto fut = client_.MakeBucketAsync(args);
        if (!fut.get()) {
          throw std::runtime_error("MakeBucketAsync(): failed");
        }
      }

      std::future<minio::s3::SetBucketReplicationResponse> repl_fut;
      {
        minio::s3::ReplicationConfig repl_config;
        minio::s3::SetBucketReplicationArgs repl_args(repl_config);
        repl_args.bucket = bucket_name;
        repl_fut = client_.SetBucketReplicationAsync(std::move(repl_args));
        // repl_config and repl_args go out of scope here.
      }

      minio::s3::SetBucketReplicationResponse repl_resp = repl_fut.get();
      if (!repl_resp) {
        std::cout << "  SetBucketReplicationAsync skipped: "
                  << repl_resp.Error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        client_.RemoveBucket(args);
      }
    }

    // --- Reference-holding arg: SelectObjectContentAsync ---
    {
      std::string object_name = RandObjectName();
      std::string data =
          "1997,Ford,E350,\"ac, abs, moon\",3000.00\n"
          "1999,Chevy,\"Venture \"\"Extended Edition\"\"\",,4900.00\n";
      std::stringstream ss("Year,Make,Model,Description,Price\n" + data);
      {
        minio::s3::PutObjectArgs args(
            ss, static_cast<uint64_t>(ss.str().length()), 0);
        args.bucket = bucket_name_;
        args.object = object_name;
        auto fut = client_.PutObjectAsync(std::move(args));
        if (!fut.get()) {
          throw std::runtime_error("PutObject(): failed");
        }
      }

      std::future<minio::s3::SelectObjectContentResponse> sel_fut;
      std::string records;
      {
        std::string expression = "select * from S3Object";
        minio::s3::CsvInputSerialization csv_input;
        minio::s3::FileHeaderInfo file_header_info =
            minio::s3::FileHeaderInfo::kUse;
        csv_input.file_header_info =
            std::make_shared<minio::s3::FileHeaderInfo>(file_header_info);
        minio::s3::CsvOutputSerialization csv_output;
        minio::s3::QuoteFields quote_fields = minio::s3::QuoteFields::kAsNeeded;
        csv_output.quote_fields =
            std::make_shared<minio::s3::QuoteFields>(quote_fields);

        minio::s3::SelectRequest request(expression, &csv_input, &csv_output);
        auto func = [&records](minio::s3::SelectResult result) -> bool {
          if (result.err) return false;
          records += result.records;
          return true;
        };

        minio::s3::SelectObjectContentArgs sel_args(request, func);
        sel_args.bucket = bucket_name_;
        sel_args.object = object_name;

        sel_fut = client_.SelectObjectContentAsync(std::move(sel_args));
        // csv_input, csv_output, request, etc. go out of scope here.
      }

      minio::s3::SelectObjectContentResponse sel_resp = sel_fut.get();
      if (!sel_resp && sel_resp.code == "MethodNotAllowed") {
        std::cout << "  SelectObjectContentAsync skipped: server does not "
                     "implement S3 Select"
                  << std::endl;
      } else if (!sel_resp) {
        throw std::runtime_error("SelectObjectContentAsync(): " +
                                 sel_resp.Error().String());
      }
      RemoveObject(bucket_name_, object_name);
    }
  }  // TestAsyncOperations
};  // class Tests

int main(int /*argc*/, char* /*argv*/[]) {
  std::string host;
  if (!minio::utils::GetEnv(host, "SERVER_ENDPOINT")) {
    std::cerr << "SERVER_ENDPOINT environment variable must be set"
              << std::endl;
    return EXIT_FAILURE;
  }

  std::string access_key;
  if (!minio::utils::GetEnv(access_key, "ACCESS_KEY")) {
    std::cerr << "ACCESS_KEY environment variable must be set" << std::endl;
    return EXIT_FAILURE;
  }

  std::string secret_key;
  if (!minio::utils::GetEnv(secret_key, "SECRET_KEY")) {
    std::cerr << "SECRET_KEY environment variable must be set" << std::endl;
    return EXIT_FAILURE;
  }

  std::string value;
  bool secure = false;
  if (minio::utils::GetEnv(value, "ENABLE_HTTPS")) secure = true;

  bool ignore_cert_check = false;
  if (minio::utils::GetEnv(value, "IGNORE_CERT_CHECK")) {
    ignore_cert_check = true;
  }

  std::string region;
  minio::utils::GetEnv(region, "SERVER_REGION");

  minio::s3::BaseUrl base_url(host, secure);

  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);
  if (secure) client.IgnoreCertCheck(ignore_cert_check);

  Tests tests(client);
  tests.MakeBucket();
  tests.RemoveBucket();
  tests.BucketExists();
  tests.ListBuckets();
  tests.StatObject();
  tests.RemoveObject();
  tests.DownloadObject();
  tests.GetObject();
  tests.ListObjects();
  tests.ListObjects1010();
  tests.PutObject();
  tests.PutObjectWithInflight();
  tests.CopyObject();
  tests.UploadObject();
  tests.RemoveObjects();
  tests.SelectObjectContent();
  tests.ListenBucketNotification();
  tests.TestAsyncOperations();

  return EXIT_SUCCESS;
}
