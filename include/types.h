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
#include <nlohmann/json.hpp>

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

enum class CompressionType { kNone, kGZip, kBZip2 };

// CompressionTypeToString converts compression type enum to string.
constexpr const char* CompressionTypeToString(CompressionType& ctype) throw() {
  switch (ctype) {
    case CompressionType::kNone:
      return "NONE";
    case CompressionType::kGZip:
      return "GZIP";
    case CompressionType::kBZip2:
      return "BZIP2";
    default: {
      std::cerr << "ABORT: Unknown compression type. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

enum class FileHeaderInfo { kUse, kIgnore, kNone };

// FileHeaderInfoToString converts file header info enum to string.
constexpr const char* FileHeaderInfoToString(FileHeaderInfo& info) throw() {
  switch (info) {
    case FileHeaderInfo::kUse:
      return "USE";
    case FileHeaderInfo::kIgnore:
      return "IGNORE";
    case FileHeaderInfo::kNone:
      return "NONE";
    default: {
      std::cerr << "ABORT: Unknown file header info. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

enum class JsonType { kDocument, kLines };

// JsonTypeToString converts JSON type enum to string.
constexpr const char* JsonTypeToString(JsonType& jtype) throw() {
  switch (jtype) {
    case JsonType::kDocument:
      return "DOCUMENT";
    case JsonType::kLines:
      return "LINES";
    default: {
      std::cerr << "ABORT: Unknown JSON type. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

enum class QuoteFields { kAlways, kAsNeeded };

// QuoteFieldsToString converts quote fields enum to string.
constexpr const char* QuoteFieldsToString(QuoteFields& qtype) throw() {
  switch (qtype) {
    case QuoteFields::kAlways:
      return "ALWAYS";
    case QuoteFields::kAsNeeded:
      return "ASNEEDED";
    default: {
      std::cerr << "ABORT: Unknown quote fields. This should not happen."
                << std::endl;
      std::terminate();
    }
  }
  return NULL;
}

struct CsvInputSerialization {
  CompressionType* compression_type = NULL;
  bool allow_quoted_record_delimiter = false;
  char comments = 0;
  char field_delimiter = 0;
  FileHeaderInfo* file_header_info = NULL;
  char quote_character = 0;
  char quote_escape_character = 0;
  char record_delimiter = 0;
};  // struct CsvInputSerialization

struct JsonInputSerialization {
  CompressionType* compression_type = NULL;
  JsonType* json_type = NULL;
};  // struct JsonInputSerialization

struct ParquetInputSerialization {};  // struct ParquetInputSerialization

struct CsvOutputSerialization {
  char field_delimiter = 0;
  char quote_character = 0;
  char quote_escape_character = 0;
  QuoteFields* quote_fields = NULL;
  char record_delimiter = 0;
};  // struct CsvOutputSerialization

struct JsonOutputSerialization {
  char record_delimiter = 0;
};  // struct JsonOutputSerialization

struct SelectRequest {
  std::string expr;
  CsvInputSerialization* csv_input = NULL;
  JsonInputSerialization* json_input = NULL;
  ParquetInputSerialization* parquet_input = NULL;
  CsvOutputSerialization* csv_output = NULL;
  JsonOutputSerialization* json_output = NULL;
  bool request_progress = false;
  size_t* scan_start_range = NULL;
  size_t* scan_end_range = NULL;

  SelectRequest(std::string expression, CsvInputSerialization* csv_input,
                CsvOutputSerialization* csv_output) {
    this->expr = expression;
    this->csv_input = csv_input;
    this->csv_output = csv_output;
  }

  SelectRequest(std::string expression, CsvInputSerialization* csv_input,
                JsonOutputSerialization* json_output) {
    this->expr = expression;
    this->csv_input = csv_input;
    this->json_output = json_output;
  }

  SelectRequest(std::string expression, JsonInputSerialization* json_input,
                CsvOutputSerialization* csv_output) {
    this->expr = expression;
    this->json_input = json_input;
    this->csv_output = csv_output;
  }

  SelectRequest(std::string expression, JsonInputSerialization* json_input,
                JsonOutputSerialization* json_output) {
    this->expr = expression;
    this->json_input = json_input;
    this->json_output = json_output;
  }

  SelectRequest(std::string expression,
                ParquetInputSerialization* parquet_input,
                CsvOutputSerialization* csv_output) {
    this->expr = expression;
    this->parquet_input = parquet_input;
    this->csv_output = csv_output;
  }

  SelectRequest(std::string expression,
                ParquetInputSerialization* parquet_input,
                JsonOutputSerialization* json_output) {
    this->expr = expression;
    this->parquet_input = parquet_input;
    this->json_output = json_output;
  }

  std::string ToXML();
};  // struct SelectRequest

struct SelectResult {
  error::Error err = error::SUCCESS;
  bool ended = false;
  long int bytes_scanned = -1;
  long int bytes_processed = -1;
  long int bytes_returned = -1;
  std::string records;

  SelectResult() { this->ended = true; }

  SelectResult(error::Error err) {
    this->err = err;
    this->ended = true;
  }

  SelectResult(long int bytes_scanned, long int bytes_processed,
               long int bytes_returned) {
    this->bytes_scanned = bytes_scanned;
    this->bytes_processed = bytes_processed;
    this->bytes_returned = bytes_returned;
  }

  SelectResult(std::string records) { this->records = records; }
};

using SelectResultFunction = std::function<bool(SelectResult)>;

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

struct DeleteObject {
  std::string name;
  std::string version_id;
};  // struct DeleteObject

struct NotificationRecord {
  std::string event_version;
  std::string event_source;
  std::string aws_region;
  std::string event_time;
  std::string event_name;
  struct {
    std::string principal_id;
  } user_identity;
  struct {
    std::string principal_id;
    std::string region;
    std::string source_ip_address;
  } request_parameters;
  struct {
    std::string content_length;
    std::string x_amz_request_id;
    std::string x_minio_deployment_id;
    std::string x_minio_origin_endpoint;
  } response_elements;
  struct {
    std::string s3_schema_version;
    std::string configuration_id;
    struct {
      std::string name;
      std::string arn;
      struct {
        std::string principal_id;
      } owner_identity;
    } bucket;
    struct {
      std::string key;
      size_t size;
      std::string etag;
      std::string content_type;
      std::map<std::string, std::string> user_metadata;
      std::string sequencer;
    } object;
  } s3;
  struct {
    std::string host;
    std::string port;
    std::string user_agent;
  } source;

  static NotificationRecord ParseJSON(nlohmann::json j_record);
};  // struct NotificationRecord

using NotificationRecordsFunction =
    std::function<bool(std::list<NotificationRecord>)>;

struct FilterValue {
 private:
  std::string value_;
  bool is_value_set_ = false;

 public:
  FilterValue() {}
  FilterValue(std::string value) {
    this->value_ = value;
    this->is_value_set_ = true;
  }
  operator bool() const { return is_value_set_; }
  std::string Value() { return value_; }
};  // struct FilterValue

struct PrefixFilterRule : public FilterValue {
  static constexpr const char* name = "prefix";

  PrefixFilterRule() {}
  PrefixFilterRule(std::string value) : FilterValue(value) {}
};  // struct PrefixFilterRule

struct SuffixFilterRule : public FilterValue {
  static constexpr const char* name = "suffix";

  SuffixFilterRule() {}
  SuffixFilterRule(std::string value) : FilterValue(value) {}
};  // struct SuffixFilterRule

struct NotificationCommonConfig {
  std::list<std::string> events;
  std::string id;
  PrefixFilterRule prefix_filter_rule;
  SuffixFilterRule suffix_filter_rule;
};  // struct NotificationCommonConfig

struct CloudFuncConfig : public NotificationCommonConfig {
  std::string cloud_func;
};  // struct CloudFuncConfig

struct QueueConfig : public NotificationCommonConfig {
  std::string queue;
};  // struct QueueConfig

struct TopicConfig : public NotificationCommonConfig {
  std::string topic;
};  // struct TopicConfig

struct NotificationConfig {
  std::list<CloudFuncConfig> cloud_func_config_list;
  std::list<QueueConfig> queue_config_list;
  std::list<TopicConfig> topic_config_list;

  std::string ToXML();
};  // struct NotificationConfig

struct SseConfig {
  std::string sse_algorithm;
  std::string kms_master_key_id;

  SseConfig() {}
  static SseConfig S3() {
    SseConfig config;
    config.sse_algorithm = "AES256";
    return config;
  }
  static SseConfig Kms(std::string masterkeyid = "") {
    SseConfig config;
    config.sse_algorithm = "aws:kms";
    config.kms_master_key_id = masterkeyid;
    return config;
  }
  operator bool() const { return !sse_algorithm.empty(); }
};  //  struct SseConfig

struct Tag {
  std::string key;
  std::string value;

  operator bool() const { return !key.empty(); }
};  // struct Tag

struct Prefix {
 private:
  std::string value_;
  bool is_set_ = false;

 public:
  Prefix() {}
  Prefix(std::string value) {
    this->value_ = value;
    this->is_set_ = true;
  }
  operator bool() const { return is_set_; }
  std::string Get() { return value_; }
};  // struct Prefix

struct Integer {
 private:
  int value_ = 0;
  bool is_set_ = false;

 public:
  Integer() {}
  Integer(int value) {
    this->value_ = value;
    this->is_set_ = true;
  }
  operator bool() const { return is_set_; }
  int Get() { return value_; }
};  // struct Integer

struct Boolean {
 private:
  bool value_ = false;
  bool is_set_ = false;

 public:
  Boolean() {}
  Boolean(bool value) {
    this->value_ = value;
    this->is_set_ = true;
  }
  operator bool() const { return is_set_; }
  bool Get() { return value_; }
};  // struct Boolean

struct AndOperator {
  Prefix prefix;
  std::map<std::string, std::string> tags;

  operator bool() const { return prefix || !tags.empty(); }
};  // struct AndOperator

struct Filter {
  AndOperator and_operator;
  Prefix prefix;
  Tag tag;

  operator bool() const { return and_operator ^ prefix ^ tag; }
};  // struct Filter

struct AccessControlTranslation {
  std::string owner = "Destination";

  void Enable() { enabled_ = true; }
  operator bool() const { return enabled_; }

 private:
  bool enabled_ = false;
};  // struct AccessControlTranslation

struct EncryptionConfig {
  std::string replica_kms_key_id;

  void Enable() { enabled_ = true; }
  operator bool() const { return enabled_; }

 private:
  bool enabled_ = false;
};  // struct EncryptionConfig

struct Metrics {
  unsigned int event_threshold_minutes = 15;
  bool status = false;

  void Enable() { enabled_ = true; }
  operator bool() const { return enabled_; }

 private:
  bool enabled_ = false;
};  // struct Metrics

struct ReplicationTime {
  unsigned int time_minutes = 15;
  bool status = false;

  void Enable() { enabled_ = true; }
  operator bool() const { return enabled_; }

 private:
  bool enabled_ = false;
};  // struct ReplicationTime

struct Destination {
  std::string bucket_arn;
  AccessControlTranslation access_control_translation;
  std::string account;
  EncryptionConfig encryption_config;
  Metrics metrics;
  ReplicationTime replication_time;
  std::string storage_class;
};  // struct Destination

struct SourceSelectionCriteria {
  Boolean sse_kms_encrypted_objects_status;

  void Enable() { enabled_ = true; }
  operator bool() const { return enabled_; }

 private:
  bool enabled_ = false;
};  // struct SourceSelectionCriteria

struct ReplicationRule {
  Destination destination;
  Boolean delete_marker_replication_status;
  Boolean existing_object_replication_status;
  Filter filter;
  std::string id;
  Prefix prefix;
  Integer priority;
  SourceSelectionCriteria source_selection_criteria;
  Boolean delete_replication_status;
  bool status = false;
};  // struct ReplicationRule

struct ReplicationConfig {
  std::string role;
  std::list<ReplicationRule> rules;

  std::string ToXML();
};  // status ReplicationConfig

struct LifecycleRule {
  Integer abort_incomplete_multipart_upload_days_after_initiation;
  utils::Time expiration_date;
  Integer expiration_days;
  Boolean expiration_expired_object_delete_marker;
  Filter filter;
  std::string id;
  Integer noncurrent_version_expiration_noncurrent_days;
  Integer noncurrent_version_transition_noncurrent_days;
  std::string noncurrent_version_transition_storage_class;
  bool status = false;
  utils::Time transition_date;
  Integer transition_days;
  std::string transition_storage_class;

  error::Error Validate();
};  // struct LifecycleRule

struct LifecycleConfig {
  std::list<LifecycleRule> rules;

  std::string ToXML();
};  // struct LifecycleConfig

struct ObjectLockConfig {
  RetentionMode retention_mode;
  Integer retention_duration_days;
  Integer retention_duration_years;

  error::Error Validate();
};  // struct ObjectLockConfig
}  // namespace s3
}  // namespace minio
#endif  // #ifndef __MINIO_S3_TYPES_H
