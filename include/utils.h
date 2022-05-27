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

#ifndef _MINIO_UTILS_H
#define _MINIO_UTILS_H

#include <arpa/inet.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include <cmath>
#include <cstring>
#include <curlpp/cURLpp.hpp>
#include <iomanip>
#include <iostream>
#include <list>
#include <regex>
#include <set>

#include "error.h"

namespace minio {
namespace utils {
inline constexpr unsigned int kMaxMultipartCount = 10000;  // 10000 parts
inline constexpr unsigned long kMaxObjectSize =
    5L * 1024 * 1024 * 1024 * 1024;                                     // 5TiB
inline constexpr unsigned long kMaxPartSize = 5L * 1024 * 1024 * 1024;  // 5GiB
inline constexpr unsigned int kMinPartSize = 5 * 1024 * 1024;           // 5MiB

bool GetEnv(std::string& var, const char* name);

std::string GetHomeDir();

std::string Printable(std::string s);

unsigned long CRC32(std::string_view str);

unsigned int Int(std::string_view str);

// FormatTime formats time as per format.
std::string FormatTime(const std::tm* time, const char* format);

// StringToBool converts string to bool.
bool StringToBool(std::string str);

// BoolToString converts bool to string.
inline const char* const BoolToString(bool b) { return b ? "true" : "false"; }

// Trim trims leading and trailing character of a string.
std::string Trim(std::string_view str, char ch = ' ');

// CheckNonemptystring checks whether string is not empty after trimming
// whitespaces.
bool CheckNonEmptyString(std::string_view str);

// ToLower converts string to lower case.
std::string ToLower(std::string str);

// StartsWith returns whether str starts with prefix or not.
bool StartsWith(std::string_view str, std::string_view prefix);

// EndsWith returns whether str ends with suffix or not.
bool EndsWith(std::string_view str, std::string_view suffix);

// Contains returns whether str has ch.
bool Contains(std::string_view str, char ch);

// Contains returns whether str has substr.
bool Contains(std::string_view str, std::string_view substr);

// Join returns a string of joined values by delimiter.
std::string Join(std::list<std::string> values, std::string delimiter);

// Join returns a string of joined values by delimiter.
std::string Join(std::vector<std::string> values, std::string delimiter);

// EncodePath does URL encoding of path. It also normalizes multiple slashes.
std::string EncodePath(std::string& path);

// Sha256hash computes SHA-256 of data and return hash as hex encoded value.
std::string Sha256Hash(std::string_view str);

// Base64Encode encodes string to base64.
std::string Base64Encode(std::string_view str);

// Md5sumHash computes MD5 of data and return hash as Base64 encoded value.
std::string Md5sumHash(std::string_view str);

error::Error CheckBucketName(std::string_view bucket_name, bool strict = false);
error::Error ReadPart(std::istream& stream, char* buf, size_t size,
                      size_t& bytes_read);
error::Error CalcPartInfo(long object_size, size_t& part_size,
                          long& part_count);

/**
 * Time represents date and time with timezone.
 */
class Time {
 private:
  struct timeval tv_ = {0, 0};
  bool utc_ = false;

 public:
  Time() {}

  Time(std::time_t tv_sec, suseconds_t tv_usec, bool utc) {
    this->tv_.tv_sec = tv_sec;
    this->tv_.tv_usec = tv_usec;
    this->utc_ = utc;
  }

  void Add(time_t seconds) { tv_.tv_sec += seconds; }

  std::tm* ToUTC();

  std::string ToSignerDate();

  std::string ToAmzDate();

  std::string ToHttpHeaderValue();

  static Time FromHttpHeaderValue(const char* value);

  std::string ToISO8601UTC();

  static Time FromISO8601UTC(const char* value);

  static Time Now() {
    Time t;
    gettimeofday(&t.tv_, NULL);
    return t;
  }

  operator bool() const { return tv_.tv_sec != 0 && tv_.tv_usec != 0; }
};  // class Time

/**
 * Multimap represents dictionary of keys and their multiple values.
 */
class Multimap {
 private:
  std::map<std::string, std::set<std::string>> map_;
  std::map<std::string, std::set<std::string>> keys_;

 public:
  Multimap() {}

  Multimap(const Multimap& headers) { this->AddAll(headers); }

  void Add(std::string key, std::string value);

  void AddAll(const Multimap& headers);

  std::list<std::string> ToHttpHeaders();

  std::string ToQueryString();

  operator bool() const { return !map_.empty(); }

  bool Contains(std::string_view key);

  std::list<std::string> Get(std::string_view key);

  std::string GetFront(std::string_view key);

  std::list<std::string> Keys();

  void GetCanonicalHeaders(std::string& signed_headers,
                           std::string& canonical_headers);

  std::string GetCanonicalQueryString();
};  // class Multimap

/**
 * CharBuffer represents stream buffer wrapping character array and its size.
 */
struct CharBuffer : std::streambuf {
  CharBuffer(char* buf, size_t size) { this->setg(buf, buf, buf + size); }

  pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                   std::ios_base::openmode which = std::ios_base::in) override {
    if (dir == std::ios_base::cur)
      gbump(off);
    else if (dir == std::ios_base::end)
      setg(eback(), egptr() + off, egptr());
    else if (dir == std::ios_base::beg)
      setg(eback(), eback() + off, egptr());
    return gptr() - eback();
  }

  pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
    return seekoff(sp - pos_type(off_type(0)), std::ios_base::beg, which);
  }
};  // struct CharBuffer
}  // namespace utils
}  // namespace minio

#endif  // #ifndef _MINIO_UTILS_H
