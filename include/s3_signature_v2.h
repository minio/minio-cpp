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

#ifndef _S3_SIGNATURE_V2_H
#define _S3_SIGNATURE_V2_H

#include <iostream>
#include <string>
#include <openssl/md5.h>
#include <openssl/buffer.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>

namespace Minio
{
  namespace XML
  {
    // A very minimal XML parser.
    // Extract text enclosed between <tag> and </tag>, starting from crsr
    // position and leaving crsr at the character index following the end tag.
    // Does not handle nested <tag>...</tag> constructs, any nested tags must
    // be of a different type.
    bool ExtractXML(std::string & data, std::string::size_type & crsr,
                    const std::string & tag, const std::string & xml);

    // Same as above, but starts from beginning of xml string. Useful when order of tags is unknown.
    // Scanning from the beginning every time will be inefficient for large strings and will only
    // ever return the first instance of a tag, so use appropriately.
    inline bool ExtractXML(std::string & data, const std::string & tag, const std::string & xml) {
      std::string::size_type crsr = 0;
      return ExtractXML(data, crsr, tag, xml);
    }

    bool ExtractXMLXPath(std::string & data, const std::string & xpath, const std::string & xml);
  }

  namespace SignatureV2
  {
    std::string EncodeB64(uint8_t * data, size_t dataLen);
    size_t ComputeMD5(uint8_t md5[EVP_MAX_MD_SIZE], std::istream & istrm);
    std::string ComputeMD5(std::istream & istrm);
    std::string GenerateSignature(const std::string & secret, const std::string & stringToSign);
    std::string HTTP_Date();
  }
}

#endif /* _S3_SIGNATURE_V2_H */
