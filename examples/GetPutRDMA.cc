// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2024 MinIO, Inc.
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
//
// SPDX-License-Identifier: Apache-2.0

#include <dlfcn.h>
#include <miniocpp/client.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>

// NOTE (for example/CI use only):
// The dlopen(libcuda.so) shim below is a convenience so this example can be
// built and run on machines that do NOT have the full CUDA Toolkit installed
// — only the NVIDIA GPU driver, which always ships libcuda.so. It avoids the
// build-time dependency on cuda_runtime.h / libcudart.
//
// Why this is an application-side shim, not an SDK feature: minio-cpp itself
// has no CUDA runtime dependency (see the "CUDA dependency model" note at
// the top of include/miniocpp/rdma.h). Allocating/managing GPU memory is
// purely an application responsibility.
//
// Production applications that want GPU Direct Storage should NOT copy this
// pattern. Link against the real CUDA APIs (cudart or the CUDA driver library)
// via the CUDA Toolkit, use `#include <cuda_runtime.h>` / `#include <cuda.h>`
// directly, and let the linker resolve cu*/cuda* symbols at build time. That
// gives you proper error checking, symbol-version guarantees, and a supported
// toolchain path.
//
// CUdeviceptr / CUdevice / CUcontext / CUresult come from <cuda.h>,
// transitively via <miniocpp/client.h>.
namespace {

struct Cuda {
  void *lib = nullptr;
  CUresult (*cuInit)(unsigned);
  CUresult (*cuDeviceGet)(CUdevice *, int);
  CUresult (*cuCtxCreate)(CUcontext *, unsigned, CUdevice);
  CUresult (*cuCtxDestroy)(CUcontext);
  CUresult (*cuMemAlloc)(CUdeviceptr *, size_t);
  CUresult (*cuMemFree)(CUdeviceptr);
  CUresult (*cuMemsetD8)(CUdeviceptr, unsigned char, size_t);
  CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t);
  CUresult (*cuCtxSynchronize)();

  bool load() {
    lib = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (lib == nullptr) lib = dlopen("libcuda.so", RTLD_LAZY | RTLD_GLOBAL);
    if (lib == nullptr) {
      std::cerr << "dlopen libcuda.so failed: " << dlerror() << std::endl;
      return false;
    }
#define SYM(name, versioned)                                   \
  do {                                                         \
    void *s = dlsym(lib, versioned);                           \
    if (s == nullptr) s = dlsym(lib, #name);                   \
    if (s == nullptr) {                                        \
      std::cerr << "dlsym " << versioned << " failed: "        \
                << dlerror() << std::endl;                     \
      return false;                                            \
    }                                                          \
    name = reinterpret_cast<decltype(name)>(s);                \
  } while (0)
    SYM(cuInit, "cuInit");
    SYM(cuDeviceGet, "cuDeviceGet");
    SYM(cuCtxCreate, "cuCtxCreate_v2");
    SYM(cuCtxDestroy, "cuCtxDestroy_v2");
    SYM(cuMemAlloc, "cuMemAlloc_v2");
    SYM(cuMemFree, "cuMemFree_v2");
    SYM(cuMemsetD8, "cuMemsetD8_v2");
    SYM(cuMemcpyDtoH, "cuMemcpyDtoH_v2");
    SYM(cuCtxSynchronize, "cuCtxSynchronize");
#undef SYM
    return true;
  }
};

}  // namespace

int main(int argc, char *argv[]) {
  std::string host;
  std::string access_key;
  std::string secret_key;

  char *bufptr = nullptr;
  CUdeviceptr dptr = 0;
  size_t bufsize = 10 * 1024 * 1024UL;
  bool gpu_enabled = false;

  if (argc <= 1) {
    printf("usage: %s <server_address> <access_key> <secret_key> [size] [gpu]\n",
           argv[0]);
    exit(1);
  }

  host = std::string(argv[1]);
  access_key = std::string(argv[2]);
  secret_key = std::string(argv[3]);
  if (argc >= 5) bufsize = std::atoi(argv[4]);
  if (argc >= 6) gpu_enabled = std::string(argv[5]) == "gpu";

  minio::s3::BaseUrl base_url(host, false, "us-east-1");
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  std::cout << bufsize << " " << std::endl;

  Cuda cuda;
  CUcontext cu_ctx = nullptr;
  if (gpu_enabled) {
    if (!cuda.load()) {
      std::cerr << "CUDA driver (libcuda.so) unavailable — install NVIDIA "
                   "driver or omit 'gpu'"
                << std::endl;
      exit(1);
    }
    if (cuda.cuInit(0) != 0) {
      std::cerr << "cuInit failed" << std::endl;
      exit(1);
    }
    CUdevice dev = 0;
    if (cuda.cuDeviceGet(&dev, 0) != 0) {
      std::cerr << "cuDeviceGet failed" << std::endl;
      exit(1);
    }
    if (cuda.cuCtxCreate(&cu_ctx, 0, dev) != 0) {
      std::cerr << "cuCtxCreate failed" << std::endl;
      exit(1);
    }
    if (cuda.cuMemAlloc(&dptr, bufsize) != 0) {
      std::cerr << "cuMemAlloc failed" << std::endl;
      exit(1);
    }
    cuda.cuMemsetD8(dptr, 'A', bufsize);
    cuda.cuCtxSynchronize();
    bufptr = reinterpret_cast<char *>(dptr);
    std::cout << "GPU enabled" << std::endl;
  } else {
    int res = posix_memalign((void **)&bufptr, getpagesize(), bufsize);
    if (res) {
      std::cerr << "unable to allocate system memory with alignment"
                << getpagesize() << "buf size" << bufsize << std::endl;
    }
    assert(bufptr);
    memset(bufptr, 'A', bufsize);
  }

  minio::s3::PutObjectRDMAArgs pargs;
  pargs.buf = bufptr;
  pargs.size = bufsize;
  pargs.bucket = "my-bucket";
  pargs.object = "my-object";

  minio::s3::PutObjectResponse presp = client.PutObject(pargs);
  if (presp) {
    std::cout << std::endl
              << "data uploaded successfully " << presp.etag << std::endl;
  } else {
    std::cout << "unable to get object; " << presp.Error().String()
              << std::endl;
  }

  minio::s3::GetObjectRDMAArgs args;
  if (gpu_enabled) {
    cuda.cuMemsetD8(dptr, 'U', bufsize);
    cuda.cuCtxSynchronize();
  }
  args.buf = bufptr;
  args.size = bufsize;
  args.bucket = "my-bucket";
  args.object = "my-object";

  minio::s3::GetObjectResponse resp = client.GetObject(args);
  if (resp) {
    std::cout << std::endl
              << "data of my-object is received successfully" << std::endl;
  } else {
    std::cout << "unable to get object; " << resp.Error().String() << std::endl;
  }

  char *hostptr = (char *)malloc(bufsize);
  if (gpu_enabled) {
    cuda.cuMemcpyDtoH(hostptr, dptr, bufsize);
  } else {
    memcpy(hostptr, bufptr, bufsize);
  }

  std::ofstream file("output.txt", std::ios::binary);
  if (file.is_open()) {
    file.write(hostptr, bufsize);
    file.close();
    std::cout << "Buffer written to file successfully." << std::endl;
  } else {
    std::cerr << "Error opening file." << std::endl;
  }

  free(hostptr);
  if (gpu_enabled) {
    cuda.cuMemFree(dptr);
    cuda.cuCtxDestroy(cu_ctx);
  } else {
    free(bufptr);
  }

  return 0;
}
