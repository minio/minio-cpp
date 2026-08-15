# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MinIO C++ SDK is an S3-compatible object storage client library. This fork extends the upstream minio-cpp with RDMA (Remote Direct Memory Access) support for high-performance data transfers, over RoCE or native InfiniBand, into host or GPU memory.

## Build Commands

### Prerequisites
- CMake 3.10+
- C++17 compiler
- vcpkg package manager (set `VCPKG_ROOT` environment variable)
- `libs3rdma` vendored under `vendor/s3rdma/` (for RDMA support). Build and
  vendor it for both arches with `./build-libs.sh` in the sibling s3rdma repo.

### Configure and Build

```bash
# Configure both Debug and Release builds (recommended)
./configure.sh -DMINIO_CPP_TEST=ON

# Or configure manually for Debug
cmake . -B build/Debug -DCMAKE_BUILD_TYPE=Debug -DMINIO_CPP_TEST=ON

# Build
cmake --build ./build/Debug
```

### Build Options
- `MINIO_CPP_TEST=ON` - Build tests and examples
- `MINIO_CPP_MAKE_DOC=ON` - Build Doxygen documentation
- `BUILD_SHARED_LIBS=ON` - Build shared library (default is static)

### Running Tests

```bash
./build/Debug/tests
```

### Running Examples

Examples are built when `MINIO_CPP_TEST=ON`. Run individual examples:
```bash
./build/Debug/MakeBucket
./build/Debug/PutObject
./build/Debug/GetPutRDMA  # RDMA-specific example
./build/Debug/GPUHostDisk  # GPU Direct Storage example
```

## Architecture

### Core Components

- **`minio::s3::Client`** (`include/miniocpp/client.h`, `src/client.cc`) - Main S3 client class with high-level operations (UploadObject, DownloadObject, etc.)
- **`minio::s3::BaseClient`** (`include/miniocpp/baseclient.h`, `src/baseclient.cc`) - Base class implementing low-level S3 API operations and request execution

### Request/Response Flow

- **`args.h`** - Argument structs for each S3 operation (e.g., `PutObjectArgs`, `GetObjectArgs`)
- **`response.h`** - Response types returned by operations
- **`request.h`** - HTTP request construction
- **`http.h`** - HTTP client abstraction using curlpp

### Authentication

- **`providers.h`/`credentials.h`** - Credential providers (StaticProvider, EnvProvider, etc.)
- **`signer.h`** - AWS Signature V4 request signing

### RDMA Support (Fork-specific)

- **`rdma_client.h`** - `minio::rdma::Client`, a thin RAII wrapper over the
  `libs3rdma` client C API (register / mint token / classify pointer), plus
  the process-wide `minio::rdma::Shared()` accessor
- **`rdma.h`** - RDMA transport layer: token minting plus the S3 signing and
  HTTP control plane that carries `x-amz-rdma-token`

The SDK is the RDMA passive side: it pins the caller's buffer, mints a token
describing it, and sends that token on an ordinary signed HTTP request. The
server performs the one-sided transfer (READ for a PUT, WRITE for a GET) and
reports the outcome in `x-amz-rdma-reply`; a 501 means the server declined
RDMA and the caller falls back to HTTP.

## Dependencies (vcpkg)

- curlpp - HTTP client
- inih - INI file parsing for config
- nlohmann-json - JSON handling
- openssl - TLS/crypto
- pugixml - XML parsing for S3 responses

RDMA needs no additional system dependency at build time: libs3rdma is vendored, and it resolves the RDMA stack itself at runtime.

## Code Style Guidelines

- Do not add obvious comments (e.g., "Safe: Validate() ensures X has value")
- Use `std::optional<T>` for values that may be uninitialized (not sentinel values like `-1` for unsigned types)
- RDMA buffers require page-aligned memory - use `posix_memalign` or `std::aligned_alloc` (C++17)
