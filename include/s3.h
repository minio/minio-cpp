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

#ifndef _S3_H
#define _S3_H

#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "s3_headers.h"
#include "s3_io.h"
#include "s3_types.h"

using namespace Minio;

class S3Client {
 private:
  std::string endpoint, keyID, secret;
  int verbosity;
  std::list<Minio::S3::Bucket> buckets;

  std::string SignV2Request(const Minio::S3ClientIO& io, const std::string& uri,
                            const std::string& mthd);

  void Submit(const std::string& url, const std::string& uri,
              Http::Method method, Minio::S3ClientIO& io, S3Connection** conn);

  static std::string ParseCreateMultipartUpload(const std::string& xml);
  static void ParseBucketsList(std::list<Minio::S3::Bucket>& buckets,
                               const std::string& xml);
  static void ParseObjectsList(std::list<Minio::S3::Object>& objects,
                               const std::string& xml);

 public:
  S3Client(const std::string& endpoint, const std::string& kid,
           const std::string& sk);
  ~S3Client();

  void SetVerbosity(int v) { verbosity = v; }

  void ListObjects(Minio::S3::Bucket& bucket, S3Connection** conn = NULL);

  // Upload from IO stream.
  void PutObject(const std::string& bkt, const std::string& key,
                 Minio::S3ClientIO& io, S3Connection** reqPtr = NULL);

  // Upload from local path.
  void PutObject(const std::string& bkt, const std::string& key,
                 const std::string& localpath, Minio::S3ClientIO& io,
                 S3Connection** reqPtr = NULL);

  // Get object data (GET /key) with specific partNumber.
  void GetObject(const std::string& bkt, const std::string& key,
                 const int& part_number, Minio::S3ClientIO& io,
                 S3Connection** reqPtr = NULL);

  // Get object data fully
  void GetObject(const std::string& bkt, const std::string& key,
                 Minio::S3ClientIO& io, S3Connection** reqPtr = NULL);

  // Get meta-data on object (HEAD)
  // Headers are same as for GetObject(), but no data is retrieved.
  void StatObject(const std::string& bkt, const std::string& key,
                  Minio::S3ClientIO& io, S3Connection** reqPtr = NULL);

  // Delete object (DELETE)
  void DeleteObject(const std::string& bkt, const std::string& key,
                    Minio::S3ClientIO& io, S3Connection** reqPtr = NULL);

  // Copy object (COPY)
  void CopyObject(const std::string& srcbkt, const std::string& srckey,
                  const std::string& dstbkt, const std::string& dstkey,
                  bool copyMD, Minio::S3ClientIO& io,
                  S3Connection** reqPtr = NULL);

  // List buckets (s3.amazonaws.com GET /)
  void ListBuckets(Minio::S3ClientIO& io, S3Connection** reqPtr = NULL);

  // Make bucket (bucket.s3.amazonaws.com PUT /)
  void MakeBucket(const std::string& bkt, Minio::S3ClientIO& io,
                  S3Connection** reqPtr = NULL);

  // List objects (bucket.s3.amazonaws.com GET /)
  void ListObjects(const std::string& bkt, Minio::S3ClientIO& io,
                   S3Connection** reqPtr = NULL);

  // Remove bucket (bucket.s3.amazonaws.com DELETE /)
  void RemoveBucket(const std::string& bkt, Minio::S3ClientIO& io,
                    S3Connection** reqPtr = NULL);

  // Multipart APIs
  // Upload from io stream to a specific part number for multipart upload_id.
  Minio::S3::CompletePart PutObject(const std::string& bkt,
                                    const std::string& key,
                                    const int& part_number,
                                    const std::string& upload_id,
                                    Minio::S3ClientIO& io,
                                    S3Connection** reqPtr = NULL);

  std::string CreateMultipartUpload(const std::string& bkt,
                                    const std::string& key,
                                    Minio::S3ClientIO& io,
                                    S3Connection** reqPtr = NULL);

  void AbortMultipartUpload(const std::string& bkt, const std::string& key,
                            const std::string& upload_id,
                            S3Connection** reqPtr = NULL);

  void CompleteMultipartUpload(const std::string& bkt, const std::string& key,
                               const std::string& upload_id,
                               const std::list<Minio::S3::CompletePart>& parts,
                               Minio::S3ClientIO& io,
                               S3Connection** reqPtr = NULL);
};

#endif /* _S3_H */
