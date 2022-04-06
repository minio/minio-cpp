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

#include "s3_io.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <ostream>
#include <sstream>

#include "s3_headers.h"

using namespace Minio;

void Minio::S3ClientIO::WillStart() {}

void Minio::S3ClientIO::DidFinish() {
  if (printProgress) cout << endl;

  if (Failure()) cerr << "#### ERROR: Operation failed:\n" << *this << endl;
}

size_t Minio::S3ClientIO::Write(char* buf, size_t size, size_t nmemb) {
  if (ostrm) {
    ostrm->write(buf, size * nmemb);
    bytesReceived += size * nmemb;
    if (printProgress) {
      if (bytesToGet == 0)
        cout << "received " << bytesReceived << " bytes, content size unknown";
      else
        cout << "received " << bytesReceived << " bytes, "
             << 100 * bytesReceived / bytesToGet << "%";
      cout << "                        \r";
      cout.flush();
    }
  }
  return size * nmemb;
}

size_t Minio::S3ClientIO::Read(char* buf, size_t size, size_t nmemb) {
  streamsize count = 0;
  if (istrm) {
    istrm->read(buf, size * nmemb);
    count = istrm->gcount();
    bytesSent += count;
    if (printProgress) {
      if (bytesToPut == 0)
        cout << "sent " << bytesSent << " bytes, content size unknown";
      else
        cout << "sent " << bytesSent << " bytes, "
             << 100 * bytesSent / bytesToPut << "%";
      cout << "                        \r";
      cout.flush();
    }
  }
  return count;
}

size_t Minio::S3ClientIO::HandleHeader(char* buf, size_t size, size_t nmemb) {
  size_t length = size * nmemb;
  //        cout << "#### HeaderCB, Header received: " << string(buf, length);
  if (length >= 8 && strncmp(buf, "HTTP/1.1", 8) == 0) {
    result = string(buf + 9, length - 9);
    numResult = strtol(result.c_str(), NULL, 0);
  } else if (length == 2 && strncmp(buf, "\r\n", 2) == 0) {
    // ignore
  } else {
    // Find first occurrence of ':'
    size_t c = 0;
    while (c < length && buf[c] != ':') ++c;

    if (c < length) {
      string header(buf, c);
      string data(buf + c + 2, length - c - 2);
      if (data[data.length() - 1] == '\n') data.erase(data.length() - 1);
      // cout << "#### HeaderCB, parsed header: " << header << endl;
      // cout << "#### HeaderCB, parsed header data: " << data << endl;
      respHeaders.Update(header, data);
    } else {
      cerr << "#### ERROR: HeaderCB, unknown header received: "
           << string(buf, length);
      cerr << "#### length: " << length << endl;
      for (size_t j = 0; j < length; ++j) cerr << (int)buf[j] << " ";
      cerr << endl;
    }
  }
  return length;
}
