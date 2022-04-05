// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2021 MinIO, Inc.
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

#pragma once

#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include "s3_http.h"

struct Dictionary_Error : public std::runtime_error {
  Dictionary_Error(const std::string& msg = "") : std::runtime_error(msg) {}
};

namespace Minio {
// A simple string-to-string dictionary, with additional methods for conversion
// to doubles and integers.
class Headers {
  Minio::Http::HeaderValueCollection entries;

 public:
  Headers() {}
  typedef Minio::Http::HeaderValueCollection::iterator iterator;
  typedef Minio::Http::HeaderValueCollection::const_iterator const_iterator;

  iterator begin() { return entries.begin(); }
  const_iterator begin() const { return entries.begin(); }

  iterator end() { return entries.end(); }
  const_iterator end() const { return entries.end(); }

  std::pair<iterator, iterator> equal_range(const std::string& key) {
    return entries.equal_range(key);
  }

  std::pair<const_iterator, const_iterator> equal_range(
      const std::string& key) const {
    return entries.equal_range(key);
  }

  void Clear() { entries.clear(); }

  bool Exists(const std::string& key) const {
    return (entries.find(key) != entries.end());
  }

  // Get first value for key if one exists. Return true if a value found for
  // key, return false otherwise.
  bool Get(const std::string& key, std::string& value) const {
    const_iterator val = entries.find(key);
    if (val != entries.end()) value = val->second;
    return (val != entries.end());
  }

  bool Get(const std::string& key, double& value) const {
    const_iterator val = entries.find(key);
    if (val != entries.end()) value = strtod(val->second.c_str(), NULL);
    return (val != entries.end());
  }

  bool Get(const std::string& key, int& value) const {
    const_iterator val = entries.find(key);
    if (val != entries.end()) value = strtol(val->second.c_str(), NULL, 0);
    return (val != entries.end());
  }

  bool Get(const std::string& key, long& value) const {
    const_iterator val = entries.find(key);
    if (val != entries.end()) value = strtol(val->second.c_str(), NULL, 0);
    return (val != entries.end());
  }

  bool Get(const std::string& key, size_t& value) const {
    const_iterator val = entries.find(key);
    if (val != entries.end()) value = strtol(val->second.c_str(), NULL, 0);
    return (val != entries.end());
  }

  // Get first value for key if one exists. Return value if found for key,
  // return defaultVal otherwise.
  const std::string& GetWithDefault(const std::string& key,
                                    const std::string& defaultVal) const {
    const_iterator val = entries.find(key);
    return (val != entries.end()) ? val->second : defaultVal;
  }

  double GetWithDefault(const std::string& key, double defaultVal) const {
    const_iterator val = entries.find(key);
    return (val != entries.end()) ? strtod(val->second.c_str(), NULL)
                                  : defaultVal;
  }

  int GetWithDefault(const std::string& key, int defaultVal) const {
    const_iterator val = entries.find(key);
    return (val != entries.end()) ? strtol(val->second.c_str(), NULL, 0)
                                  : defaultVal;
  }

  long GetWithDefault(const std::string& key, long defaultVal) const {
    const_iterator val = entries.find(key);
    return (val != entries.end()) ? strtol(val->second.c_str(), NULL, 0)
                                  : defaultVal;
  }

  size_t GetWithDefault(const std::string& key, size_t defaultVal) const {
    const_iterator val = entries.find(key);
    return (val != entries.end()) ? strtol(val->second.c_str(), NULL, 0)
                                  : defaultVal;
  }

  // Insert entry into dictionary, overwrites existing key values.
  void Insert(const std::string& key, const std::string& value) {
    entries.insert(std::make_pair(key, value));
  }

  // Update value for existing key if possible, insert entry into dictionary if
  // no value for key
  void Update(const std::string& key, const std::string& value) {
    iterator val = entries.find(key);
    if (val == entries.end())
      Insert(key, value);
    else
      val->second = value;
  }
};
}  // namespace Minio
