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

  // Create set object lock config arguments.
  minio::s3::SetObjectLockConfigArgs args;
  args.bucket = "my-bucket";
  args.config.retention_mode = minio::s3::RetentionMode::kGovernance;
  args.config.retention_duration_days = minio::s3::Integer(30);

  // Call set object lock config.
  minio::s3::SetObjectLockConfigResponse resp =
      client.SetObjectLockConfig(args);

  // Handle response.
  if (resp) {
    std::cout << "Object lock configuration is set successfully" << std::endl;
  } else {
    std::cout << "unable to do object lock configuration; "
              << resp.Error().String() << std::endl;
  }

  return 0;
}
