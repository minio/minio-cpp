> NOTE: This project is a work in progress

# MinIO C++ Client SDK for Amazon S3 Compatible Cloud Storage [![Slack](https://slack.min.io/slack?type=svg)](https://slack.min.io) [![Sourcegraph](https://sourcegraph.com/github.com/minio/minio-cpp/-/badge.svg)](https://sourcegraph.com/github.com/minio/minio-cpp?badge) [![Apache V2 License](https://img.shields.io/badge/license-Apache%20V2-blue.svg)](https://github.com/minio/minio-cpp/blob/master/LICENSE)

The MinIO C++ Client SDK provides simple APIs to access any Amazon S3 compatible object storage.

This quickstart guide will show you how to install the MinIO client SDK, connect to MinIO, and provide a walkthrough for a simple file uploader. For a complete list of APIs and examples, please take a look at the [C++ Client API Reference](https://docs.min.io/docs/cpp-client-api-reference).

This document assumes that you have a working C++ development environment.

> NOTE: This library is based on original work https://github.com/cjameshuff/s3tools, but has been completely re-implemented since then.

## Build Instructions
In order to build this project, you need the Cross-Platform Make CMake 3.10 or higher. You can download it from http://www.cmake.org/. In order to build **miniocpp** you need to have the following libraries and their development headers installed.

- libcurl-dev

  ```
  vcpkg install curl
  ```


- libssl-dev (OpenSSL 1.1.x, preferably)

  ```
  vcpkg install openssl
  ```

- pugixml

  ```
  vcpkg install pugixml
  ```

- doxygen
  - `dnf install doxygen -y` on CentOS 8.x
  - `apt install doxygen -y` on Ubuntu 20.04
  

```
git clone https://github.com/minio/minio-cpp
cd minio-cpp; mkdir build; cd build; cmake ../;
make
```

If building pugixml from source, then in the pugixml directory e.g. `/src/pugixml-1.11`
```
cmake .
make
```

Then you may point PUGIXML to point to custom folders.
```
git clone https://github.com/minio/minio-cpp
cd minio-cpp; git submodule init; git submodule update; mkdir build; cd build;
cmake -DPUGIXML_INCLUDE_DIR=/src/pugixml-1.11/src \
      -DCMAKE_PREFIX_PATH=/src/pugixml-1.11 ../;
make
```

## Example code
```c++
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <getopt.h>
#include <s3.h>

using namespace Minio;

int
main ( int argc, char** argv )
{
  S3Client s3("https://play.min.io:9000", "minioadmin", "minioadmin");
  S3ClientIO io;
  s3.MakeBucket("newbucket", io);
  if(io.Failure()) {
    std::cerr << "ERROR: failed to create bucket" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    return -1;
  }
  return 0;
}
```

## Run an example
Following example runs 'multipart' upload, uploads a single part. You would have to choose a local file to upload for `-f`, and also remote bucket to upload the object to as `-n` and final object name in the bucket as `-k`.

```
export ACTION="multipart"
export ACCESS_KEY=minioadmin
export SECRET_KEY=minioadmin
export ENDPOINT="https://play.min.io:9000"

./examples/s3 -a ${ACTION} -f <local_filename_to_upload> \
              -n <remote_bucket> -k <remote_objectname>
```

Please choose a `<remote_bucket>` that exists.

## License
This SDK is distributed under the [Apache License, Version 2.0](https://www.apache.org/licenses/LICENSE-2.0), see [LICENSE](https://github.com/minio/minio-cpp/blob/master/LICENSE) for more information.
