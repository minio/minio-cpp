// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022 MinIO, Inc.
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

#include "client.h"
#include <thread>
#include <iostream>
#include <chrono>
#include <functional>

int count = 0;
bool run_flag = true;

void printUploadSpeed(minio::s3::Client& client_)
{
  while (run_flag) 
  {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::cout << "printUploadSpeed->upload speed: " << ((client_.GetUploadSpeed())/1024) << "KB/s" << std::endl;
  }
}

void PrintUploadProgress(minio::s3::Client& client_)
{
  while (run_flag)
  {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::cout << "PrintUploadProgress->upload progress: " << client_.GetUploadProgress() << "%" << std::endl;
  }
}

int main(int argc, char* argv[]) {
  // Create S3 base URL.
  minio::s3::BaseUrl base_url("play.min.io");

  // Create credential provider.
  minio::creds::StaticProvider provider(
      "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG");

  // Create S3 client.
  minio::s3::Client client(base_url, &provider);

  // Create upload object arguments.
  minio::s3::UploadObjectArgs args;
  args.bucket = "my-bucket";
  args.object = "big-file";
  args.filename = "/path/to/big-file";
  // Call upload object.
  // client.Debug(true);
  
  std::thread t(printUploadSpeed, std::ref(client));
  std::thread t1(PrintUploadProgress, std::ref(client));
  // std::thread t(&print);

  minio::s3::UploadObjectResponse resp = client.UploadObject(args);
  run_flag = false;
  // std::cout << "progress value : " << client.GetUploadProgress() << std::endl;

  // Handle response.
  if (resp) {
    std::cout << "my-file is successfully uploaded to my-file"
              << std::endl;
  } else {
    std::cout << "unable to upload file; " << resp.Error().String()
              << std::endl;
  }

  t.join();
  t1.join();
  return 0;
}
