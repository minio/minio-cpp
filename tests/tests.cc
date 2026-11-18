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

#include <httplib.h>
#include <miniocpp/args.h>
#include <miniocpp/client.h>
#include <miniocpp/http.h>
#include <miniocpp/providers.h>
#include <miniocpp/request.h>
#include <miniocpp/response.h>
#include <miniocpp/result.h>
#include <miniocpp/select.h>
#include <miniocpp/types.h>

using minio::Result;
#include <miniocpp/utils.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

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
#include <map>
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

std::string PutUint32BigEndian(unsigned int v) {
  std::string s(4, '\0');
  s[0] = static_cast<char>((v >> 24) & 0xFF);
  s[1] = static_cast<char>((v >> 16) & 0xFF);
  s[2] = static_cast<char>((v >> 8) & 0xFF);
  s[3] = static_cast<char>(v & 0xFF);
  return s;
}

// Build a single S3 Select protocol frame (prelude + prelude CRC + headers
// + payload + message CRC) for the given event headers and XML payload. The
// headers section is exactly the encoded headers: the wire format (AWS S3 and
// MinIO) has no terminator byte, the prelude's headers-length is the boundary.
std::string MakeSelectFrame(const std::map<std::string, std::string>& headers,
                            const std::string& payload) {
  std::string headerdata;
  for (const auto& [name, value] : headers) {
    headerdata += static_cast<char>(name.length());
    headerdata += name;
    headerdata += static_cast<char>(7);  // header value type: string
    headerdata += static_cast<char>((value.length() >> 8) & 0xFF);
    headerdata += static_cast<char>(value.length() & 0xFF);
    headerdata += value;
  }

  std::string data = headerdata + payload;
  unsigned int total_length = 16 + static_cast<unsigned int>(data.length());
  std::string prelude =
      PutUint32BigEndian(total_length) +
      PutUint32BigEndian(static_cast<unsigned int>(headerdata.length()));
  std::string prelude_crc = PutUint32BigEndian(
      static_cast<unsigned int>(minio::utils::CRC32(prelude)));
  std::string message = prelude + prelude_crc + data;
  std::string message_crc = PutUint32BigEndian(
      static_cast<unsigned int>(minio::utils::CRC32(message)));
  return message + message_crc;
}

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
    auto resp = client_.MakeBucket(args);
    if (!resp) {
      throw std::runtime_error("MakeBucket(): " + resp.error().String());
    }
  }

  ~Tests() noexcept(false) {
    minio::s3::RemoveBucketArgs args;
    args.bucket = bucket_name_;
    auto resp = client_.RemoveBucket(args);
    if (!resp) {
      throw std::runtime_error("RemoveBucket(): " + resp.error().String());
    }
  }

  void MakeBucket(std::string bucket_name) noexcept(false) {
    minio::s3::MakeBucketArgs args;
    args.bucket = bucket_name;
    auto resp = client_.MakeBucket(args);
    if (resp) return;
    throw MakeBucketError("MakeBucket(): " + resp.error().String());
  }

  void RemoveBucket(std::string bucket_name) noexcept(false) {
    minio::s3::RemoveBucketArgs args;
    args.bucket = bucket_name;
    auto resp = client_.RemoveBucket(args);
    if (resp) return;
    throw RemoveBucketError("RemoveBucket(): " + resp.error().String());
  }

  void RemoveObject(std::string bucket_name, std::string object_name) {
    minio::s3::RemoveObjectArgs args;
    args.bucket = bucket_name;
    args.object = object_name;
    auto resp = client_.RemoveObject(args);
    if (!resp) {
      throw std::runtime_error("RemoveObject(): " + resp.error().String());
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

    auto result = client_.RemoveObjects(args);
    std::string msg;
    for (; result; result++) {
      auto err = *result;
      if (!err.code.empty() || !err.message.empty()) {
        throw std::runtime_error("RemoveObjects(): " + err.code + ": " +
                                 err.message);
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
      auto resp = client_.BucketExists(args);
      if (!resp) {
        throw BucketExistsError("BucketExists(): " + resp.error().String());
      }
      if (!resp->exist) {
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

      auto resp = client_.ListBuckets();
      if (!resp) {
        throw std::runtime_error("ListBuckets(): " + resp.error().String());
      }

      std::size_t c = 0;
      for (auto& bucket : resp->buckets) {
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
    auto resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.error().String());
    }
    try {
      minio::s3::StatObjectArgs args;
      args.bucket = bucket_name_;
      args.object = object_name;
      auto resp = client_.StatObject(args);
      if (!resp) {
        throw std::runtime_error("StatObject(): " + resp.error().String());
      }
      if (resp->size != data.length()) {
        throw std::runtime_error(
            "StatObject(): expected: " + std::to_string(data.length()) +
            "; got: " + std::to_string(resp->size));
      }
      if (resp->delete_marker) {
        throw std::runtime_error("StatObject(): expected: false; got: true");
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
    auto resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.error().String());
    }
    RemoveObject(bucket_name_, object_name);
  }

  void DownloadObject() {
    std::cout << "DownloadObject()" << std::endl;

    std::string object_name = RandObjectName();

    // Binary-safe round-trip: a newline, the Windows text-mode EOF byte
    // (0x1A / Ctrl-Z), and a NUL byte must survive download byte-for-byte.
    std::string data = "DownloadObject()\n";
    data += static_cast<char>(0x1A);
    data += '\0';
    data += "binary-tail";
    std::stringstream ss(data);
    minio::s3::PutObjectArgs args(ss, static_cast<uint64_t>(data.length()), 0);
    args.bucket = bucket_name_;
    args.object = object_name;
    auto resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.error().String());
    }

    try {
      std::string filename = RandObjectName();
      minio::s3::DownloadObjectArgs args;
      args.bucket = bucket_name_;
      args.object = object_name;
      args.filename = filename;
      auto resp = client_.DownloadObject(args);
      if (!resp) {
        throw std::runtime_error("DownloadObject(): " + resp.error().String());
      }

      std::ifstream file(filename, std::ios::binary);
      file.seekg(0, std::ios::end);
      size_t length = file.tellg();
      file.seekg(0, std::ios::beg);
      char* buf = new char[length];
      file.read(buf, length);
      file.close();

      if (data != std::string(buf, length)) {
        throw std::runtime_error(
            "DownloadObject(): expected " + std::to_string(data.length()) +
            " bytes; got " + std::to_string(length) + " bytes");
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
    auto resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.error().String());
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
      auto resp = client_.GetObject(args);
      if (!resp) {
        throw std::runtime_error("GetObject(): " + resp.error().String());
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
        auto resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.error().String());
        }
        object_names.push_back(object_name);
      }

      std::size_t c = 0;
      minio::s3::ListObjectsArgs args;
      args.bucket = bucket_name_;
      auto result = client_.ListObjects(args);
      for (; result; result++) {
        auto item = *result;
        if (item.name.empty()) {
          throw std::runtime_error("ListObjects(): empty item");
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
      auto resp = client_.PutObject(args);
      if (!resp) {
        throw std::runtime_error("PutObject(): " + resp.error().String());
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
      auto resp = client_.PutObject(args);
      if (!resp) {
        throw std::runtime_error("<Multipart> PutObject(): " +
                                 resp.error().String());
      }
      if (resp->etag == "") {
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
    auto resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.error().String());
    }

    try {
      minio::s3::CopySource source;
      source.bucket = bucket_name_;
      source.object = src_object_name;
      minio::s3::CopyObjectArgs args;
      args.bucket = bucket_name_;
      args.object = object_name;
      args.source = source;
      auto resp = client_.CopyObject(args);
      if (!resp) {
        throw std::runtime_error("CopyObject(): " + resp.error().String());
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
    auto resp = client_.UploadObject(args);
    if (!resp) {
      throw std::runtime_error("UploadObject(): " + resp.error().String());
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
        auto resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.error().String());
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
    auto resp = client_.PutObject(args);
    if (!resp) {
      throw std::runtime_error("PutObject(): " + resp.error().String());
    }

    std::string expression = "select * from S3Object";
    minio::s3::CsvInputSerialization csv_input;
    minio::s3::FileHeaderInfo file_header_info =
        minio::s3::FileHeaderInfo::kUse;
    csv_input.file_header_info =
        std::make_shared<minio::s3::FileHeaderInfo>(file_header_info);
    minio::s3::CsvOutputSerialization csv_output;
    auto quote_fields = minio::s3::QuoteFields::kAsNeeded;
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
      auto resp = client_.SelectObjectContent(args);
      if (!resp) {
        if (resp.error().String().find("MethodNotAllowed") !=
            std::string::npos) {
          std::cout << "  skipped: server does not implement S3 Select"
                    << std::endl;
          RemoveObject(bucket_name_, object_name);
          return;
        }
        throw std::runtime_error("SelectObjectContent(): " +
                                 resp.error().String());
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
      auto resp = client_.ListenBucketNotification(args);
      if (!resp) {
        throw std::runtime_error("ListenBucketNotification(): " +
                                 resp.error().String());
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
      auto resp = client_.PutObject(args);
      if (!resp) {
        throw std::runtime_error("PutObject(): " + resp.error().String());
      }

      task.join();

      if (records.empty()) {
        throw std::runtime_error(
            "ListenBucketNotification(): records length: expected: 1, got: 0");
      }

      auto record = records.front();
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

        auto resp = client_.PutObject(args);
        if (!resp) {
          remove_object_best_effort(object_name);
          throw std::runtime_error("PutObject(" + label +
                                   "): " + resp.error().String());
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

        auto get_resp = client_.GetObject(gargs);
        if (!get_resp) {
          throw std::runtime_error("GetObject(" + label +
                                   "): " + get_resp.error().String());
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

      auto remove_bucket_best_effort = [this](const std::string& b) {
        try {
          minio::s3::RemoveBucketArgs args;
          args.bucket = b;
          (void)client_.RemoveBucket(args);
        } catch (...) {
        }
      };

      try {
        {
          minio::s3::MakeBucketArgs args;
          args.bucket = bucket_name;
          auto make_fut = client_.MakeBucketAsync(args);
          auto make_resp = make_fut.get();
          if (!make_resp) {
            throw std::runtime_error("MakeBucketAsync(): " +
                                     make_resp.error().String());
          }
        }

        {
          minio::s3::BucketExistsArgs args;
          args.bucket = bucket_name;
          auto exists_fut = client_.BucketExistsAsync(args);
          auto exists_resp = exists_fut.get();
          if (!exists_resp) {
            throw std::runtime_error("BucketExistsAsync(): " +
                                     exists_resp.error().String());
          }
          if (!exists_resp->exist) {
            throw std::runtime_error(
                "BucketExistsAsync(): expected bucket to exist");
          }
        }

        {
          minio::s3::RemoveBucketArgs args;
          args.bucket = bucket_name;
          auto rm_fut = client_.RemoveBucketAsync(args);
          auto rm_resp = rm_fut.get();
          if (!rm_resp) {
            throw std::runtime_error("RemoveBucketAsync(): " +
                                     rm_resp.error().String());
          }
        }
      } catch (...) {
        remove_bucket_best_effort(bucket_name);
        throw;
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
        auto put_resp = put_fut.get();
        if (!put_resp) {
          throw std::runtime_error("PutObjectAsync(): " +
                                   put_resp.error().String());
        }
      }

      // async StatObject
      try {
        {
          minio::s3::StatObjectArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          auto stat_fut = client_.StatObjectAsync(std::move(args));
          auto stat_resp = stat_fut.get();
          if (!stat_resp) {
            throw std::runtime_error("StatObjectAsync(): " +
                                     stat_resp.error().String());
          }
          if (stat_resp->size != data.length()) {
            throw std::runtime_error("StatObjectAsync(): expected size: " +
                                     std::to_string(data.length()) + "; got: " +
                                     std::to_string(stat_resp->size));
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
          auto get_resp = get_fut.get();
          if (!get_resp) {
            throw std::runtime_error("GetObjectAsync(): " +
                                     get_resp.error().String());
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
          auto rm_resp = rm_fut.get();
          if (!rm_resp) {
            throw std::runtime_error("RemoveObjectAsync(): " +
                                     rm_resp.error().String());
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
      auto resp = fut.get();
      if (!resp) {
        throw std::runtime_error("ListBucketsAsync(): " +
                                 resp.error().String());
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
          auto resp = client_.PutObject(args);
          if (!resp) {
            throw std::runtime_error("PutObject(): " + resp.error().String());
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
        auto copy_resp = copy_fut.get();
        if (!copy_resp) {
          throw std::runtime_error("CopyObjectAsync(): " +
                                   copy_resp.error().String());
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
        auto up_resp = up_fut.get();
        if (!up_resp) {
          throw std::runtime_error("UploadObjectAsync(): " +
                                   up_resp.error().String());
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
        auto resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.error().String());
        }
      }

      try {
        minio::s3::GetPresignedObjectUrlArgs args;
        args.bucket = bucket_name_;
        args.object = object_name;
        args.method = minio::http::Method::kGet;
        auto url_fut = client_.GetPresignedObjectUrlAsync(std::move(args));
        auto url_resp = url_fut.get();
        if (!url_resp) {
          throw std::runtime_error("GetPresignedObjectUrlAsync(): " +
                                   url_resp.error().String());
        }
        if (url_resp->url.empty()) {
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
        auto resp = fut.get();
        if (!resp) {
          throw std::runtime_error("MakeBucketAsync(): " +
                                   resp.error().String());
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
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("SetBucketVersioningAsync(): " +
                                     resp.error().String());
          }
        }

        // Get and verify versioning status.
        {
          minio::s3::GetBucketVersioningArgs args;
          args.bucket = bucket_name;
          auto fut = client_.GetBucketVersioningAsync(std::move(args));
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("GetBucketVersioningAsync(): " +
                                     resp.error().String());
          }
          if (!resp->status || !resp->status.Get()) {
            throw std::runtime_error(
                "GetBucketVersioningAsync(): expected Enabled");
          }
        }

        {
          minio::s3::RemoveBucketArgs args;
          args.bucket = bucket_name;
          auto fut = client_.RemoveBucketAsync(args);
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("RemoveBucketAsync(): " +
                                     resp.error().String());
          }
        }
      } catch (const std::runtime_error&) {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        (void)client_.RemoveBucket(args);
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
        auto resp = client_.PutObject(args);
        if (!resp) {
          throw std::runtime_error("PutObject(): " + resp.error().String());
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
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("SetObjectTagsAsync(): " +
                                     resp.error().String());
          }
        }

        // Get and verify tags.
        {
          minio::s3::GetObjectTagsArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          auto fut = client_.GetObjectTagsAsync(std::move(args));
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("GetObjectTagsAsync(): " +
                                     resp.error().String());
          }
          if (resp->tags.size() != 2 || resp->tags["key1"] != "value1" ||
              resp->tags["key2"] != "value2") {
            throw std::runtime_error("GetObjectTagsAsync(): tag mismatch");
          }
        }

        // Delete tags.
        {
          minio::s3::DeleteObjectTagsArgs args;
          args.bucket = bucket_name_;
          args.object = object_name;
          auto fut = client_.DeleteObjectTagsAsync(std::move(args));
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("DeleteObjectTagsAsync(): " +
                                     resp.error().String());
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
        auto resp = fut.get();
        if (!resp) {
          throw std::runtime_error("MakeBucketAsync(): " +
                                   resp.error().String());
        }
      }

      try {
        // Set bucket tags.
        {
          minio::s3::SetBucketTagsArgs args;
          args.bucket = bucket_name;
          args.tags = {{"department", "engineering"}};
          auto fut = client_.SetBucketTagsAsync(std::move(args));
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("SetBucketTagsAsync(): " +
                                     resp.error().String());
          }
        }

        // Get and verify bucket tags.
        {
          minio::s3::GetBucketTagsArgs args;
          args.bucket = bucket_name;
          auto fut = client_.GetBucketTagsAsync(std::move(args));
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("GetBucketTagsAsync(): " +
                                     resp.error().String());
          }
          if (resp->tags.size() != 1 ||
              resp->tags["department"] != "engineering") {
            throw std::runtime_error("GetBucketTagsAsync(): tag mismatch");
          }
        }

        // Delete bucket tags.
        {
          minio::s3::DeleteBucketTagsArgs args;
          args.bucket = bucket_name;
          auto fut = client_.DeleteBucketTagsAsync(std::move(args));
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("DeleteBucketTagsAsync(): " +
                                     resp.error().String());
          }
        }

        {
          minio::s3::RemoveBucketArgs args;
          args.bucket = bucket_name;
          auto fut = client_.RemoveBucketAsync(args);
          auto resp = fut.get();
          if (!resp) {
            throw std::runtime_error("RemoveBucketAsync(): " +
                                     resp.error().String());
          }
        }
      } catch (const std::runtime_error&) {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        (void)client_.RemoveBucket(args);
        throw;
      }
    }

    // --- async ListBuckets (no-arg overload) ---
    {
      auto fut = client_.ListBucketsAsync();
      auto resp = fut.get();
      if (!resp) {
        throw std::runtime_error("ListBucketsAsync() (no-arg): " +
                                 resp.error().String());
      }
    }

    // --- Error path: async call on non-existent object ---
    {
      minio::s3::StatObjectArgs args;
      args.bucket = bucket_name_;
      args.object = "__nonexistent_object_async_test__";
      auto fut = client_.StatObjectAsync(std::move(args));
      auto resp = fut.get();
      if (resp) {
        throw std::runtime_error(
            "StatObjectAsync() on nonexistent object: expected failure");
      }
      if (resp.error().String().empty()) {
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
            (void)client_.RemoveBucket(args);
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
        auto fe1_resp = fe1.get();
        auto fe2_resp = fe2.get();
        auto fe3_resp = fe3.get();
        if (!fe1_resp || !fe2_resp || !fe3_resp) {
          cleanup(b1, b2, b3);
          throw std::runtime_error(
              "concurrent BucketExistsAsync(): one failed");
        }
        if (!fe1_resp->exist || !fe2_resp->exist || !fe3_resp->exist) {
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

      std::future<Result<minio::s3::SetBucketEncryptionResponse>> enc_fut;
      {
        // SseConfig lives only in this scope; async must own a copy.
        auto sse_config = minio::s3::SseConfig::S3();
        minio::s3::SetBucketEncryptionArgs enc_args(sse_config);
        enc_args.bucket = bucket_name;
        enc_fut = client_.SetBucketEncryptionAsync(std::move(enc_args));
        // sse_config and enc_args go out of scope here.
      }

      auto enc_resp = enc_fut.get();
      if (!enc_resp) {
        std::cout << "  SetBucketEncryptionAsync skipped: "
                  << enc_resp.error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        (void)client_.RemoveBucket(args);
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

      std::future<Result<minio::s3::SetBucketLifecycleResponse>> lc_fut;
      {
        minio::s3::LifecycleConfig lc_config;
        minio::s3::SetBucketLifecycleArgs lc_args(lc_config);
        lc_args.bucket = bucket_name;
        lc_fut = client_.SetBucketLifecycleAsync(std::move(lc_args));
        // lc_config and lc_args go out of scope here.
      }

      auto lc_resp = lc_fut.get();
      if (!lc_resp) {
        std::cout << "  SetBucketLifecycleAsync skipped: "
                  << lc_resp.error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        (void)client_.RemoveBucket(args);
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

      std::future<Result<minio::s3::SetBucketNotificationResponse>> notif_fut;
      {
        minio::s3::NotificationConfig notif_config;
        minio::s3::SetBucketNotificationArgs notif_args(notif_config);
        notif_args.bucket = bucket_name;
        notif_fut = client_.SetBucketNotificationAsync(std::move(notif_args));
        // notif_config and notif_args go out of scope here.
      }

      auto notif_resp = notif_fut.get();
      if (!notif_resp) {
        std::cout << "  SetBucketNotificationAsync skipped: "
                  << notif_resp.error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        (void)client_.RemoveBucket(args);
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

      std::future<Result<minio::s3::SetBucketReplicationResponse>> repl_fut;
      {
        minio::s3::ReplicationConfig repl_config;
        minio::s3::SetBucketReplicationArgs repl_args(repl_config);
        repl_args.bucket = bucket_name;
        repl_fut = client_.SetBucketReplicationAsync(std::move(repl_args));
        // repl_config and repl_args go out of scope here.
      }

      auto repl_resp = repl_fut.get();
      if (!repl_resp) {
        std::cout << "  SetBucketReplicationAsync skipped: "
                  << repl_resp.error().String() << std::endl;
      }

      {
        minio::s3::RemoveBucketArgs args;
        args.bucket = bucket_name;
        (void)client_.RemoveBucket(args);
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

      std::future<Result<minio::s3::SelectObjectContentResponse>> sel_fut;
      std::string records;
      {
        std::string expression = "select * from S3Object";
        minio::s3::CsvInputSerialization csv_input;
        minio::s3::FileHeaderInfo file_header_info =
            minio::s3::FileHeaderInfo::kUse;
        csv_input.file_header_info =
            std::make_shared<minio::s3::FileHeaderInfo>(file_header_info);
        minio::s3::CsvOutputSerialization csv_output;
        auto quote_fields = minio::s3::QuoteFields::kAsNeeded;
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

      auto sel_resp = sel_fut.get();
      if (!sel_resp && sel_resp.error().String().find("MethodNotAllowed") !=
                           std::string::npos) {
        std::cout << "  SelectObjectContentAsync skipped: server does not "
                     "implement S3 Select"
                  << std::endl;
      } else if (!sel_resp) {
        throw std::runtime_error("SelectObjectContentAsync(): " +
                                 sel_resp.error().String());
      }
      RemoveObject(bucket_name_, object_name);
    }
  }  // TestAsyncOperations

  // Regression test for SelectHandler Stats metric parsing: metrics larger
  // than INT32_MAX must round-trip as exact long long values (any fallback
  // to std::stol on 32-bit Windows LLP64 would truncate them). Uses a
  // synthetic Stats event frame, independent of large objects.
  void SelectStatsMetrics() {
    std::cout << "SelectStatsMetrics()" << std::endl;

    const long long scanned = 5000000000LL;
    const long long processed = 6000000000LL;
    const long long returned = 7000000000LL;

    std::map<std::string, std::string> headers = {
        {":message-type", "event"},
        {":event-type", "Stats"},
    };
    std::string payload = "<Stats><BytesScanned>" + std::to_string(scanned) +
                          "</BytesScanned><BytesProcessed>" +
                          std::to_string(processed) +
                          "</BytesProcessed><BytesReturned>" +
                          std::to_string(returned) + "</BytesReturned></Stats>";

    bool stats_delivered = false;
    minio::s3::SelectHandler handler(
        [&](minio::s3::SelectResult result) -> bool {
          if (result.err) {
            throw std::runtime_error("SelectStatsMetrics(): " +
                                     result.err.String());
          }
          if (!result.bytes_scanned.has_value() ||
              *result.bytes_scanned != scanned ||
              !result.bytes_processed.has_value() ||
              *result.bytes_processed != processed ||
              !result.bytes_returned.has_value() ||
              *result.bytes_returned != returned) {
            throw std::runtime_error(
                "SelectStatsMetrics(): unexpected metrics");
          }
          stats_delivered = true;
          return true;
        });

    minio::http::DataFunctionArgs args;
    args.datachunk = MakeSelectFrame(headers, payload);
    if (!handler.DataFunction(args)) {
      throw std::runtime_error("SelectStatsMetrics(): DataFunction failed");
    }
    if (!stats_delivered) {
      throw std::runtime_error(
          "SelectStatsMetrics(): Stats result was not delivered");
    }
  }

  // Issue #205 regression: a failed AssumeRoleProvider::Fetch() leaves
  // access_key/secret_key/session_token empty. The failure must be surfaced
  // via creds.err instead of silently printing empty fields; on success all
  // three temporary credentials must be non-empty.
  void AssumeRoleProvider() {
    std::cout << "AssumeRoleProvider()" << std::endl;

    const minio::s3::BaseUrl& base_url = client_.GetBaseUrl();
    minio::http::Url sts_endpoint(base_url.https, base_url.host, base_url.port);

    std::string access_key;
    std::string secret_key;
    minio::utils::GetEnv(access_key, "ACCESS_KEY");
    minio::utils::GetEnv(secret_key, "SECRET_KEY");

    std::string region =
        base_url.region.empty() ? "us-east-1" : base_url.region;
    minio::creds::AssumeRoleProvider provider(sts_endpoint, access_key,
                                              secret_key, 900, "", region);
    minio::creds::Credentials creds = provider.Fetch();

    if (!creds.err.String().empty()) {
      // Failure path (issue #205): empty credential fields must be
      // accompanied by a diagnosable error.
      throw std::runtime_error(
          "AssumeRoleProvider(): Fetch() failed with an error" +
          creds.err.String());
      return;
    }

    if (creds.access_key.empty() || creds.secret_key.empty() ||
        creds.session_token.empty()) {
      throw std::runtime_error(
          "AssumeRoleProvider(): temporary credentials are empty");
    }
  }
};  // class Tests

// Regression test for the host/port parsing guard in http::Url::Parse():
// bare IPv4/hostname keep the whole string as host with port 0; only an
// explicit ":port" suffix is split off.
void TestUrlParse() noexcept(false) {
  std::cout << "TestUrlParse()" << std::endl;

  struct UrlParseCase {
    std::string input;
    std::string host;
    unsigned int port;
  };

  const std::array<UrlParseCase, 4> cases = {{
      {"10.0.0.1", "10.0.0.1", 0},
      {"example.com", "example.com", 0},
      {"example.com:8080", "example.com", 8080},
      {"10.0.0.1:9000", "10.0.0.1", 9000},
  }};

  for (const auto& c : cases) {
    const minio::http::Url url = minio::http::Url::Parse(c.input);
    if (url.host != c.host || url.port != c.port) {
      throw std::runtime_error(
          "TestUrlParse(): Url::Parse(\"" + c.input + "\"): expected host='" +
          c.host + "' port=" + std::to_string(c.port) + "; got host='" +
          url.host + "' port=" + std::to_string(url.port));
    }
  }
}

// Unit tests of the web identity STS endpoint and AWS_CA_BUNDLE handling
// (T1-T14 of the cross-SDK matrix). None of them leaves the loopback
// interface.

namespace {

void Check(bool ok, const std::string& what) {
  if (!ok) throw std::runtime_error(what);
}

// The Microsoft CRT has _putenv_s() instead of setenv() and unsetenv(); an
// empty value removes the variable there.
void PutEnv(const std::string& name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name.c_str(), value.c_str());
#else
  setenv(name.c_str(), value.c_str(), 1);
#endif
}

void RemoveEnv(const std::string& name) {
#ifdef _WIN32
  _putenv_s(name.c_str(), "");
#else
  unsetenv(name.c_str());
#endif
}

// Sets or unsets environment variables for one scope and restores them.
class ScopedEnv {
 private:
  std::map<std::string, std::pair<bool, std::string>> saved_;

 public:
  ScopedEnv() = default;
  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;

  void Set(const std::string& name, const std::string& value) {
    Save(name);
    PutEnv(name, value);
  }

  void Unset(const std::string& name) {
    Save(name);
    RemoveEnv(name);
  }

  ~ScopedEnv() {
    for (const auto& [name, old] : saved_) {
      if (old.first) {
        PutEnv(name, old.second);
      } else {
        RemoveEnv(name);
      }
    }
  }

 private:
  void Save(const std::string& name) {
    if (saved_.count(name)) return;
    std::string value;
    bool found = minio::utils::GetEnv(value, name.c_str());
    saved_[name] = {found, value};
  }
};

// Resolves the endpoint from the environment, which the caller has set up.
minio::http::Url Resolve(const minio::http::Url& custom,
                         const std::string& region, bool* from_env = nullptr) {
  minio::http::Url out;
  bool from = false;
  minio::error::Error err =
      minio::creds::ResolveStsEndpoint(out, from, custom, region);
  Check(!err, "ResolveStsEndpoint(): " + err.String());
  if (from_env != nullptr) *from_env = from;
  return out;
}

// Generates a self-signed certificate (subject CN=cn) for 127.0.0.1 and writes
// the key and the certificate as PEM files.
void GenerateCert(const std::string& cert_file, const std::string& key_file,
                  const std::string& cn) {
  EVP_PKEY* pkey = EVP_RSA_gen(2048);
  Check(pkey != nullptr, "unable to generate key");
  X509* x509 = X509_new();
  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
  X509_gmtime_adj(X509_getm_notBefore(x509), 0);
  X509_gmtime_adj(X509_getm_notAfter(x509), 3600);
  X509_set_pubkey(x509, pkey);
  X509_NAME* name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(cn.c_str()),
                             -1, -1, 0);
  X509_set_issuer_name(x509, name);
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);
  char san[] = "IP:127.0.0.1";
  X509_EXTENSION* ext =
      X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, san);
  Check(ext != nullptr, "unable to create SAN");
  X509_add_ext(x509, ext, -1);
  X509_EXTENSION_free(ext);
  Check(X509_sign(x509, pkey, EVP_sha256()) > 0, "unable to sign certificate");

  FILE* f = fopen(cert_file.c_str(), "w");
  Check(f != nullptr, "unable to write " + cert_file);
  PEM_write_X509(f, x509);
  fclose(f);
  f = fopen(key_file.c_str(), "w");
  Check(f != nullptr, "unable to write " + key_file);
  PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  fclose(f);
  X509_free(x509);
  EVP_PKEY_free(pkey);
}

X509* ReadCert(const std::string& file) {
  FILE* f = fopen(file.c_str(), "r");
  Check(f != nullptr, "unable to read " + file);
  X509* x509 = PEM_read_X509(f, nullptr, nullptr, nullptr);
  fclose(f);
  Check(x509 != nullptr, "unable to parse " + file);
  return x509;
}

constexpr const char* kStsResponse =
    "<AssumeRoleWithWebIdentityResponse><AssumeRoleWithWebIdentityResult>"
    "<Credentials><AccessKeyId>AK</AccessKeyId><SecretAccessKey>SK"
    "</SecretAccessKey><SessionToken>ST</SessionToken>"
    "<Expiration>2099-01-01T00:00:00.000Z</Expiration></Credentials>"
    "</AssumeRoleWithWebIdentityResult></AssumeRoleWithWebIdentityResponse>";

// Runs an httplib server on the loopback interface for one scope.
template <typename SERVER>
class ScopedServer {
 private:
  SERVER& svr_;
  std::thread thread_;

 public:
  int port = 0;
  explicit ScopedServer(SERVER& svr) : svr_(svr) {
    port = svr_.bind_to_any_port("127.0.0.1");
    Check(port > 0, "unable to bind test server");
    thread_ = std::thread([this] { svr_.listen_after_bind(); });
    svr_.wait_until_ready();
  }
  ~ScopedServer() {
    svr_.stop();
    thread_.join();
  }
};

}  // namespace

void TestStsEndpointResolution() noexcept(false) {
  std::cout << "TestStsEndpointResolution()" << std::endl;

  ScopedEnv env;
  for (const char* name : {"AWS_ENDPOINT_URL_STS", "AWS_ENDPOINT_URL",
                           "AWS_IGNORE_CONFIGURED_ENDPOINT_URLS"}) {
    env.Unset(name);
  }

  const minio::http::Url none;
  const minio::http::Url custom =
      minio::http::Url::Parse("https://custom.example.com");
  const std::string sts = "https://sts.example.com";
  bool from_env = false;

  // The explicit endpoint wins over the variable and over the ignore flag.
  env.Set("AWS_ENDPOINT_URL_STS", sts);
  Check(
      Resolve(custom, "", &from_env).host == "custom.example.com" && !from_env,
      "explicit endpoint");
  env.Set("AWS_IGNORE_CONFIGURED_ENDPOINT_URLS", "true");
  Check(Resolve(custom, "").host == "custom.example.com", "explicit, ignored");
  env.Unset("AWS_IGNORE_CONFIGURED_ENDPOINT_URLS");

  // AWS_ENDPOINT_URL_STS wins over the default, with and without a region.
  Check(Resolve(none, "", &from_env).host == "sts.example.com" && from_env,
        "AWS_ENDPOINT_URL_STS");
  Check(Resolve(none, "eu-west-1").host == "sts.example.com", "over region");

  // The global AWS_ENDPOINT_URL is not read: the default host is used.
  env.Unset("AWS_ENDPOINT_URL_STS");
  env.Set("AWS_ENDPOINT_URL", "https://all.example.com");
  Check(Resolve(none, "", &from_env).host == "sts.amazonaws.com" && !from_env,
        "AWS_ENDPOINT_URL alone is ignored");
  env.Set("AWS_ENDPOINT_URL_STS", sts);
  Check(Resolve(none, "").host == "sts.example.com", "STS over global");
  env.Unset("AWS_ENDPOINT_URL");

  // An empty variable counts as unset.
  env.Set("AWS_ENDPOINT_URL_STS", "");
  Check(Resolve(none, "").host == "sts.amazonaws.com", "empty");
  env.Set("AWS_ENDPOINT_URL_STS", sts);

  // The ignore flag selects the default host; "true" is case-insensitive and
  // other values have no effect.
  env.Set("AWS_IGNORE_CONFIGURED_ENDPOINT_URLS", "true");
  Check(Resolve(none, "eu-west-1", &from_env).host ==
                "sts.eu-west-1.amazonaws.com" &&
            !from_env,
        "ignore flag");
  env.Set("AWS_IGNORE_CONFIGURED_ENDPOINT_URLS", "TRUE");
  Check(Resolve(none, "").host == "sts.amazonaws.com", "ignore flag TRUE");
  env.Set("AWS_IGNORE_CONFIGURED_ENDPOINT_URLS", "false");
  Check(Resolve(none, "").host == "sts.example.com", "ignore flag false");
  env.Set("AWS_IGNORE_CONFIGURED_ENDPOINT_URLS", "1");
  Check(Resolve(none, "").host == "sts.example.com", "ignore flag 1");
  env.Unset("AWS_IGNORE_CONFIGURED_ENDPOINT_URLS");

  // The path is kept, including repeated leading slashes.
  env.Set("AWS_ENDPOINT_URL_STS", "https://sts.example.com/sts/ns/name");
  Check(Resolve(none, "").path == "sts/ns/name", "path");
  env.Set("AWS_ENDPOINT_URL_STS", "https://sts.example.com//tenant/sts");
  Check(Resolve(none, "").path == "//tenant/sts", "double slash");

  // The defaults, including the China partition.
  env.Unset("AWS_ENDPOINT_URL_STS");
  minio::http::Url cn = Resolve(none, "cn-north-1");
  Check(cn.https && cn.host == "sts.cn-north-1.amazonaws.com.cn", "cn region");
  Check(Resolve(none, "us-east-1").host == "sts.us-east-1.amazonaws.com",
        "region");

  // Malformed ports are rejected; valid ones and IPv6 authorities are not.
  for (const char* bad :
       {"https://sts.example.com:443extra", "https://h:abc", "https://h:",
        "https://h:65536", "https://h:0", "https://h:000", "https://[::1]x",
        "https://h:99999999999", "ftp://x", "https://h?next=:99999999999/x"}) {
    env.Set("AWS_ENDPOINT_URL_STS", bad);
    minio::http::Url out;
    bool from = false;
    Check(bool(minio::creds::ResolveStsEndpoint(out, from, none, "")),
          std::string("endpoint must be rejected: ") + bad);
  }
  env.Set("AWS_ENDPOINT_URL_STS", "https://sts.example.com:8443/x");
  Check(Resolve(none, "").port == 8443, "valid port");
  env.Set("AWS_ENDPOINT_URL_STS", "https://sts.example.com:008443");
  Check(Resolve(none, "").port == 8443, "port with leading zeros");
  env.Set("AWS_ENDPOINT_URL_STS", "https://[::1]:9000");
  Check(Resolve(none, "").port == 9000, "ipv6 port");
  env.Set("AWS_ENDPOINT_URL_STS", sts);
  Check(Resolve(none, "").port == 0, "no port");
}

void TestIamAwsProviderEnv() noexcept(false) {
  std::cout << "TestIamAwsProviderEnv()" << std::endl;

  namespace fs = std::filesystem;
  const fs::path dir =
      fs::temp_directory_path() /
      ("minio-cpp-sts-" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  struct Cleanup {
    fs::path dir;
    ~Cleanup() {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }
  } cleanup{dir};

  const std::string cert = (dir / "cert.pem").string();
  const std::string key = (dir / "key.pem").string();
  const std::string other_cert = (dir / "other-cert.pem").string();
  const std::string other_key = (dir / "other-key.pem").string();
  const std::string empty_bundle = (dir / "empty.pem").string();
  const std::string token = (dir / "token").string();
  GenerateCert(cert, key, "server");
  GenerateCert(other_cert, other_key, "other");
  std::ofstream(empty_bundle) << "not a certificate\n";
  std::ofstream(token) << R"({"access_token":"jwt","expires_in":3600})";

  ScopedEnv env;
  for (const char* name :
       {"AWS_ENDPOINT_URL_STS", "AWS_ENDPOINT_URL",
        "AWS_IGNORE_CONFIGURED_ENDPOINT_URLS", "AWS_CA_BUNDLE", "SSL_CERT_FILE",
        "AWS_REGION", "AWS_ROLE_ARN", "AWS_ROLE_SESSION_NAME",
        "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI",
        "AWS_CONTAINER_CREDENTIALS_FULL_URI", "AWS_WEB_IDENTITY_TOKEN_FILE"}) {
    env.Unset(name);
  }

  // T8: the container credentials path ignores the endpoint variables.
  {
    httplib::Server svr;
    svr.Get("/creds", [](const httplib::Request&, httplib::Response& res) {
      res.set_content(
          R"({"Code":"Success","AccessKeyId":"AK","SecretAccessKey":"SK",)"
          R"("Token":"T","Expiration":"2099-01-01T00:00:00Z"})",
          "application/json");
    });
    ScopedServer<httplib::Server> running(svr);
    env.Set("AWS_ENDPOINT_URL_STS", "https://127.0.0.1:1");
    env.Set("AWS_ENDPOINT_URL", "https://127.0.0.1:1");
    env.Set("AWS_CONTAINER_CREDENTIALS_FULL_URI",
            "http://127.0.0.1:" + std::to_string(running.port) + "/creds");
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(bool(creds) && creds.access_key == "AK", "T8: " + creds.err.String());
    env.Unset("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    env.Unset("AWS_ENDPOINT_URL_STS");
    env.Unset("AWS_ENDPOINT_URL");
  }

  env.Set("AWS_WEB_IDENTITY_TOKEN_FILE", token);

  httplib::SSLServer svr(cert.c_str(), key.c_str());
  Check(svr.is_valid(), "unable to start TLS test server");
  // Only the exact request targets succeed, so a path that is altered on the
  // way makes the fetch fail.
  svr.Post(R"(.*)", [](const httplib::Request& req, httplib::Response& res) {
    if (req.path == "/sts/ns/name" || req.path == "//tenant/sts") {
      res.set_content(kStsResponse, "text/xml");
    } else {
      res.status = 404;
    }
  });
  ScopedServer<httplib::SSLServer> running(svr);
  const std::string endpoint =
      "https://127.0.0.1:" + std::to_string(running.port) + "/sts/ns/name";
  env.Set("AWS_ENDPOINT_URL_STS", endpoint);

  // T10: without AWS_CA_BUNDLE the self-signed server is rejected.
  {
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(
        !creds && creds.err.String().find("verification") != std::string::npos,
        "T10: expected a verification failure, got: " + creds.err.String());
  }

  // AWS_CA_BUNDLE is read only for an endpoint from AWS_ENDPOINT_URL_STS: with
  // an explicit endpoint, a missing or valid bundle changes nothing.
  {
    minio::http::Url explicit_url = minio::http::Url::Parse(
        "https://127.0.0.1:" + std::to_string(running.port) + "/sts/ns/name");
    for (const std::string& bundle : {(dir / "missing.pem").string(), cert}) {
      env.Set("AWS_CA_BUNDLE", bundle);
      minio::creds::IamAwsProvider provider(explicit_url);
      minio::creds::Credentials creds = provider.Fetch();
      Check(!creds &&
                creds.err.String().find("AWS_CA_BUNDLE") == std::string::npos &&
                creds.err.String().find("verification") != std::string::npos,
            "explicit endpoint ignores AWS_CA_BUNDLE: " + creds.err.String());
    }
    env.Unset("AWS_CA_BUNDLE");
  }

  // T9: AWS_CA_BUNDLE is trusted, and the endpoint path reaches the server.
  {
    env.Set("AWS_CA_BUNDLE", cert);
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(bool(creds) && creds.access_key == "AK", "T9: " + creds.err.String());

    // T7: repeated leading slashes of the path reach the server unchanged.
    env.Set(
        "AWS_ENDPOINT_URL_STS",
        "https://127.0.0.1:" + std::to_string(running.port) + "//tenant/sts");
    minio::creds::IamAwsProvider provider2;
    creds = provider2.Fetch();
    Check(bool(creds), "T7 double slash: " + creds.err.String());
    env.Set("AWS_ENDPOINT_URL_STS", endpoint);
  }

  // A bundle with an OpenSSL "TRUSTED CERTIFICATE" block is accepted and
  // trusted, as the store loader of the transport does.
  {
    const std::string trusted = (dir / "trusted.pem").string();
    X509* x509 = ReadCert(cert);
    FILE* f = fopen(trusted.c_str(), "w");
    Check(f != nullptr, "unable to write " + trusted);
    PEM_write_X509_AUX(f, x509);
    fclose(f);
    X509_free(x509);
    env.Set("AWS_CA_BUNDLE", trusted);
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(bool(creds), "TRUSTED CERTIFICATE bundle: " + creds.err.String());
    env.Set("AWS_CA_BUNDLE", cert);
  }

  // B6: the bundle adds to SSL_CERT_FILE instead of losing to it.
  {
    env.Set("SSL_CERT_FILE", other_cert);
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(bool(creds), "B6 additive: " + creds.err.String());
    // SSL_CERT_FILE still contributes when the bundle is set: the server
    // certificate is in SSL_CERT_FILE, an unrelated one in AWS_CA_BUNDLE.
    env.Set("AWS_CA_BUNDLE", other_cert);
    env.Set("SSL_CERT_FILE", cert);
    minio::creds::IamAwsProvider provider3;
    creds = provider3.Fetch();
    Check(bool(creds), "B6 combined SSL_CERT_FILE: " + creds.err.String());
    // An unreadable SSL_CERT_FILE is an error, not silently skipped.
    env.Set("SSL_CERT_FILE", (dir / "missing-ssl.pem").string());
    minio::creds::IamAwsProvider provider4;
    creds = provider4.Fetch();
    Check(!creds &&
              creds.err.String().find("missing-ssl.pem") != std::string::npos,
          "unreadable SSL_CERT_FILE: " + creds.err.String());
    // SSL_CERT_FILE alone still works for its own certificate.
    env.Unset("AWS_CA_BUNDLE");
    env.Set("SSL_CERT_FILE", cert);
    minio::creds::IamAwsProvider provider2;
    creds = provider2.Fetch();
    Check(bool(creds), "B6 SSL_CERT_FILE: " + creds.err.String());
    env.Unset("SSL_CERT_FILE");
  }

  // B6 system roots: a certificate that is only in the system trust
  // directory (SSL_CERT_DIR, an OpenSSL hashed name) stays trusted next to
  // AWS_CA_BUNDLE, through the system-root path and not SSL_CERT_FILE.
  {
    X509* x509 = ReadCert(cert);
    char hash[16];
    snprintf(hash, sizeof(hash), "%08lx.0", X509_subject_name_hash(x509));
    X509_free(x509);
    const fs::path certs_dir = dir / "certs";
    fs::create_directories(certs_dir);
    fs::copy_file(cert, certs_dir / hash);

    env.Set("SSL_CERT_DIR", certs_dir.string());
    env.Set("AWS_CA_BUNDLE", other_cert);
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(bool(creds), "B6 system roots: " + creds.err.String());
    env.Unset("SSL_CERT_DIR");
  }

  // T11: the provider takes no HTTP client, so the bundle always applies; the
  // case does not exist in this SDK.

  // T12: a missing bundle is an error naming AWS_CA_BUNDLE.
  {
    env.Set("AWS_CA_BUNDLE", (dir / "missing.pem").string());
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(
        !creds && creds.err.String().find("AWS_CA_BUNDLE") != std::string::npos,
        "T12: " + creds.err.String());
  }

  // T13: a bundle without certificates is an error naming AWS_CA_BUNDLE.
  {
    env.Set("AWS_CA_BUNDLE", empty_bundle);
    minio::creds::IamAwsProvider provider;
    minio::creds::Credentials creds = provider.Fetch();
    Check(
        !creds && creds.err.String().find("AWS_CA_BUNDLE") != std::string::npos,
        "T13: " + creds.err.String());
  }
}

int main(int /*argc*/, char* /*argv*/[]) {
  // Unit check first so a parsing regression fails fast without a server.
  try {
    TestUrlParse();
    TestStsEndpointResolution();
    TestIamAwsProviderEnv();
  } catch (const std::runtime_error& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

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
  tests.SelectStatsMetrics();
  tests.AssumeRoleProvider();

  return EXIT_SUCCESS;
}
