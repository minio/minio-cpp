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

  // Create remove object arguments.
  minio::s3::RemoveObjectsArgs args;
  args.bucket = "my-bucket";

  std::list<minio::s3::DeleteObject> objects;
  objects.push_back(minio::s3::DeleteObject{"my-object1"});
  objects.push_back(minio::s3::DeleteObject{"my-object2"});
  objects.push_back(minio::s3::DeleteObject{"my-object3"});
  std::list<minio::s3::DeleteObject>::iterator i = objects.begin();

  args.func = [&objects = objects,
               &i = i](minio::s3::DeleteObject& obj) -> bool {
    if (i == objects.end()) return false;
    obj = *i;
    i++;
    return true;
  };

  // Call remove objects.
  minio::s3::RemoveObjectsResult result = client.RemoveObjects(args);
  for (; result; result++) {
    minio::s3::DeleteError err = *result;
    if (!err) {
      std::cout << "unable to do remove objects; " << err.Error().String()
                << std::endl;
      break;
    }

    std::cout << "unable to remove object " << err.object_name;
    if (!err.version_id.empty()) {
      std::cout << " of version ID " << err.version_id;
    }
    std::cout << std::endl;
  }

  return 0;
}
