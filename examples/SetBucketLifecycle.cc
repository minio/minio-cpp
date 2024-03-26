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

  // Create set bucket lifecycle arguments.
  minio::s3::LifecycleConfig config;
  {
    minio::s3::LifecycleRule rule;
    rule.id = "rule1";
    rule.status = true;
    rule.transition_days = minio::s3::Integer(30);
    rule.transition_storage_class = "GLACIER";
    rule.filter.prefix = minio::s3::Prefix("documents/");
    config.rules.push_back(rule);
  }
  {
    minio::s3::LifecycleRule rule;
    rule.id = "rule2";
    rule.status = true;
    rule.expiration_days = minio::s3::Integer(365);
    rule.filter.prefix = minio::s3::Prefix("logs/");
    config.rules.push_back(rule);
  }

  minio::s3::SetBucketLifecycleArgs args(config);
  args.bucket = "my-bucket";

  // Call set bucket lifecycle.
  minio::s3::SetBucketLifecycleResponse resp = client.SetBucketLifecycle(args);

  // Handle response.
  if (resp) {
    std::cout << "Bucket lifecycle is set successfully" << std::endl;
  } else {
    std::cout << "unable to set bucket lifecycle; " << resp.Error().String()
              << std::endl;
  }

  return 0;
}
