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

  // Create list objects arguments.
  minio::s3::ListObjectsArgs args;
  args.bucket = "my-bucket";

  // Call list objects.
  minio::s3::ListObjectsResult result = client.ListObjects(args);
  for (; result; result++) {
    minio::s3::Item item = *result;
    if (item) {
      std::cout << "Name: " << item.name << std::endl;
      std::cout << "Version ID: " << item.version_id << std::endl;
      std::cout << "ETag: " << item.etag << std::endl;
      std::cout << "Size: " << item.size << std::endl;
      std::cout << "Last Modified: " << item.last_modified << std::endl;
      std::cout << "Delete Marker: "
                << minio::utils::BoolToString(item.is_delete_marker)
                << std::endl;
      std::cout << "User Metadata: " << std::endl;
      for (auto& [key, value] : item.user_metadata) {
        std::cout << "  " << key << ": " << value << std::endl;
      }
      std::cout << "Owner ID: " << item.owner_id << std::endl;
      std::cout << "Owner Name: " << item.owner_name << std::endl;
      std::cout << "Storage Class: " << item.storage_class << std::endl;
      std::cout << "Is Latest: " << minio::utils::BoolToString(item.is_latest)
                << std::endl;
      std::cout << "Is Prefix: " << minio::utils::BoolToString(item.is_prefix)
                << std::endl;
      std::cout << "---" << std::endl;
    } else {
      std::cout << "unable to listobjects; " << item.Error().String()
                << std::endl;
      break;
    }
  }

  return 0;
}
