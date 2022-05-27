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
