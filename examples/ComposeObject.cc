// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2024 MinIO, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a compose of the License at
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

  // Create compose object arguments.
  minio::s3::ComposeObjectArgs args;
  args.bucket = "my-bucket";
  args.object = "my-object";

  std::list<minio::s3::ComposeSource> sources;

  minio::s3::ComposeSource source1;
  source1.bucket = "my-src-bucket1";
  source1.object = "my-src-object1";
  sources.push_back(source1);

  minio::s3::ComposeSource source2;
  source2.bucket = "my-src-bucket2";
  source2.object = "my-src-object2";
  sources.push_back(source2);

  args.sources = sources;

  // Call compose object.
  minio::s3::ComposeObjectResponse resp = client.ComposeObject(args);

  // Handle response.
  if (resp) {
    std::cout << "my-object is successfully created" << std::endl;
  } else {
    std::cout << "unable to compose object; " << resp.Error().String()
              << std::endl;
  }

  return 0;
}
