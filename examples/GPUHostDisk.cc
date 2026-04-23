// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2024 MinIO, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <dlfcn.h>
#include <miniocpp/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
// directly, and let the linker resolve cu*/cuda* symbols at build time.
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
    if (lib == nullptr) return false;
#define SYM(name, versioned)                                   \
  do {                                                         \
    void *s = dlsym(lib, versioned);                           \
    if (s == nullptr) s = dlsym(lib, #name);                   \
    if (s == nullptr) return false;                            \
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
  size_t bufsize = 10 * 1024 * 1024UL;
  if (argc == 2) bufsize = std::atoi(argv[1]);

  Cuda cuda;
  if (!cuda.load()) {
    std::cerr << "libcuda.so not found — install NVIDIA driver" << std::endl;
    return 1;
  }
  if (cuda.cuInit(0) != 0) {
    std::cerr << "cuInit failed" << std::endl;
    return 1;
  }
  CUdevice dev = 0;
  if (cuda.cuDeviceGet(&dev, 0) != 0) {
    std::cerr << "cuDeviceGet failed" << std::endl;
    return 1;
  }
  CUcontext ctx = nullptr;
  if (cuda.cuCtxCreate(&ctx, 0, dev) != 0) {
    std::cerr << "cuCtxCreate failed" << std::endl;
    return 1;
  }

  CUdeviceptr dptr = 0;
  if (cuda.cuMemAlloc(&dptr, bufsize) != 0) {
    std::cerr << "cuMemAlloc failed" << std::endl;
    return 1;
  }
  cuda.cuMemsetD8(dptr, 'A', bufsize);
  cuda.cuCtxSynchronize();

  char *hostptr = (char *)malloc(bufsize);
  cuda.cuMemcpyDtoH(hostptr, dptr, bufsize);

  std::ofstream file("output.txt", std::ios::binary);
  if (file.is_open()) {
    file.write(hostptr, bufsize);
    file.close();
    std::cout << "Buffer written to file successfully." << std::endl;
  } else {
    std::cerr << "Error opening file." << std::endl;
  }

  free(hostptr);
  cuda.cuMemFree(dptr);
  cuda.cuCtxDestroy(ctx);

  return 0;
}
