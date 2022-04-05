// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2021 MinIO, Inc.
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

#include "s3_http.h"

std::string Minio::Http::methodToString(Minio::Http::Method method) {
  switch (method) {
    case Minio::Http::Method::HTTP_GET:
      return "GET";
    case Minio::Http::Method::HTTP_PUT:
      return "PUT";
    case Minio::Http::Method::HTTP_POST:
      return "POST";
    case Minio::Http::Method::HTTP_HEAD:
      return "HEAD";
    case Minio::Http::Method::HTTP_DELETE:
      return "DELETE";
    case Minio::Http::Method::HTTP_PATCH:
      return "PATCH";
  }
  return "GET";
}
