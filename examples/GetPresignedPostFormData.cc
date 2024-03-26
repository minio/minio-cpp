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

  // Create get presigned post form data arguments.
  minio::utils::UtcTime expiration = minio::utils::UtcTime::Now();
  expiration.Add(60 * 60 * 24);  // 1 day from now.
  minio::s3::PostPolicy policy("my-bucket", expiration);
  policy.AddStartsWithCondition("key", "my/object/prefix/");
  policy.AddContentLengthRangeCondition(1 * 1024 * 1024, 10 * 1024 * 1024);

  // Call get presigned post form data.
  minio::s3::GetPresignedPostFormDataResponse resp =
      client.GetPresignedPostFormData(policy);

  // Handle response.
  if (resp) {
    std::string fields;
    for (auto& [key, value] : resp.form_data) fields += key + "=" + value + " ";
    fields += "-F file=@<FILE>";
    std::cout << "Example CURL command to use form-data:" << std::endl
              << "curl -X POST https://play.min.io/my-bucket " << fields
              << std::endl;
  } else {
    std::cout << "unable to get presigned post form data; "
              << resp.Error().String() << std::endl;
  }

  return 0;
}
