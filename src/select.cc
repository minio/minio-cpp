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

#include "select.h"

void minio::s3::SelectHandler::Reset() {
  prelude_ = "";
  prelude_read_ = false;

  prelude_crc_ = "";
  prelude_crc_read_ = false;

  data_ = "";
  data_read_ = false;

  message_crc_ = "";
  message_crc_read_ = false;
}

bool minio::s3::SelectHandler::ReadPrelude() {
  if (response_.length() < 8) return false;

  prelude_read_ = true;
  prelude_ = response_.substr(0, 8);
  response_.erase(0, 8);

  return true;
}

bool minio::s3::SelectHandler::ReadPreludeCrc() {
  if (response_.length() < 4) return false;

  prelude_crc_read_ = true;
  prelude_crc_ = response_.substr(0, 4);
  response_.erase(0, 4);

  return true;
}

bool minio::s3::SelectHandler::ReadData() {
  size_t data_length = total_length_ - 8 - 4 - 4;
  if (response_.length() < data_length) return false;

  data_read_ = true;
  data_ = response_.substr(0, data_length);
  response_.erase(0, data_length);

  return true;
}

bool minio::s3::SelectHandler::ReadMessageCrc() {
  if (response_.length() < 4) return false;

  message_crc_read_ = true;
  message_crc_ = response_.substr(0, 4);
  response_.erase(0, 4);

  return true;
}

minio::error::Error minio::s3::SelectHandler::DecodeHeader(
    std::map<std::string, std::string>& headers, std::string data) {
  while (true) {
    size_t length = data[0];
    data.erase(0, 1);
    if (!length) break;

    std::string name = data.substr(0, length);
    data.erase(0, length);

    if (data[0] != 7) return error::Error("header value type is not 7");
    data.erase(0, 1);

    length = data[0] << 8 | data[1];
    data.erase(0, 2);

    std::string value = data.substr(0, length);
    data.erase(0, length);

    headers[name] = value;
  }

  return error::SUCCESS;
}

bool minio::s3::SelectHandler::process(http::DataFunctionArgs args,
                                       bool& cont) {
  if (!prelude_read_ && !ReadPrelude()) return true;

  if (!prelude_crc_read_) {
    if (!ReadPreludeCrc()) return true;

    unsigned long got = utils::CRC32(prelude_);
    unsigned long expected = utils::Int(prelude_crc_);
    if (got != expected) {
      done_ = true;
      std::string msg("prelude CRC mismatch; expected: ");
      msg += std::to_string(expected) + ", got: " + std::to_string(got);
      result_func_(SelectResult(error::Error(msg)));
      return false;
    }
    total_length_ = utils::Int(prelude_.substr(0, 4));
  }

  if (!data_read_ && !ReadData()) return true;

  if (!message_crc_read_) {
    if (!ReadMessageCrc()) return true;

    std::string message = prelude_ + prelude_crc_ + data_;
    unsigned long got = utils::CRC32(message);
    unsigned long expected = utils::Int(message_crc_);
    if (got != expected) {
      done_ = true;
      std::string msg("message CRC mismatch; expected: ");
      msg += std::to_string(expected) + ", got: " + std::to_string(got);
      result_func_(SelectResult(error::Error(msg)));
      return false;
    }
  }

  size_t header_length = utils::Int(prelude_.substr(4));
  std::string headerdata = data_.substr(0, header_length);
  data_.erase(0, header_length);
  std::map<std::string, std::string> headers;
  if (error::Error err = DecodeHeader(headers, headerdata)) {
    done_ = true;
    result_func_(SelectResult(err));
    return false;
  }

  if (headers[":message-type"] == "error") {
    done_ = true;
    result_func_(SelectResult(error::Error(headers[":error-code"] + ": " +
                                           headers[":error-message"])));
    return false;
  }

  if (headers[":event-type"] == "End") {
    done_ = true;
    result_func_(SelectResult());
    return false;
  }

  long payload_length = total_length_ - header_length - 16;
  if (headers[":event-type"] == "Cont" || payload_length < 1) {
    Reset();
    return true;
  }

  std::string payload = data_.substr(0, payload_length);
  if (headers[":event-type"] == "Progress" ||
      headers[":event-type"] == "Stats") {
    pugi::xml_document xdoc;
    pugi::xml_parse_result result = xdoc.load_string(payload.data());
    if (!result) {
      done_ = true;
      result_func_(
          SelectResult(error::Error("unable to parse XML; " + payload)));
      return false;
    }

    std::string xpath;
    xpath = "/" + headers[":event-type"];
    auto root = xdoc.select_node(xpath.c_str());
    pugi::xpath_node text;
    std::string value;
    long int bytes_scanned = -1;
    long int bytes_processed = -1;
    long int bytes_returned = -1;

    text = root.node().select_node("BytesScanned/text()");
    value = text.node().value();
    if (!value.empty()) bytes_scanned = std::stoi(value);

    text = root.node().select_node("BytesProcessed/text()");
    value = text.node().value();
    if (!value.empty()) bytes_processed = std::stoi(value);

    text = root.node().select_node("BytesReturned/text()");
    value = text.node().value();
    if (!value.empty()) bytes_returned = std::stoi(value);

    cont = result_func_(
        SelectResult(bytes_scanned, bytes_processed, bytes_returned));
    Reset();
    done_ = !cont;
    return cont;
  }

  if (headers[":event-type"] == "Records") {
    cont = result_func_(SelectResult(payload));
    Reset();
    done_ = !cont;
    return cont;
  }

  done_ = true;
  result_func_(SelectResult(
      error::Error("unknown event-type " + headers[":event-type"])));
  return false;
}

bool minio::s3::SelectHandler::DataFunction(http::DataFunctionArgs args) {
  if (done_) return false;

  response_ += args.datachunk;

  while (true) {
    bool cont = false;
    if (!process(args, cont)) return false;
    if (!cont) return true;
  }
}
