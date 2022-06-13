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

#ifndef _MINIO_S3_SELECT_H
#define _MINIO_S3_SELECT_H

#include <pugixml.hpp>

#include "http.h"
#include "types.h"

namespace minio {
namespace s3 {
class SelectHandler {
 private:
  SelectResultFunction result_func_ = NULL;

  bool done_ = false;
  std::string response_;

  std::string prelude_;
  bool prelude_read_ = false;

  std::string prelude_crc_;
  bool prelude_crc_read_ = false;

  unsigned int total_length_ = 0;

  std::string data_;
  bool data_read_ = false;

  std::string message_crc_;
  bool message_crc_read_ = false;

  void Reset();
  bool ReadPrelude();
  bool ReadPreludeCrc();
  bool ReadData();
  bool ReadMessageCrc();
  error::Error DecodeHeader(std::map<std::string, std::string>& headers,
                            std::string data);
  bool process(http::DataFunctionArgs args, bool& cont);

 public:
  SelectHandler(SelectResultFunction result_func) {
    this->result_func_ = result_func;
  }

  bool DataFunction(http::DataFunctionArgs args);
};  // struct SelectHandler
}  // namespace s3
}  // namespace minio

#endif  // #ifndef _MINIO_S3_SELECT_H
