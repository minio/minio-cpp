# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MinIO C++ SDK is an S3-compatible object storage client library. This fork extends the upstream minio-cpp with RDMA (Remote Direct Memory Access) and NVIDIA GPU Direct Storage support for high-performance data transfers.

## Build Commands

### Prerequisites
- CMake 3.10+
- C++17 compiler
- vcpkg package manager (set `VCPKG_ROOT` environment variable)
- NVIDIA CUDA toolkit at `/usr/local/cuda` (for RDMA/GPU features)
- cuObjClient library (for RDMA support)

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

### RDMA/GPU Support (Fork-specific)

- **`rdma.h`** - RDMA transport layer with S3 signing for GPU Direct Storage
- **`rdma-httplib.h`** - HTTP-over-RDMA implementation
- **`nvidia-cufile.h`** - NVIDIA cuFile integration headers
- **`nvidia-cuobjclient.h`** - cuObjClient wrapper for RDMA operations

The RDMA implementation uses `objectPut`/`objectGet` callbacks that are invoked by the cuFile RDMA layer for direct GPU-to-storage transfers.

## Dependencies (vcpkg)

- curlpp - HTTP client
- inih - INI file parsing for config
- nlohmann-json - JSON handling
- openssl - TLS/crypto
- pugixml - XML parsing for S3 responses

Additional system dependencies for RDMA: libcufile, libcuobjclient, libibverbs, librdmacm

## Code Style Guidelines

- Do not add obvious comments (e.g., "Safe: Validate() ensures X has value")
- Use `std::optional<T>` for values that may be uninitialized (not sentinel values like `-1` for unsigned types)
- RDMA buffers require page-aligned memory - use `posix_memalign` or `std::aligned_alloc` (C++17)
