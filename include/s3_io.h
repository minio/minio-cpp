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

#ifndef _S3_IO_H
#define _S3_IO_H

#include <cstddef>
#include <cstring>
#include <curlpp/Easy.hpp>
#include <iostream>
#include <ostream>
#include <sstream>

#include "s3_headers.h"

namespace Minio {
typedef cURLpp::Easy S3Connection;

// S3ClientIO objects specify data and headers to send,
// and collect the data and headers of the response.
struct S3ClientIO {
  std::string httpDate;  // Timestamp, set by S3Client::Submit()
  Headers reqHeaders;    // Headers for request

  std::string result;  // Result code for response, minus the leading "HTTP/1.1"
  int numResult;       // Numeric result code for response
  Headers respHeaders;  // Headers from response

  std::ostringstream
      response;  // default output stream, contains body of response
  std::istream* istrm;
  std::ostream* ostrm;

  size_t bytesToGet;  // used only for progress reporting
  size_t bytesReceived;
  size_t bytesToPut;
  size_t bytesSent;

  bool printProgress;
  bool error;

  S3ClientIO() { Reset(); }
  S3ClientIO(std::istream* i) { Reset(i, NULL); }
  S3ClientIO(std::ostream* o) { Reset(NULL, o); }
  S3ClientIO(std::istream* i, std::ostream* o) { Reset(i, o); }

  void Reset(std::istream* i = NULL, std::ostream* o = NULL) {
    reqHeaders.Clear();
    respHeaders.Clear();
    response.clear();
    httpDate = "";
    result = "";
    numResult = 0;
    istrm = (i == NULL) ? NULL : i;
    ostrm = (o == NULL) ? &response : o;
    bytesToGet = 0;
    bytesReceived = 0;
    bytesToPut = 0;
    bytesSent = 0;
    printProgress = false;
    error = false;
  }

  // "200 OK", or some other 20x message
  bool Success() const { return result[0] == '2' && !error; }
  bool Failure() const { return !Success(); }

  // Called prior to performing action
  virtual void WillStart();

  // Called after action is complete
  virtual void DidFinish();

  // Handler for data received by libcurl
  virtual size_t Write(char* buf, size_t size, size_t nmemb);

  // Handler for data requested by libcurl for transmission
  virtual size_t Read(char* buf, size_t size, size_t nmemb);

  // Handler for headers: overrides must call if other functionality of
  // S3ClientIO is to be used.
  virtual size_t HandleHeader(char* buf, size_t size, size_t nmemb);

  friend std::ostream& operator<<(std::ostream& ostrm, S3ClientIO& io);
};
}  // namespace Minio

#endif /* _S3_IO_H */
