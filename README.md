# MinIO C++ Client SDK for Amazon S3 Compatible Cloud Storage [![Slack](https://slack.min.io/slack?type=svg)](https://slack.min.io) [![Sourcegraph](https://sourcegraph.com/github.com/minio/minio-cpp/-/badge.svg)](https://sourcegraph.com/github.com/minio/minio-cpp?badge) [![Apache V2 License](https://img.shields.io/badge/license-Apache%20V2-blue.svg)](https://github.com/minio/minio-cpp/blob/master/LICENSE)

MinIO C++ SDK is Simple Storage Service (aka S3) client to perform bucket and object operations to any Amazon S3 compatible object storage service.

For a complete list of APIs and examples, please take a look at the [MinIO C++ Client API Reference](https://minio-cpp.min.io/)

## Build Requirements

* [cmake](https://cmake.org/) 3.13.4 or higher.
* [vcpkg](https://vcpkg.io/en/index.html) package manager.
* A working C++ compiler that supports at least C++17.

## Installation via `vcpkg`

MinIO C++ client SDK can be installed via `vcpkg` package manager:

```bash
$ vcpkg install minio-cpp
```

Typically `minio-cpp` will be part of dependencies specified in `vcpkg.json` file:

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "your-project",
  "version": "0.0.1",
  "dependencies": [
    { "name": "minio-cpp" }
  ]
}
```

## Prebuilt release bundles

Don't want to (or can't) use `vcpkg` — for example because of network
restrictions? Every [GitHub release](https://github.com/minio/minio-cpp/releases)
ships self-contained, prebuilt SDK bundles for the most common platforms,
including arm64:

| Platform                    | Bundle                                              |
| --------------------------- | --------------------------------------------------- |
| Linux x86_64                | `minio-cpp-<version>-linux-amd64.tar.gz`            |
| Linux arm64 (Graviton, RPi) | `minio-cpp-<version>-linux-arm64.tar.gz`            |
| macOS x86_64                | `minio-cpp-<version>-macos-amd64.tar.gz`            |
| macOS arm64 (Apple silicon) | `minio-cpp-<version>-macos-arm64.tar.gz`            |
| Windows x86_64              | `minio-cpp-<version>-windows-amd64.zip`             |

Each bundle is a single relocatable prefix containing everything needed to
build and link against the SDK: the `miniocpp` headers, static libraries
(`miniocpp` and all of its dependencies), CMake package configs and the
`miniocpp.pc` file. Download a bundle, extract it, and point CMake at it:

```bash
$ curl -LO https://github.com/minio/minio-cpp/releases/download/<version>/minio-cpp-<version>-linux-arm64.tar.gz
$ tar -xzf minio-cpp-<version>-linux-arm64.tar.gz
$ cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/minio-cpp-<version>-linux-arm64"
```

No `vcpkg`, no network and no extra dependency installation is required —
`find_package(miniocpp REQUIRED)` in your `CMakeLists.txt` resolves the
bundle exactly like the section below. The bundles are built automatically
from every pushed `v*` tag (and can also be built on demand from the
"Actions" tab); the release workflow lives in
[`.github/workflows/release.yml`](.github/workflows/release.yml).

## Using `minio-cpp` with cmake

MinIO C++ cliend SDK can be consumed as a dependency in CMakeLists.txt, the following can be used as an example:

```cmake
cmake_minimum_required(VERSION 3.13.4)

project(miniocpp_example LANGUAGES C CXX)

# This will try to find miniocpp package and all its dependencies.
find_package(miniocpp REQUIRED)

# Create an executable called miniocpp-example:
add_executable(miniocpp-example example.cpp)

# Link the executable to miniocpp and all its dependencies:
target_link_libraries(miniocpp-example PRIVATE miniocpp::miniocpp)

# Make sure you are using at least C++17:
target_compile_features(miniocpp-example PUBLIC cxx_std_17)
```

Note that `miniocpp::miniocpp` is a cmake imported target, which contains all the instructions necessary to use `minio-cpp` library from your cmake projet file.

## Hacking minio-cpp

In order to run minio-cpp tests and examples, you can do the following assuming `VCPKG_ROOT` points to a valid `vcpkg` installation:

```bash
$ git clone https://github.com/minio/minio-cpp
$ cd minio-cpp
$ ${VCPKG_ROOT}/vcpkg install
$ cmake . -B build/Debug -DCMAKE_BUILD_TYPE=Debug -DMINIO_CPP_TEST=ON -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
$ cmake --build ./build/Debug
```

Note that cmake also supports multi-configuration generators. Multi-configuration generators don't use `CMAKE_BUILD_TYPE` during configure time. For example a Visual Studio project can be setup the following way:

```bash
$ git clone https://github.com/minio/minio-cpp
$ cd minio-cpp
$ ${VCPKG_ROOT}/vcpkg install
$ cmake . -B build -DMINIO_CPP_TEST=ON -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
$ cmake --build ./build --config Debug
```

The examples above assumed that you have `vcpkg` already installed and you have a `VCPKG_ROOT` environment variable set. This is common if you use `vcpkg` to handle dependencies of multiple projects as only a single installation of `vcpkg` is required in that case. If you don't have `vcpkg` installed and you only want to use it to test `minio-cpp`, it's possible to install it locally like this:

```bash
$ git clone https://github.com/minio/minio-cpp
$ cd minio-cpp
$ git clone https://github.com/microsoft/vcpkg.git
$ ./vcpkg/bootstrap-vcpkg.sh
$ ./vcpkg/vcpkg install
$ cmake . -B ./build/Debug -DCMAKE_BUILD_TYPE=Debug -DMINIO_CPP_TEST=ON -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
$ cmake --build ./build/Debug
```

We recommend the setup with `VCPKG_ROOT` for development. In that case there is a `configure.sh` script, that can be used to create both Debug and Release projects:

```bash
$ git clone https://github.com/minio/minio-cpp
$ cd minio-cpp
$ ./configure.sh -DMINIO_CPP_TEST=ON
```

### Building on Alpine Linux (musl)

`vcpkg` can run on Alpine, but its default setup downloads glibc-linked tools
such as CMake, which do not run on musl (setting `VCPKG_FORCE_SYSTEM_BINARIES`
forces use of the apk-installed tools instead). Alpine also has no packages
for `cpp-httplib` or the C++ `INIReader`. The dependency resolver
(`cmake/miniocpp-deps.cmake`) therefore falls back to `pkg-config` and then
to fetching those two libraries from source, so a plain system-package build
works:

```bash
$ apk add build-base cmake git ninja pkgconf \
    inih-dev nlohmann-json openssl-dev pugixml-dev zlib-dev
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINIO_CPP_TEST=ON
$ cmake --build build
```

## Example:: file-uploader.cc

```c++
#include <miniocpp/client.h>

int main(int argc, char* argv[]) {
  // Create S3 base URL.
  minio::s3::BaseUrl base_url("play.min.io");

  // Create credential provider.
  minio::creds::StaticProvider provider(
      "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG");

  // Create S3 client.
  minio::s3::Client client(base_url, &provider);
  std::string bucket_name = "asiatrip";

  // Check 'asiatrip' bucket exist or not.
  bool exist;
  {
    minio::s3::BucketExistsArgs args;
    args.bucket = bucket_name;

    minio::s3::BucketExistsResponse resp = client.BucketExists(args);
    if (!resp) {
      std::cout << "unable to do bucket existence check; " << resp.Error()
                << std::endl;
      return EXIT_FAILURE;
    }

    exist = resp.exist;
  }

  // Make 'asiatrip' bucket if not exist.
  if (!exist) {
    minio::s3::MakeBucketArgs args;
    args.bucket = bucket_name;

    minio::s3::MakeBucketResponse resp = client.MakeBucket(args);
    if (!resp) {
      std::cout << "unable to create bucket; " << resp.Error() << std::endl;
      return EXIT_FAILURE;
    }
  }

  // Upload '/home/user/Photos/asiaphotos.zip' as object name
  // 'asiaphotos-2015.zip' to bucket 'asiatrip'.
  minio::s3::UploadObjectArgs args;
  args.bucket = bucket_name;
  args.object = "asiaphotos-2015.zip";
  args.filename = "/home/user/Photos/asiaphotos.zip";

  minio::s3::UploadObjectResponse resp = client.UploadObject(args);
  if (!resp) {
    std::cout << "unable to upload object; " << resp.Error() << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "'/home/user/Photos/asiaphotos.zip' is successfully uploaded as "
            << "object 'asiaphotos-2015.zip' to bucket 'asiatrip'."
            << std::endl;

  return EXIT_SUCCESS;
}
```

## RDMA (optional)

This SDK has optional support for RDMA-direct S3 PUT/GET against MinIO
servers. Build with:

```
cmake -DMINIO_CPP_ENABLE_RDMA=ON ...
```

The transport is `libs3rdma`, vendored under `vendor/s3rdma/` for both
`x86_64` and `aarch64`. It is plain IBTA verbs, so it supports RoCE and
native InfiniBand HCAs, and any memory `ibv_reg_mr` accepts: page-aligned
host memory, hugepages, and CUDA device buffers alike. It resolves the RDMA
stack itself at runtime, so building the SDK needs no RDMA packages on the
host, and the default build (`MINIO_CPP_ENABLE_RDMA=OFF`) omits the RDMA API
surface entirely.

CUDA is an application concern throughout: the SDK links no CUDA library and
calls no CUDA symbol. An application that allocates GPU buffers links CUDA
itself; one that passes pinned host memory needs no CUDA at all.

### Buffer size limit

An `x-amz-rdma-token` carries the transfer size in a 32-bit field, so a single
RDMA transfer can describe at most **4 GiB - 1** (`kRDMAMaxMemoryRegSize`).
`PutObject`/`GetObject` therefore use RDMA only for buffers up to that size; a
larger buffer is transferred over a single ordinary HTTP request instead
(AIStor accepts a single PUT up to 5 TiB, far beyond this ceiling and beyond
anything a client can realistically pin or allocate). The SDK does not chunk
registrations — sizing the buffer you hand to the RDMA API is the caller's
responsibility.

## License

This SDK is distributed under the [Apache License, Version 2.0](https://www.apache.org/licenses/LICENSE-2.0), see [LICENSE](https://github.com/minio/minio-cpp/blob/master/LICENSE) for more information.

The `libs3rdma` binaries under `vendor/s3rdma/` are NOT covered by this
Apache 2.0 grant — they are distributed under MinIO's own license terms.
