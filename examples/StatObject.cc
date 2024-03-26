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

  // Create stat object arguments.
  minio::s3::StatObjectArgs args;
  args.bucket = "my-bucket";
  args.object = "my-object";

  // Call stat object.
  minio::s3::StatObjectResponse resp = client.StatObject(args);

  // Handle response.
  if (resp) {
    std::cout << "Version ID: " << resp.version_id << std::endl;
    std::cout << "ETag: " << resp.etag << std::endl;
    std::cout << "Size: " << resp.size << std::endl;
    std::cout << "Last Modified: " << resp.last_modified << std::endl;
    std::cout << "Retention Mode: ";
    if (minio::s3::IsRetentionModeValid(resp.retention_mode)) {
      std::cout << minio::s3::RetentionModeToString(resp.retention_mode)
                << std::endl;
    } else {
      std::cout << "-" << std::endl;
    }
    std::cout << "Retention Retain Until Date: ";
    if (resp.retention_retain_until_date) {
      std::cout << resp.retention_retain_until_date.ToHttpHeaderValue()
                << std::endl;
    } else {
      std::cout << "-" << std::endl;
    }
    std::cout << "Legal Hold: ";
    if (minio::s3::IsLegalHoldValid(resp.legal_hold)) {
      std::cout << minio::s3::LegalHoldToString(resp.legal_hold) << std::endl;
    } else {
      std::cout << "-" << std::endl;
    }
    std::cout << "Delete Marker: "
              << minio::utils::BoolToString(resp.delete_marker) << std::endl;
    std::cout << "User Metadata: " << std::endl;
    std::list<std::string> keys = resp.user_metadata.Keys();
    for (auto& key : keys) {
      std::cout << "  " << key << ": " << resp.user_metadata.GetFront(key)
                << std::endl;
    }
  } else {
    std::cout << "unable to get stat object; " << resp.Error().String()
              << std::endl;
  }

  return 0;
}
