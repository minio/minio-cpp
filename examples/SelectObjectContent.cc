// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2024 MinIO, Inc.
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
//
// SPDX-License-Identifier: Apache-2.0

#include <miniocpp/client.h>
#include <miniocpp/select.h>

int main() {
  // Create S3 base URL.
  minio::s3::BaseUrl base_url("play.min.io");

  // Create credential provider.
  minio::creds::StaticProvider provider(
      "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG");

  // Create S3 client.
  minio::s3::Client client(base_url, &provider);

  std::string expression = "select * from S3Object";
  minio::s3::CsvInputSerialization csv_input;
  minio::s3::FileHeaderInfo file_header_info = minio::s3::FileHeaderInfo::kUse;
  csv_input.file_header_info = &file_header_info;
  minio::s3::CsvOutputSerialization csv_output;
  minio::s3::QuoteFields quote_fields = minio::s3::QuoteFields::kAsNeeded;
  csv_output.quote_fields = &quote_fields;
  minio::s3::SelectRequest request(expression, &csv_input, &csv_output);

  std::string records;
  auto func = [&records = records](minio::s3::SelectResult result) -> bool {
    if (result.err) {
      std::cout << "error occurred; " << result.err.String() << std::endl;
      return false;
    }
    records += result.records;
    return true;
  };

  minio::s3::SelectObjectContentArgs args(request, func);
  args.bucket = "my-bucket";
  args.object = "my-object.csv";
  minio::s3::SelectObjectContentResponse resp =
      client.SelectObjectContent(args);
  if (resp) {
    std::cout << "records retrieved" << std::endl;
    std::cout << records << std::endl;
  } else {
    std::cout << "unable to do select object content; " << resp.Error().String()
              << std::endl;
  }

  return 0;
}
