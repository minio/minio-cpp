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

int main() {
  // Create S3 base URL.
  minio::s3::BaseUrl base_url("play.min.io");

  // Create credential provider.
  minio::creds::StaticProvider provider(
      "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG");

  // Create S3 client.
  minio::s3::Client client(base_url, &provider);

  // Create get bucket policy arguments.
  minio::s3::GetBucketPolicyArgs args;
  args.bucket = "my-bucket";

  // Call get bucket policy.
  minio::s3::GetBucketPolicyResponse resp = client.GetBucketPolicy(args);

  // Handle response.
  if (resp) {
    std::cout << "Bucket policy: " << resp.policy << std::endl;
  } else {
    std::cout << "unable to get bucket policy; " << resp.Error().String()
              << std::endl;
  }

  return 0;
}
