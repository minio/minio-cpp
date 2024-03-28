// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2023 MinIO, Inc.
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

int main() {
  // Create S3 base URL.
  minio::s3::BaseUrl base_url("play.min.io");

  // Create credential provider.
  minio::creds::StaticProvider provider(
      "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG");

  // Create S3 client.
  minio::s3::Client client(base_url, &provider);

  // Create get object arguments.
  minio::s3::GetObjectArgs args;
  args.bucket = "my-bucket";
  args.object = "my-object";
  args.datafunc = [](minio::http::DataFunctionArgs args) -> bool {
    std::cout << "received data: " << args.datachunk.length() << " bytes"
              << std::endl;
    return true;
  };
  args.progressfunc = [](minio::http::ProgressFunctionArgs args) -> bool {
    if (args.download_speed > 0) {
      std::cout << "downloaded speed: " << (long)args.download_speed << " bps"
                << std::endl;
    } else {
      std::cout << "downloaded: " << (long)args.downloaded_bytes << " bytes of "
                << (long)args.download_total_bytes << " bytes" << std::endl;
    }
    return true;
  };

  // Call get object.
  minio::s3::GetObjectResponse resp = client.GetObject(args);

  // Handle response.
  if (resp) {
    std::cout << std::endl
              << "data of my-object is received successfully" << std::endl;
  } else {
    std::cout << "unable to get object; " << resp.Error().String() << std::endl;
  }

  return 0;
}
