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

#ifndef _S3_TYPES_H
#define _S3_TYPES_H

#include <iostream>
#include <list>

namespace Minio
{
  namespace S3
  {
    // Instances of this class represent objects stored on Amazon S3.
    struct Object {
      std::string key;
      std::string lastModified;
      std::string eTag;
      std::string size;
    
      std::string ownerID;
      std::string ownerDisplayName;
    
      std::string storageClass;
    
      Object() {}
      size_t GetSize() const {return strtol(size.c_str(), NULL, 0);}
    };

    struct Bucket {
      std::string name;
      std::string creationDate;

      std::list<Object> objects;
          
      Bucket(const std::string & nm, const std::string & dt): name(nm), creationDate(dt) {}
    };

    struct CompletePart {
      std::string eTag;
      int partNumber;
    };
  }
}

#endif /* _S3_TYPES_H */
