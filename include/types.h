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

#ifndef _MINIO_S3_TYPES_H
#define _MINIO_S3_TYPES_H

#include <iostream>

#include "utils.h"

namespace minio {
namespace s3 {
enum class RetentionMode { kGovernance, kCompliance };

// StringToRetentionMode converts string to retention mode enum.
RetentionMode StringToRetentionMode(std::string_view str) throw();

constexpr bool IsRetentionModeValid(RetentionMode& retention) {
  switch (retention) {
    case RetentionMode::kGovernance:
    case RetentionMode::kCompliance:
      return true;
  }
  return false;
}

// RetentionModeToString converts retention mode enum to string.
constexpr const char* RetentionModeToString(RetentionMode& retention) throw() {
  switch (retention) {
    case RetentionMode::kGovernance:
      return "GOVERNANCE";
    case RetentionMode::kCompliance:
      return "COMPLIANCE";
    default: {
      std::cerr << "ABORT: Unknown retention mode. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

enum class LegalHold { kOn, kOff };

// StringToLegalHold converts string to legal hold enum.
LegalHold StringToLegalHold(std::string_view str) throw();

constexpr bool IsLegalHoldValid(LegalHold& legal_hold) {
  switch (legal_hold) {
    case LegalHold::kOn:
    case LegalHold::kOff:
      return true;
  }
  return false;
}

// LegalHoldToString converts legal hold enum to string.
constexpr const char* LegalHoldToString(LegalHold& legal_hold) throw() {
  switch (legal_hold) {
    case LegalHold::kOn:
      return "ON";
    case LegalHold::kOff:
      return "OFF";
    default: {
      std::cerr << "ABORT: Unknown legal hold. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

enum class Directive { kCopy, kReplace };

// StringToDirective converts string to directive enum.
Directive StringToDirective(std::string_view str) throw();

// DirectiveToString converts directive enum to string.
constexpr const char* DirectiveToString(Directive& directive) throw() {
  switch (directive) {
    case Directive::kCopy:
      return "COPY";
    case Directive::kReplace:
      return "REPLACE";
    default: {
      std::cerr << "ABORT: Unknown directive. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

struct Bucket {
  std::string name;
  utils::Time creation_date;
};  // struct Bucket

struct Part {
  unsigned int number;
  std::string etag;
  utils::Time last_modified;
  size_t size;
};  // struct Part

struct Retention {
  RetentionMode mode;
  utils::Time retain_until_date;
};  // struct Retention
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_S3_TYPES_H
