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

#include "types.h"

minio::s3::RetentionMode minio::s3::StringToRetentionMode(
    std::string_view str) throw() {
  if (str == "GOVERNANCE") return RetentionMode::kGovernance;
  if (str == "COMPLIANCE") return RetentionMode::kCompliance;

  std::cerr << "ABORT: Unknown retention mode. This should not happen."
            << std::endl;
  std::terminate();

  return RetentionMode::kGovernance;  // never reaches here.
}

minio::s3::LegalHold minio::s3::StringToLegalHold(
    std::string_view str) throw() {
  if (str == "ON") return LegalHold::kOn;
  if (str == "OFF") return LegalHold::kOff;

  std::cerr << "ABORT: Unknown legal hold. This should not happen."
            << std::endl;
  std::terminate();

  return LegalHold::kOff;  // never reaches here.
}

minio::s3::Directive minio::s3::StringToDirective(
    std::string_view str) throw() {
  if (str == "COPY") return Directive::kCopy;
  if (str == "REPLACE") return Directive::kReplace;

  std::cerr << "ABORT: Unknown directive. This should not happen." << std::endl;
  std::terminate();

  return Directive::kCopy;  // never reaches here.
}
