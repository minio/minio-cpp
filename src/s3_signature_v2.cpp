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

#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <cmath>

#include "s3_signature_v2.h"

using namespace std;
using namespace Minio;

// Encode binary data in ASCII form using base 64
std::string Minio::SignatureV2::EncodeB64(uint8_t * data, size_t dataLen)
{
  // http://www.ioncannon.net/programming/34/howto-base64-encode-with-cc-and-openssl/
  BIO * b64 = BIO_new(BIO_f_base64());
  BIO * bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_write(b64, data, dataLen);
  BIO_ctrl(b64, BIO_CTRL_FLUSH, 0, NULL);//BIO_flush(b64);

  BUF_MEM * bptr;
  BIO_get_mem_ptr(b64, &bptr);

  std::string b64string(bptr->data, bptr->length-1);
  //    cout << "b64string: \"" << b64string << "\"" << endl;
  BIO_free_all(b64);
  return b64string;
}

// Compute a MD5 checksum of a given data stream as a binary string
// openssl dgst -md5 -binary FILE | openssl enc -base64
const std::streamsize kMD5_ChunkSize = 16384;
const char * hexchars = "0123456789abcdef";
size_t Minio::SignatureV2::ComputeMD5(uint8_t md5[EVP_MAX_MD_SIZE], std::istream & istrm)
{
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_md5(), NULL);

  uint8_t * buf = new uint8_t[kMD5_ChunkSize];
  while(istrm) {
    istrm.read((char*)buf, kMD5_ChunkSize);
    std::streamsize count = istrm.gcount();
    EVP_DigestUpdate(ctx, buf, count);
  }
  delete[] buf;

  unsigned int mdLen;
  EVP_DigestFinal_ex(ctx, md5, &mdLen);
  EVP_MD_CTX_free(ctx);
  return mdLen;
}

// Compute a MD5 checksum of a given data stream as a hex-encoded ASCII string
std::string Minio::SignatureV2::ComputeMD5(std::istream & istrm)
{
  uint8_t md5[EVP_MAX_MD_SIZE];
  size_t mdLen = Minio::SignatureV2::ComputeMD5(md5, istrm);
  std::ostringstream md5strm;
  for(size_t j = 0; j < mdLen; ++j)
    md5strm << hexchars[(md5[j] >> 4) & 0x0F] << hexchars[md5[j] & 0x0F];

  return md5strm.str();
}

// Generate a signature from a message and secret.
std::string Minio::SignatureV2::GenerateSignature(const std::string & secret, const std::string & stringToSign)
{
  HMAC_CTX *ctx = HMAC_CTX_new();
  uint8_t md[EVP_MAX_MD_SIZE];
  unsigned int mdLength = 0;
  HMAC_Init_ex(ctx, secret.c_str(), secret.length(), EVP_sha1(), NULL);
  HMAC_Update(ctx, (uint8_t*)stringToSign.c_str(), stringToSign.length());
  HMAC_Final(ctx, md, &mdLength);
  HMAC_CTX_free(ctx);
  return Minio::SignatureV2::EncodeB64(md, mdLength);
}

bool Minio::XML::ExtractXML(std::string & data, std::string::size_type & crsr, const std::string & tag, const std::string & xml)
{
  std::string startTag = std::string("<") + tag + ">";
  std::string endTag = std::string("</") + tag + ">";
  crsr = xml.find(startTag, crsr);
  if(crsr != std::string::npos) {
    crsr += startTag.size();
    std::string::size_type crsr2 = xml.find(endTag, crsr);
    data = std::string(xml, crsr, crsr2 - crsr);
    crsr = crsr2 + endTag.size();
    return true;
  }
  data = "";
  return false;
}

std::string Minio::SignatureV2::HTTP_Date()
{
  time_t t = time(NULL);
  tm gmt;
  gmtime_r(&t, &gmt);
  char bfr[256];
  size_t n = strftime(bfr, 256, "%a, %d %b %Y %H:%M:%S GMT", &gmt);
  bfr[n] = '\0';
  return bfr;
}
