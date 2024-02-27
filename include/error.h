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

#ifndef _MINIO_ERROR_H
#define _MINIO_ERROR_H

#include <string>
#include <ostream>

namespace minio {
namespace error {
class Error {
 private:
  std::string msg_;

 public:
  Error() = default;
  Error(std::string_view msg) : msg_(msg) {}
  ~Error() = default;

  std::string String() const { return msg_; }
  explicit operator bool() const { return !msg_.empty(); }

  friend std::ostream& operator <<(std::ostream& s, const Error& e) {
    return s << e.msg_;
  }
};  // class Error

const static Error SUCCESS;
}  // namespace error
}  // namespace minio

#endif  // #ifndef _MINIO_ERROR_H
