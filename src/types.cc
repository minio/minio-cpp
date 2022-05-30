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

std::string minio::s3::SelectRequest::ToXML() {
  std::stringstream ss;
  ss << "<SelectObjectContentRequest>";

  ss << "<Expression>" << expr << "</Expression>";
  ss << "<ExpressionType>SQL</ExpressionType>";

  ss << "<InputSerialization>";

  if (csv_input != NULL) {
    if (csv_input->compression_type != NULL) {
      ss << "<CompressionType>"
         << CompressionTypeToString(*csv_input->compression_type)
         << "</CompressionType>";
    }

    ss << "<CSV>";
    if (csv_input->allow_quoted_record_delimiter) {
      ss << "<AllowQuotedRecordDelimiter>true</AllowQuotedRecordDelimiter>";
    }
    if (csv_input->comments) {
      ss << "<Comments>" << csv_input->comments << "</Comments>";
    }
    if (csv_input->field_delimiter) {
      ss << "<FieldDelimiter>" << csv_input->field_delimiter
         << "</FieldDelimiter>";
    }
    if (csv_input->file_header_info != NULL) {
      ss << "<FileHeaderInfo>"
         << FileHeaderInfoToString(*csv_input->file_header_info)
         << "</FileHeaderInfo>";
    }
    if (csv_input->quote_character) {
      ss << "<QuoteCharacter>" << csv_input->quote_character
         << "</QuoteCharacter>";
    }
    if (csv_input->record_delimiter) {
      ss << "<RecordDelimiter>" << csv_input->record_delimiter
         << "</RecordDelimiter>";
    }
    ss << "</CSV>";
  }

  if (json_input != NULL) {
    if (json_input->compression_type != NULL) {
      ss << "<CompressionType>"
         << CompressionTypeToString(*json_input->compression_type)
         << "</CompressionType>";
    }

    ss << "<JSON>";
    if (json_input->json_type != NULL) {
      ss << "<Type>" << JsonTypeToString(*json_input->json_type) << "</Type>";
    }
    ss << "</JSON>";
  }

  if (parquet_input != NULL) ss << "<Parquet></Parquet>";

  ss << "</InputSerialization>";

  ss << "<OutputSerialization>";

  if (csv_output != NULL) {
    ss << "<CSV>";
    if (csv_output->field_delimiter) {
      ss << "<FieldDelimiter>" << csv_output->field_delimiter
         << "</FieldDelimiter>";
    }
    if (csv_output->quote_character) {
      ss << "<QuoteCharacter>" << csv_output->quote_character
         << "</QuoteCharacter>";
    }
    if (csv_output->quote_escape_character) {
      ss << "<QuoteEscapeCharacter>" << csv_output->quote_escape_character
         << "</QuoteEscapeCharacter>";
    }
    if (csv_output->quote_fields != NULL) {
      ss << "<QuoteFields>" << QuoteFieldsToString(*csv_output->quote_fields)
         << "</QuoteFields>";
    }
    if (csv_output->record_delimiter) {
      ss << "<RecordDelimiter>" << csv_output->record_delimiter
         << "</RecordDelimiter>";
    }
    ss << "</CSV>";
  }

  if (json_output != NULL) {
    ss << "<JSON>";
    if (json_output->record_delimiter) {
      ss << "<RecordDelimiter>" << json_output->record_delimiter
         << "</RecordDelimiter>";
    }
    ss << "</JSON>";
  }

  ss << "</OutputSerialization>";

  if (request_progress) {
    ss << "<RequestProgress><Enabled>true</Enabled></RequestProgress>";
  }
  if (scan_start_range != NULL || scan_end_range != NULL) {
    ss << "<ScanRange>";
    if (scan_start_range != NULL) {
      ss << "<Start>" << *scan_start_range << "</Start>";
    }
    if (scan_end_range != NULL) {
      ss << "<End>" << *scan_end_range << "</End>";
    }
    ss << "</ScanRange>";
  }

  ss << "</SelectObjectContentRequest>";

  return ss.str();
}

minio::s3::NotificationRecord minio::s3::NotificationRecord::ParseJSON(
    nlohmann::json j_record) {
  minio::s3::NotificationRecord record;

  record.event_version = j_record.value("eventVersion", "");
  record.event_source = j_record.value("eventSource", "");
  record.aws_region = j_record.value("awsRegion", "");
  record.event_time = j_record.value("eventTime", "");
  record.event_name = j_record.value("eventName", "");
  if (j_record.contains("userIdentity")) {
    record.user_identity.principal_id =
        j_record["userIdentity"].value("principalId", "");
  }
  if (j_record.contains("requestParameters")) {
    auto& j = j_record["requestParameters"];
    record.request_parameters.principal_id = j.value("principalId", "");
    record.request_parameters.region = j.value("region", "");
    record.request_parameters.source_ip_address =
        j.value("sourceIPAddress", "");
  }
  if (j_record.contains("responseElements")) {
    auto& j = j_record["responseElements"];
    record.response_elements.content_length = j.value("content-length", "");
    record.response_elements.x_amz_request_id = j.value("x-amz-request-id", "");
    record.response_elements.x_minio_deployment_id =
        j.value("x-minio-deployment-id", "");
    record.response_elements.x_minio_origin_endpoint =
        j.value("x-minio-origin-endpoint", "");
  }
  if (j_record.contains("s3")) {
    auto& j_s3 = j_record["s3"];
    record.s3.s3_schema_version = j_s3.value("s3SchemaVersion", "");
    record.s3.configuration_id = j_s3.value("configurationId", "");
    if (j_s3.contains("bucket")) {
      auto& j_bucket = j_s3["bucket"];
      record.s3.bucket.name = j_bucket.value("name", "");
      record.s3.bucket.arn = j_bucket.value("arn", "");
      if (j_bucket.contains("ownerIdentity")) {
        record.s3.bucket.owner_identity.principal_id =
            j_bucket["ownerIdentity"].value("principalId", "");
      }
    }
    if (j_s3.contains("object")) {
      auto& j_object = j_s3["object"];
      record.s3.object.key = j_object.value("key", "");
      record.s3.object.size = j_object.value("size", 0);
      record.s3.object.etag = j_object.value("eTag", "");
      record.s3.object.content_type = j_object.value("contentType", "");
      record.s3.object.sequencer = j_object.value("sequencer", "");
      if (j_object.contains("userMetadata")) {
        for (auto& j : j_object["userMetadata"].items()) {
          record.s3.object.user_metadata[j.key()] = j.value();
        }
      }
    }
  }
  if (j_record.contains("source")) {
    auto& j_source = j_record["source"];
    record.source.host = j_source.value("host", "");
    record.source.port = j_source.value("port", "");
    record.source.user_agent = j_source.value("userAgent", "");
  }

  return record;
}
