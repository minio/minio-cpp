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

#include "miniocpp/client.h"

#ifdef _MSC_VER
#include <malloc.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <httplib.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include "miniocpp/args.h"
#include "miniocpp/baseclient.h"
#include "miniocpp/error.h"
#include "miniocpp/http.h"
#include "miniocpp/providers.h"
#include "miniocpp/request.h"
#include "miniocpp/response.h"
#include "miniocpp/sse.h"
#include "miniocpp/types.h"
#include "miniocpp/utils.h"

#ifdef MINIO_CPP_RDMA
#include "miniocpp/rdma.h"
#include "miniocpp/rdma_client.h"
#endif

namespace minio::s3 {

namespace {

#ifdef _MSC_VER
inline size_t GetPageSize() { return 4096; }
inline int AlignedAlloc(void** out, size_t alignment, size_t size) {
  *out = _aligned_malloc(size, alignment);
  return *out ? 0 : -1;
}
inline void AlignedFree(void* p) { _aligned_free(p); }
#else
inline size_t GetPageSize() { return static_cast<size_t>(getpagesize()); }
inline int AlignedAlloc(void** out, size_t alignment, size_t size) {
  return posix_memalign(out, alignment, size);
}
inline void AlignedFree(void* p) { free(p); }
#endif

struct AlignedBuffer {
  void* ptr = nullptr;
  AlignedBuffer() = default;
  explicit AlignedBuffer(void* p) : ptr(p) {}
  AlignedBuffer(const AlignedBuffer&) = delete;
  AlignedBuffer& operator=(const AlignedBuffer&) = delete;
  AlignedBuffer(AlignedBuffer&& o) noexcept : ptr(o.ptr) { o.ptr = nullptr; }
  AlignedBuffer& operator=(AlignedBuffer&& o) noexcept {
    if (this != &o) {
      if (ptr) AlignedFree(ptr);
      ptr = o.ptr;
      o.ptr = nullptr;
    }
    return *this;
  }
  ~AlignedBuffer() {
    if (ptr) AlignedFree(ptr);
  }
};

#ifdef MINIO_CPP_RDMA
// The HTTP fallbacks below fill or drain the caller's buffer with ordinary host
// loads and stores, which fault on a CUDA device pointer. Stage through host
// memory instead. The two driver-API copies are resolved lazily with dlopen so
// libminiocpp keeps its "no CUDA link dependency" property: a host without the
// driver simply reports the copy as unavailable and the caller gets an error
// rather than a crash.
struct CudaHostCopy {
  using DtoHFn = int (*)(void*, unsigned long long, size_t);
  using HtoDFn = int (*)(unsigned long long, const void*, size_t);
  using CtxGetCurrentFn = int (*)(void**);
  using CtxSetCurrentFn = int (*)(void*);
  using PrimaryCtxRetainFn = int (*)(void**, int);
  using PrimaryCtxReleaseFn = int (*)(int);
  using PointerGetAttrFn = int (*)(void*, int, unsigned long long);
  DtoHFn dtoh = nullptr;
  HtoDFn htod = nullptr;
  CtxGetCurrentFn ctx_get = nullptr;
  CtxSetCurrentFn ctx_set = nullptr;
  PrimaryCtxRetainFn ctx_retain = nullptr;
  PrimaryCtxReleaseFn ctx_release = nullptr;
  PointerGetAttrFn ptr_attr = nullptr;
  bool Ok() const {
    return dtoh != nullptr && htod != nullptr && ctx_get != nullptr &&
           ctx_set != nullptr && ctx_retain != nullptr &&
           ctx_release != nullptr && ptr_attr != nullptr;
  }
};

const CudaHostCopy& GetCudaHostCopy() {
  static const CudaHostCopy fns = [] {
    CudaHostCopy c;
#ifndef _MSC_VER
    if (void* h = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL)) {
      c.dtoh =
          reinterpret_cast<CudaHostCopy::DtoHFn>(dlsym(h, "cuMemcpyDtoH_v2"));
      c.htod =
          reinterpret_cast<CudaHostCopy::HtoDFn>(dlsym(h, "cuMemcpyHtoD_v2"));
      c.ctx_get = reinterpret_cast<CudaHostCopy::CtxGetCurrentFn>(
          dlsym(h, "cuCtxGetCurrent"));
      c.ctx_set = reinterpret_cast<CudaHostCopy::CtxSetCurrentFn>(
          dlsym(h, "cuCtxSetCurrent"));
      c.ctx_retain = reinterpret_cast<CudaHostCopy::PrimaryCtxRetainFn>(
          dlsym(h, "cuDevicePrimaryCtxRetain"));
      c.ctx_release = reinterpret_cast<CudaHostCopy::PrimaryCtxReleaseFn>(
          dlsym(h, "cuDevicePrimaryCtxRelease_v2"));
      c.ptr_attr = reinterpret_cast<CudaHostCopy::PointerGetAttrFn>(
          dlsym(h, "cuPointerGetAttribute"));
    }
#endif
    return c;
  }();
  return fns;
}

// The driver API copies operate on the calling thread's *current* context.
// GetObjectAsync/PutObjectAsync dispatch through std::async, so a fallback can
// run on a pool thread that has never touched CUDA; there every copy would fail
// with CUDA_ERROR_INVALID_CONTEXT. Borrow the primary context of the device the
// buffer belongs to for the duration of the copy, and put the thread back the
// way we found it afterwards. A thread that already has a context is left
// alone.
class ScopedCudaContext {
 public:
  ScopedCudaContext(const CudaHostCopy& fns, void* devptr) : fns_(&fns) {
    void* current = nullptr;
    if (fns.ctx_get(&current) != 0) return;
    if (current != nullptr) {
      ok_ = true;  // caller already established one; nothing to do
      return;
    }
    // No context on this thread. cuPointerGetAttribute can itself require one,
    // so borrow device 0's primary context purely to make the ordinal query
    // legal, then move to the buffer's own device when it differs.
    if (!Retain(0)) return;
    int ordinal = 0;
    // 9 == CU_POINTER_ATTRIBUTE_DEVICE_ORDINAL
    if (fns.ptr_attr(&ordinal, 9,
                     reinterpret_cast<unsigned long long>(devptr)) != 0) {
      return;  // destructor releases the bootstrap context
    }
    if (ordinal != 0) {
      const int bootstrap = *retained_ordinal_;
      retained_ordinal_.reset();  // Retain() overwrites it; release manually
      if (!Retain(ordinal)) {
        Release(bootstrap);
        return;
      }
      Release(bootstrap);
    }
    ok_ = true;
  }
  ScopedCudaContext(const ScopedCudaContext&) = delete;
  ScopedCudaContext& operator=(const ScopedCudaContext&) = delete;
  ~ScopedCudaContext() {
    if (!retained_ordinal_.has_value()) return;
    if (fns_->ctx_set(nullptr) != 0) {
      std::cerr << "warning: cuCtxSetCurrent(nullptr) failed during teardown"
                << std::endl;
    }
    Release(*retained_ordinal_);
  }
  bool Ok() const { return ok_; }

 private:
  // Retains device `ordinal`'s primary context and makes it current.
  bool Retain(int ordinal) {
    void* ctx = nullptr;
    if (fns_->ctx_retain(&ctx, ordinal) != 0) return false;
    if (fns_->ctx_set(ctx) != 0) {
      Release(ordinal);
      return false;
    }
    retained_ordinal_ = ordinal;
    return true;
  }

  void Release(int ordinal) {
    if (fns_->ctx_release(ordinal) != 0) {
      std::cerr << "warning: cuDevicePrimaryCtxRelease failed for device "
                << ordinal << std::endl;
    }
  }

  const CudaHostCopy* fns_;
  std::optional<int> retained_ordinal_;
  bool ok_ = false;
};
// True when buf is not ordinary host memory, i.e. the HTTP fallbacks cannot
// touch it directly. An unclassifiable pointer counts as device memory on
// purpose: staging it through CUDA costs a failed copy and a clear error,
// whereas memcpy on a device pointer that we guessed was host segfaults.
bool IsDeviceBuffer(void* buf) {
  return buf != nullptr && minio::rdma::Client::GetMemoryType(buf) !=
                               minio::rdma::MemoryType::kSystem;
}

// Releases an RDMA buffer registration when it goes out of scope. Declared
// *after* the buffer it covers, so destruction order (reverse of declaration)
// guarantees deregister-before-free regardless of which control path returns.
struct ScopedRDMARegistration {
  minio::rdma::Client* client = nullptr;
  void* buf = nullptr;
  ScopedRDMARegistration() = default;
  ScopedRDMARegistration(minio::rdma::Client* c, void* p) : client(c), buf(p) {}
  ScopedRDMARegistration(const ScopedRDMARegistration&) = delete;
  ScopedRDMARegistration& operator=(const ScopedRDMARegistration&) = delete;
  ScopedRDMARegistration(ScopedRDMARegistration&& o) noexcept
      : client(o.client), buf(o.buf) {
    o.client = nullptr;
    o.buf = nullptr;
  }
  ScopedRDMARegistration& operator=(ScopedRDMARegistration&& o) noexcept {
    if (this != &o) {
      Release();
      client = o.client;
      buf = o.buf;
      o.client = nullptr;
      o.buf = nullptr;
    }
    return *this;
  }
  ~ScopedRDMARegistration() { Release(); }

 private:
  void Release() {
    if (client && buf && !client->Deregister(buf)) {
      std::cerr << "warning: RDMA deregistration failed during teardown"
                << std::endl;
    }
    client = nullptr;
    buf = nullptr;
  }
};
#endif

}  // namespace

ListObjectsResult::ListObjectsResult([[maybe_unused]] error::Error err)
    : failed_(true) {
  resp_ = std::make_shared<ListObjectsResponse>();
  itr_ = resp_->contents.end();
}

ListObjectsResult::ListObjectsResult(Client* const client,
                                     const ListObjectsArgs& args)
    : client_(client), args_(args) {
  resp_ = std::make_shared<ListObjectsResponse>();
  itr_ = resp_->contents.end();
  StartPrefetch();
  Populate();
}

ListObjectsResult::ListObjectsResult(Client* const client,
                                     ListObjectsArgs&& args)
    : client_(client), args_(std::move(args)) {
  resp_ = std::make_shared<ListObjectsResponse>();
  itr_ = resp_->contents.end();
  StartPrefetch();
  Populate();
}

void ListObjectsResult::UpdatePaginationArgs() {
  if (args_.include_versions || !args_.version_id_marker.empty()) {
    args_.key_marker = resp_->next_key_marker;
    args_.version_id_marker = resp_->next_version_id_marker;
  } else if (args_.use_api_v1) {
    args_.marker = resp_->next_marker;
  } else {
    args_.start_after = resp_->start_after;
    args_.continuation_token = resp_->next_continuation_token;
  }
}

void ListObjectsResult::StartPrefetch() {
  ListObjectsArgs next_args = args_;
  try {
    prefetch_future_ = std::make_shared<
        std::shared_future<std::shared_ptr<ListObjectsResponse>>>(std::async(
        std::launch::async,
        [client = client_, next_args = std::move(next_args)]() mutable
            -> std::shared_ptr<ListObjectsResponse> {
          try {
            auto resp = client->GetRegion(next_args.bucket, next_args.region);
            if (resp) {
              next_args.region = resp->region;
              if (next_args.recursive) {
                next_args.delimiter = "";
              } else if (next_args.delimiter.empty()) {
                next_args.delimiter = "/";
              }

              if (next_args.include_versions ||
                  !next_args.version_id_marker.empty()) {
                auto list_resp = client->ListObjectVersions(
                    ListObjectVersionsArgs(next_args));
                if (list_resp) {
                  return std::shared_ptr<ListObjectsResponse>(
                      new ListObjectsResponse(std::move(*list_resp)));
                }
                return std::make_shared<ListObjectsResponse>();
              } else if (next_args.use_api_v1) {
                auto list_resp =
                    client->ListObjectsV1(ListObjectsV1Args(next_args));
                if (list_resp) {
                  return std::shared_ptr<ListObjectsResponse>(
                      new ListObjectsResponse(std::move(*list_resp)));
                }
                return std::make_shared<ListObjectsResponse>();
              } else {
                auto list_resp =
                    client->ListObjectsV2(ListObjectsV2Args(next_args));
                if (list_resp) {
                  return std::shared_ptr<ListObjectsResponse>(
                      new ListObjectsResponse(std::move(*list_resp)));
                }
                return std::make_shared<ListObjectsResponse>();
              }
            }
            return std::make_shared<ListObjectsResponse>();
          } catch (const std::exception& e) {
            return std::make_shared<ListObjectsResponse>();
          }
        }));
  } catch (const std::exception& e) {
    std::promise<std::shared_ptr<ListObjectsResponse>> p;
    p.set_value(std::make_shared<ListObjectsResponse>());
    prefetch_future_ = std::make_shared<
        std::shared_future<std::shared_ptr<ListObjectsResponse>>>(
        p.get_future());
  }
}

void ListObjectsResult::Populate() {
  if (!prefetch_future_ || !prefetch_future_->valid()) {
    return;
  }
  try {
    resp_ = prefetch_future_->get();
  } catch (const std::exception& e) {
    resp_ = std::make_shared<ListObjectsResponse>();
  }
  prefetch_future_.reset();
  if (failed_ || resp_->contents.empty()) {
    failed_ = true;
  }
  itr_ = resp_->contents.begin();

  if (!failed_ && resp_->is_truncated) {
    UpdatePaginationArgs();
    StartPrefetch();
  }
}

RemoveObjectsResult::RemoveObjectsResult([[maybe_unused]] error::Error err) {
  done_ = true;
  itr_ = resp_.errors.end();
}

RemoveObjectsResult::RemoveObjectsResult(Client* const client,
                                         const RemoveObjectsArgs& args)
    : client_(client), args_(args) {
  Populate();
}

RemoveObjectsResult::RemoveObjectsResult(Client* const client,
                                         RemoveObjectsArgs&& args)
    : client_(client), args_(args) {
  Populate();
}

void RemoveObjectsResult::Populate() {
  while (!done_ && resp_.errors.size() == 0) {
    RemoveObjectsApiArgs args;
    args.extra_headers = args_.extra_headers;
    args.extra_query_params = args_.extra_query_params;
    args.bucket = args_.bucket;
    args.region = args_.region;
    args.quiet = true;
    args.bypass_governance_mode = args_.bypass_governance_mode;

    for (int i = 0; i < 1000; i++) {
      DeleteObject object;
      if (!args_.func(object)) {
        break;
      }
      args.objects.push_back(object);
    }

    if (args.objects.size() != 0) {
      auto rm_resp = client_->BaseClient::RemoveObjects(args);
      if (rm_resp) {
        resp_ = std::move(*rm_resp);
      }
      itr_ = resp_.errors.begin();
    } else {
      done_ = true;
    }
  }
  // Caller's func may have returned false on the very first call (empty
  // batch). `done_` flips to true above but itr_ was never assigned, so
  // operator bool() would compare an uninitialized iterator. Pin it to
  // end() so the result evaluates to false cleanly.
  if (done_ && resp_.errors.empty()) {
    itr_ = resp_.errors.end();
  }
}

#ifdef MINIO_CPP_RDMA
minio::rdma::Client& Client::SharedRDMAClient() {
  return minio::rdma::Shared();
}
#endif

Client::Client(BaseUrl& base_url, creds::Provider* const provider)
    : BaseClient(base_url, provider) {}

Result<StatObjectResponse> Client::CalculatePartCount(
    size_t& part_count, std::list<ComposeSource> sources) {
  size_t object_size = 0;
  size_t i = 0;
  for (auto& source : sources) {
    if (source.ssec != nullptr && !base_url_.https) {
      std::string msg = "source " + source.bucket + "/" + source.object;
      if (!source.version_id.empty()) {
        msg += "?versionId=" + source.version_id;
      }
      msg += ": SSE-C operation must be performed over a secure connection";
      return error::make<StatObjectResponse>(msg);
    }

    i++;

    std::string etag;
    size_t size;

    auto resp = StatObject(source);
    if (!resp) {
      return resp;
    }
    etag = resp->etag;
    size = resp->size;
    if (error::Error err = source.BuildHeaders(size, etag)) {
      return tl::make_unexpected(err);
    }
    if (source.length.has_value()) {
      size = *source.length;
    } else if (source.offset.has_value()) {
      size -= *source.offset;
    }

    if (size < utils::kMinPartSize && sources.size() != 1 &&
        i != sources.size()) {
      std::string msg = "source " + source.bucket + "/" + source.object;
      if (!source.version_id.empty()) msg += "?versionId=" + source.version_id;
      msg += ": size " + std::to_string(size) + " must be greater than " +
             std::to_string(utils::kMinPartSize);
      return error::make<StatObjectResponse>(msg);
    }

    object_size += size;
    if (object_size > utils::kMaxObjectSize) {
      return error::make<StatObjectResponse>(
          "destination object size must be less than " +
          std::to_string(utils::kMaxObjectSize));
    }

    if (size > utils::kMaxPartSize) {
      size_t count = size / utils::kMaxPartSize;
      size_t last_part_size = size - (count * utils::kMaxPartSize);
      if (last_part_size > 0) {
        count++;
      } else {
        last_part_size = utils::kMaxPartSize;
      }

      if (last_part_size < utils::kMinPartSize && sources.size() != 1 &&
          i != sources.size()) {
        std::string msg = "source " + source.bucket + "/" + source.object;
        if (!source.version_id.empty()) {
          msg += "?versionId=" + source.version_id;
        }
        msg += ": size " + std::to_string(size) +
               " for multipart split upload of " + std::to_string(size) +
               ", last part size is less than " +
               std::to_string(utils::kMinPartSize);
        return error::make<StatObjectResponse>(msg);
      }

      part_count += count;
    } else {
      part_count++;
    }

    if (part_count > utils::kMaxMultipartCount) {
      return error::make<StatObjectResponse>(
          "Compose sources create more than allowed multipart count " +
          std::to_string(utils::kMaxMultipartCount));
    }
  }

  StatObjectResponse result;
  return result;
}

Result<ComposeObjectResponse> Client::ComposeObject(ComposeObjectArgs args,
                                                    std::string& upload_id) {
  size_t part_count = 0;
  {
    auto resp = CalculatePartCount(part_count, args.sources);
    if (!resp) {
      return tl::make_unexpected(resp.error());
    }
  }

  ComposeSource& source = args.sources.front();
  if (part_count == 1 && !source.offset.has_value() &&
      !source.length.has_value()) {
    CopyObjectArgs coargs;
    coargs.extra_headers = args.extra_headers;
    coargs.extra_query_params = args.extra_query_params;
    coargs.bucket = args.bucket;
    coargs.region = args.region;
    coargs.object = args.object;
    coargs.sse = args.sse;
    coargs.source = source;

    auto co_resp = CopyObject(coargs);
    if (!co_resp) return tl::make_unexpected(co_resp.error());
    return ComposeObjectResponse(std::move(*co_resp));
  }

  utils::Multimap headers = args.Headers();

  {
    CreateMultipartUploadArgs cmu_args;
    cmu_args.extra_query_params = args.extra_query_params;
    cmu_args.bucket = args.bucket;
    cmu_args.region = args.region;
    cmu_args.object = args.object;
    cmu_args.headers = headers;
    if (auto resp = CreateMultipartUpload(cmu_args)) {
      upload_id = resp->upload_id;
    } else {
      return tl::make_unexpected(resp.error());
    }
  }

  unsigned int part_number = 0;
  utils::Multimap ssecheaders;
  if (args.sse != nullptr) {
    if (SseCustomerKey* ssec = dynamic_cast<SseCustomerKey*>(args.sse)) {
      ssecheaders = ssec->Headers();
    }
  }

  std::list<Part> parts;
  for (auto& source : args.sources) {
    size_t size = source.ObjectSize();
    if (source.length.has_value()) {
      size = *source.length;
    } else if (source.offset.has_value()) {
      size -= *source.offset;
    }

    size_t offset = 0;
    if (source.offset.has_value()) offset = *source.offset;

    utils::Multimap headers;
    headers.AddAll(source.Headers());
    headers.AddAll(ssecheaders);

    if (size <= utils::kMaxPartSize) {
      part_number++;
      if (source.length.has_value()) {
        headers.Add("x-amz-copy-source-range",
                    "bytes=" + std::to_string(offset) + "-" +
                        std::to_string(offset + *source.length - 1));
      } else if (source.offset.has_value()) {
        headers.Add("x-amz-copy-source-range",
                    "bytes=" + std::to_string(offset) + "-" +
                        std::to_string(offset + size - 1));
      }

      UploadPartCopyArgs upc_args;
      upc_args.bucket = args.bucket;
      upc_args.region = args.region;
      upc_args.object = args.object;
      upc_args.headers = headers;
      upc_args.upload_id = upload_id;
      upc_args.part_number = part_number;
      auto resp = UploadPartCopy(upc_args);
      if (!resp) {
        return tl::make_unexpected(resp.error());
      }
      parts.push_back(Part(part_number, std::move(resp->etag)));
    } else {
      while (size > 0) {
        part_number++;

        size_t length = size;
        if (length > utils::kMaxPartSize) length = utils::kMaxPartSize;
        size_t end_bytes = offset + length - 1;

        utils::Multimap headerscopy;
        headerscopy.AddAll(headers);
        headerscopy.Add("x-amz-copy-source-range",
                        "bytes=" + std::to_string(offset) + "-" +
                            std::to_string(end_bytes));

        UploadPartCopyArgs upc_args;
        upc_args.bucket = args.bucket;
        upc_args.region = args.region;
        upc_args.object = args.object;
        upc_args.headers = headerscopy;
        upc_args.upload_id = upload_id;
        upc_args.part_number = part_number;
        {
          auto resp = UploadPartCopy(upc_args);
          if (!resp) {
            return tl::make_unexpected(resp.error());
          }
          parts.push_back(Part(part_number, std::move(resp->etag)));
        }
        offset += length;
        size -= length;
      }
    }
  }

  CompleteMultipartUploadArgs cmu_args;
  cmu_args.bucket = args.bucket;
  cmu_args.region = args.region;
  cmu_args.object = args.object;
  cmu_args.upload_id = upload_id;
  cmu_args.parts = parts;
  auto cmu_resp = CompleteMultipartUpload(cmu_args);
  if (!cmu_resp) return tl::make_unexpected(cmu_resp.error());
  return ComposeObjectResponse(std::move(*cmu_resp));
}

Result<GetObjectResponse> Client::GetObject(GetObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

#ifdef MINIO_CPP_RDMA
  if (args.buf != nullptr) {
    std::string region;
    if (auto get_resp = GetRegion(args.bucket, args.region)) {
      region = get_resp->region;
    } else {
      return tl::make_unexpected(get_resp.error());
    }

    const size_t size = *args.size;

    // An explicit offset selects an object byte-range (size bytes from it),
    // letting a caller stream an object larger than one registration as a
    // sequence of <= 4 GiB ranged GETs. Unset means read the whole object.
    const int64_t range_offset =
        args.offset.has_value() ? static_cast<int64_t>(*args.offset) : -1;

    // Process-wide RDMA client — see client.h. A buffer larger than one RDMA
    // descriptor can describe (kRDMAMaxMemoryRegSize) cannot be named to the
    // server; skip straight to the HTTP path rather than issue a registration
    // that is guaranteed to fail.
    minio::rdma::Client& rdma_client = SharedRDMAClient();
    bool use_rdma =
        size <= kRDMAMaxMemoryRegSize && rdma_client.Register(args.buf, size);

    if (use_rdma) {
      minio::rdma::ClientCtx getCtx = {provider_, args.bucket,  args.object,
                                       {},        std::nullopt, {},
                                       base_url_, region};

      // RAII, matching the multipart paths below. rdmaGetWithRetry signs and
      // sends an HTTP request, and curlpp throws, so a manual Deregister after
      // the call is skipped on that path and the buffer stays pinned for the
      // life of the process.
      ScopedRDMARegistration reg(&rdma_client, args.buf);

      ssize_t ret =
          rdmaGetWithRetry(&rdma_client, &getCtx, args.buf, size, range_offset);

      if (ret > 0) {
        GetObjectResponse go_result;
        go_result.etag = getCtx.etag;
        return go_result;
      }
      // ret < 0 (retries exhausted) or kRDMANotSupported (server declined):
      // fall through to HTTP-into-buffer path below.
    }

    // HTTP fallback: stream the body into the caller's buffer. pubsetbuf and
    // the stream writes below are ordinary host stores, so a device pointer has
    // to be staged through host memory — writing straight into it faults
    // (SIGSEGV at the buffer address) the moment an RDMA GET declines, which is
    // exactly what concurrent GETs provoke.
    const bool device_buf = IsDeviceBuffer(args.buf);
    std::vector<char> stage;
    char* sink = args.buf;
    if (device_buf) {
      if (!GetCudaHostCopy().Ok()) {
        return error::make<GetObjectResponse>(
            "RDMA GET failed and the HTTP fallback cannot reach device memory: "
            "libcuda.so.1 is unavailable");
      }
      stage.resize(size);
      sink = stage.data();
    }

    GetObjectArgs targs;
    std::stringstream ss(std::ios_base::in | std::ios_base::out);
    ss.rdbuf()->pubsetbuf(sink, size);

    targs.bucket = args.bucket;
    targs.object = args.object;
    targs.region = region;
    // Mirror the RDMA range on the fallback: read the same size bytes from the
    // same object offset (Headers() turns offset/length into a Range header).
    if (range_offset >= 0) {
      targs.offset = static_cast<size_t>(range_offset);
      targs.length = size;
    }
    targs.datafunc = [&ss = ss](minio::http::DataFunctionArgs args) -> bool {
      ss << args.datachunk;
      return true;
    };

    Result<GetObjectResponse> tresp = BaseClient::GetObject(targs);
    if (tresp.has_value() && device_buf) {
      ScopedCudaContext ctx(GetCudaHostCopy(), args.buf);
      if (!ctx.Ok()) {
        return error::make<GetObjectResponse>(
            "unable to establish a CUDA context for the HTTP fallback copy");
      }
      if (GetCudaHostCopy().htod(reinterpret_cast<unsigned long long>(args.buf),
                                 stage.data(), size) != 0) {
        return error::make<GetObjectResponse>(
            "unable to copy the staged HTTP body into device memory");
      }
    }
    return tresp;
  }
#endif

  return BaseClient::GetObject(args);
}

Result<PutObjectResponse> Client::PutObject(PutObjectArgs args,
                                            std::string& upload_id, char* buf) {
  utils::Multimap headers = args.Headers();
  if (!headers.Contains("Content-Type")) {
    if (args.content_type.empty()) {
      headers.Add("Content-Type", "application/octet-stream");
    } else {
      headers.Add("Content-Type", args.content_type);
    }
  }

  std::optional<uint64_t> object_size = args.object_size;
  size_t part_size = args.part_size;
  size_t uploaded_size = 0;
  unsigned int part_number = 0;
  std::string one_byte;
  bool stop = false;
  std::list<Part> parts;
  std::optional<size_t> part_count = args.part_count;
  double uploaded_bytes = 0;           // for progress
  std::optional<double> upload_speed;  // for progress

  auto read_part_data = [&](char* buf, size_t& bytes_read) -> error::Error {
    if (part_count.has_value()) {
      if (part_number == *part_count) {
        part_size = *object_size - uploaded_size;
        stop = true;
      }

      if (error::Error err =
              utils::ReadPart(*args.stream, buf, part_size, bytes_read)) {
        return err;
      }

      if (bytes_read != part_size) {
        return error::Error("not enough data in the stream; expected: " +
                            std::to_string(part_size) +
                            ", got: " + std::to_string(bytes_read) + " bytes");
      }
    } else {
      char* b = buf;
      size_t size = part_size + 1;

      if (!one_byte.empty()) {
        buf[0] = one_byte.front();
        b = buf + 1;
        size--;
        bytes_read = 1;
        one_byte = "";
      }

      size_t n = 0;
      if (error::Error err = utils::ReadPart(*args.stream, b, size, n)) {
        return err;
      }

      bytes_read += n;

      // If bytes read is less than or equals to part size, then we have reached
      // last part.
      if (bytes_read <= part_size) {
        part_count = std::optional<size_t>(part_number);
        part_size = bytes_read;
        stop = true;
      } else {
        one_byte = buf[part_size];
      }
    }
    return error::Error();
  };

  while (!stop) {
    part_number++;

    size_t bytes_read = 0;
    if (error::Error err = read_part_data(buf, bytes_read)) {
      return tl::make_unexpected(err);
    }

    std::string_view data(buf, part_size);

    uploaded_size += part_size;

    if (part_count.has_value() && *part_count == 1) {
      PutObjectApiArgs api_args;
      api_args.extra_query_params = args.extra_query_params;
      api_args.bucket = args.bucket;
      api_args.region = args.region;
      api_args.object = args.object;
      api_args.data = data;
      api_args.buf = buf;
      api_args.size = part_size;
      api_args.progressfunc = args.progressfunc;
      api_args.progress_userdata = args.progress_userdata;
      api_args.headers = headers;

      return BaseClient::PutObject(api_args);
    }

    if (upload_id.empty()) {
      CreateMultipartUploadArgs cmu_args;
      cmu_args.extra_query_params = args.extra_query_params;
      cmu_args.bucket = args.bucket;
      cmu_args.region = args.region;
      cmu_args.object = args.object;
      cmu_args.headers = headers;
#ifdef MINIO_CPP_RDMA
      // Declare CRC64NVME so the server enforces per-part integrity on the
      // RDMA UploadPart path (server returns 501 if checksum is missing when
      // an algorithm was declared on Create).
      cmu_args.headers.Add("x-amz-checksum-algorithm", "CRC64NVME");
#endif
      auto cmu_resp = CreateMultipartUpload(cmu_args);
      if (cmu_resp) {
        upload_id = cmu_resp->upload_id;
      } else {
        return tl::make_unexpected(cmu_resp.error());
      }
    }

    UploadPartArgs up_args;
    up_args.bucket = args.bucket;
    up_args.region = args.region;
    up_args.object = args.object;
    up_args.upload_id = upload_id;
    up_args.part_number = part_number;
    up_args.data = data;
    up_args.buf = buf;
    up_args.part_size = part_size;
#ifdef MINIO_CPP_RDMA
    up_args.rdmaclient = args.rdmaclient;
    if (buf != nullptr && minio::rdma::Client::GetMemoryType(buf) ==
                              minio::rdma::MemoryType::kSystem) {
      const std::string crc = utils::Crc64NvmeBase64(buf, part_size);
      up_args.checksum_crc64nvme = crc;
      up_args.headers.Add("x-amz-checksum-crc64nvme", crc);
    }
#endif
    if (args.progressfunc != nullptr) {
      up_args.progressfunc =
          [&object_size = object_size, &uploaded_bytes = uploaded_bytes,
           &upload_speed = upload_speed, &progressfunc = args.progressfunc,
           &progress_userdata = args.progress_userdata](
              http::ProgressFunctionArgs args) -> bool {
        if (args.upload_speed > 0) {
          if (!upload_speed.has_value()) {
            upload_speed = args.upload_speed;
          } else {
            upload_speed = (*upload_speed + args.upload_speed) / 2.0;
          }
          return true;
        }

        http::ProgressFunctionArgs actual_args;
        actual_args.upload_total_bytes =
            object_size ? static_cast<double>(*object_size) : -1.0;
        actual_args.uploaded_bytes = uploaded_bytes + args.uploaded_bytes;
        actual_args.userdata = progress_userdata;
        return progressfunc(actual_args);
      };
    }
    // Propagate caller-supplied x-amz-content-sha256 (e.g. UNSIGNED-PAYLOAD
    // for GPU-resident buffers) into each UploadPart so the per-part signing
    // path also skips hashing the body.
    if (headers.Contains("x-amz-content-sha256")) {
      up_args.headers.Add("x-amz-content-sha256",
                          headers.GetFront("x-amz-content-sha256"));
    }
    if (args.sse != nullptr) {
      if (SseCustomerKey* ssec = dynamic_cast<SseCustomerKey*>(args.sse)) {
        up_args.headers.AddAll(ssec->Headers());
      }
    }

    if (auto resp = UploadPart(up_args)) {
      if (args.progressfunc != nullptr) {
        uploaded_bytes += static_cast<double>(data.length());
        http::ProgressFunctionArgs actual_args;
        actual_args.upload_total_bytes =
            object_size ? static_cast<double>(*object_size) : -1.0;
        actual_args.uploaded_bytes = uploaded_bytes;
        actual_args.userdata = args.progress_userdata;
        if (!args.progressfunc(actual_args)) {
          return tl::make_unexpected(
              error::Error("aborted by progress function"));
        }
      }
      // HTTP fallback leaves resp.checksum_crc64nvme empty; use the local CRC.
      parts.push_back(Part(part_number, std::move(resp->etag),
                           std::move(up_args.checksum_crc64nvme)));
    } else {
      return resp;
    }
  }

  CompleteMultipartUploadArgs cmu_args;
  cmu_args.bucket = args.bucket;
  cmu_args.region = args.region;
  cmu_args.object = args.object;
  cmu_args.upload_id = upload_id;
  cmu_args.parts = parts;
  auto resp = CompleteMultipartUpload(cmu_args);
  if (!resp) {
    return tl::make_unexpected(resp.error());
  }
  if (args.progressfunc != nullptr) {
    http::ProgressFunctionArgs actual_args;
    actual_args.upload_speed = upload_speed.value_or(-1.0);
    actual_args.userdata = args.progress_userdata;
    // ignore the return value as we completed the upload
    args.progressfunc(actual_args);
  }
  return PutObjectResponse(std::move(*resp));
}

Result<ComposeObjectResponse> Client::ComposeObject(ComposeObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (args.sse != nullptr && args.sse->TlsRequired() && !base_url_.https) {
    return error::make<ComposeObjectResponse>(
        "SSE operation must be performed over a secure connection");
  }

  std::string upload_id;
  auto resp = ComposeObject(args, upload_id);
  if (!resp && !upload_id.empty()) {
    AbortMultipartUploadArgs amu_args;
    amu_args.bucket = args.bucket;
    amu_args.region = args.region;
    amu_args.object = args.object;
    amu_args.upload_id = upload_id;
    (void)AbortMultipartUpload(amu_args);
  }

  return resp;
}

Result<CopyObjectResponse> Client::CopyObject(CopyObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (args.sse != nullptr && args.sse->TlsRequired() && !base_url_.https) {
    return error::make<CopyObjectResponse>(
        "SSE operation must be performed over a secure connection");
  }

  if (args.source.ssec != nullptr && !base_url_.https) {
    return error::make<CopyObjectResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string etag;
  size_t size;
  {
    auto resp = StatObject(args.source);
    if (!resp) {
      return tl::make_unexpected(resp.error());
    }
    etag = resp->etag;
    size = resp->size;
  }

  if (args.source.offset.has_value() || args.source.length.has_value() ||
      size > utils::kMaxPartSize) {
    if (args.metadata_directive != nullptr &&
        *args.metadata_directive == Directive::kCopy) {
      return error::make<CopyObjectResponse>(
          "COPY metadata directive is not applicable to source object size "
          "greater than 5 GiB");
    }

    if (args.tagging_directive != nullptr &&
        *args.tagging_directive == Directive::kCopy) {
      return error::make<CopyObjectResponse>(
          "COPY tagging directive is not applicable to source object size "
          "greater than 5 GiB");
    }

    ComposeSource src;
    src.extra_headers = args.source.extra_headers;
    src.extra_query_params = args.source.extra_query_params;
    src.bucket = args.source.bucket;
    src.region = args.source.region;
    src.object = args.source.object;
    src.ssec = args.source.ssec;
    src.offset = args.source.offset;
    src.length = args.source.length;
    src.match_etag = args.source.match_etag;
    src.not_match_etag = args.source.not_match_etag;
    src.modified_since = args.source.modified_since;
    src.unmodified_since = args.source.unmodified_since;

    ComposeObjectArgs coargs;
    coargs.extra_headers = args.extra_headers;
    coargs.extra_query_params = args.extra_query_params;
    coargs.bucket = args.bucket;
    coargs.region = args.region;
    coargs.object = args.object;
    coargs.sse = args.sse;
    coargs.sources.push_back(src);

    auto co_result = ComposeObject(coargs);
    if (!co_result) return tl::make_unexpected(co_result.error());
    return CopyObjectResponse(std::move(*co_result));
  }

  utils::Multimap headers;
  headers.AddAll(args.extra_headers);
  headers.AddAll(args.Headers());
  if (args.metadata_directive != nullptr) {
    headers.Add("x-amz-metadata-directive",
                DirectiveToString(*args.metadata_directive));
  }
  if (args.tagging_directive != nullptr) {
    headers.Add("x-amz-tagging-directive",
                DirectiveToString(*args.tagging_directive));
  }
  headers.AddAll(args.source.CopyHeaders());

  std::string region;
  if (auto get_resp = GetRegion(args.bucket, args.region)) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.headers.AddAll(headers);

  auto response = Execute(req);
  if (!response) {
    return tl::make_unexpected(response.error());
  }

  CopyObjectResponse resp;
  resp.etag = utils::Trim(response->headers.GetFront("etag"), '"');
  resp.version_id = response->headers.GetFront("x-amz-version-id");

  return resp;
}

Result<DownloadObjectResponse> Client::DownloadObject(DownloadObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (args.ssec != nullptr && !base_url_.https) {
    return error::make<DownloadObjectResponse>(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string etag;
  {
    StatObjectArgs soargs;
    soargs.bucket = args.bucket;
    soargs.region = args.region;
    soargs.object = args.object;
    soargs.version_id = args.version_id;
    soargs.ssec = args.ssec;
    auto resp = StatObject(soargs);
    if (!resp) {
      return tl::make_unexpected(resp.error());
    }
    etag = resp->etag;
  }

  std::string temp_filename =
      args.filename + "." + httplib::decode_uri_component(etag) + ".part.minio";
  std::ofstream fout(temp_filename,
                     std::ios::trunc | std::ios::out | std::ios::binary);
  if (!fout.is_open()) {
    return error::make<DownloadObjectResponse>("unable to open file " +
                                               temp_filename);
  }

  std::string region;
  if (auto get_resp = GetRegion(args.bucket, args.region)) {
    region = get_resp->region;
  } else {
    return tl::make_unexpected(get_resp.error());
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.datafunc = [&fout = fout](http::DataFunctionArgs args) -> bool {
    fout << args.datachunk;
    return true;
  };
  req.progressfunc = args.progressfunc;
  req.progress_userdata = args.progress_userdata;

  auto response = Execute(req);
  fout.close();
  if (response) {
    std::filesystem::rename(temp_filename, args.filename);
    return DownloadObjectResponse(std::move(*response));
  }
  return tl::make_unexpected(response.error());
}

ListObjectsResult Client::ListObjects(ListObjectsArgs args) {
  if (error::Error err = args.Validate()) {
    return ListObjectsResult(err);
  }
  return ListObjectsResult(this, std::move(args));
}

Result<PutObjectResponse> Client::PutObject(PutObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  if (args.sse != nullptr && args.sse->TlsRequired() && !base_url_.https) {
    return error::make<PutObjectResponse>(
        "SSE operation must be performed over a secure connection");
  }

#ifdef MINIO_CPP_RDMA
  // Direct-buffer mode (was PutObject(PutObjectRDMAArgs)): caller supplied
  // a pre-allocated, page-aligned buffer. Try RDMA, fall back to a single
  // HTTP upload from the same buffer if RDMA declines.
  if (args.buf != nullptr) {
    std::string region;
    if (auto get_resp = GetRegion(args.bucket, args.region)) {
      region = get_resp->region;
    } else {
      return tl::make_unexpected(get_resp.error());
    }

    const size_t size = *args.size;

    // A buffer larger than one RDMA descriptor can describe
    // (kRDMAMaxMemoryRegSize) cannot be named to the server; skip straight
    // to the single HTTP PUT below.
    minio::rdma::Client& rdma_client = SharedRDMAClient();
    bool use_rdma =
        size <= kRDMAMaxMemoryRegSize && rdma_client.Register(args.buf, size);

    if (use_rdma) {
      minio::rdma::ClientCtx putCtx = {provider_, args.bucket,  args.object,
                                       {},        std::nullopt, {},
                                       base_url_, region};

      // RAII, matching the multipart paths below -- see the GET path for why
      // a manual Deregister after the call is not enough.
      ScopedRDMARegistration reg(&rdma_client, args.buf);

      ssize_t ret = rdmaPutWithRetry(&rdma_client, &putCtx, args.buf, size);

      if (ret > 0) {
        PutObjectResponse resp;
        resp.etag = putCtx.etag;
        return resp;
      }
      // ret < 0 / kRDMANotSupported: fall through to HTTP-from-buffer.
    }

    // HTTP fallback from the same buffer (single-shot, not multipart).
    // Skip body hashing for signing — caller's buffer may be GPU-resident,
    // and we'd otherwise drag device memory through OpenSSL just to compute
    // a hash that TLS already authenticates.
    //
    // For the same reason the upload cannot read the buffer directly: pubsetbuf
    // and the stream reads below are host loads, so a device pointer is staged
    // out to host memory first.
    const bool device_buf = IsDeviceBuffer(args.buf);
    std::vector<char> stage;
    char* src = args.buf;
    if (device_buf) {
      if (!GetCudaHostCopy().Ok()) {
        return error::make<PutObjectResponse>(
            "RDMA PUT failed and the HTTP fallback cannot reach device memory: "
            "libcuda.so.1 is unavailable");
      }
      ScopedCudaContext ctx(GetCudaHostCopy(), args.buf);
      if (!ctx.Ok()) {
        return error::make<PutObjectResponse>(
            "unable to establish a CUDA context for the HTTP fallback copy");
      }
      stage.resize(size);
      if (GetCudaHostCopy().dtoh(stage.data(),
                                 reinterpret_cast<unsigned long long>(args.buf),
                                 size) != 0) {
        return error::make<PutObjectResponse>(
            "unable to stage device memory to host for the HTTP fallback");
      }
      src = stage.data();
    }

    // Single PUT of the whole buffer via the request body — not multipart. The
    // buffer is already fully resident (host, or staged from device above), so
    // re-chunking it into parts buys nothing; AIStor accepts a single PUT up to
    // kMaxObjectSize (5 TiB), far beyond kRDMAMaxMemoryRegSize (the 4 GiB RDMA
    // registration ceiling that routed an oversized buffer here) and beyond
    // anything a client can pin or allocate.
    PutObjectApiArgs api_args;
    api_args.bucket = args.bucket;
    api_args.object = args.object;
    api_args.region = region;
    api_args.data = std::string_view(src, size);
    api_args.buf = src;
    api_args.size = size;
    api_args.headers.Add("x-amz-content-sha256", "UNSIGNED-PAYLOAD");
    return BaseClient::PutObject(api_args);
  }
#endif

  if (args.part_count.has_value() && *args.part_count == 1) {
    args.max_inflight_parts = 1;
  }

  // === Parallel multipart upload with bounded inflight ===
  unsigned int max_inflight = args.max_inflight_parts.value_or(1);
  if (max_inflight > 1) {
    // Clamp to a reasonable maximum and to part_count to prevent memory
    // exhaustion from untrusted input.
    constexpr unsigned int kMaxInflightParts = 100;
    if (max_inflight > kMaxInflightParts) {
      max_inflight = kMaxInflightParts;
    }
    if (args.part_count.has_value() && *args.part_count > 0 &&
        static_cast<size_t>(max_inflight) > *args.part_count) {
      max_inflight = static_cast<unsigned int>(*args.part_count);
    }

    size_t alloc_size =
        (args.part_count > 0) ? args.part_size : (args.part_size + 1);

    // Allocate pool of page-aligned buffers.
    std::vector<AlignedBuffer> buf_pool;
    for (unsigned int i = 0; i < max_inflight; i++) {
      void* raw = nullptr;
      if (AlignedAlloc(&raw, GetPageSize(), alloc_size)) {
        return error::make<PutObjectResponse>(
            "unable to allocate aligned buffer");
      }
      buf_pool.emplace_back(raw);
    }

#ifdef MINIO_CPP_RDMA
    // Register each pool buffer for RDMA, indexed by buffer slot.
    minio::rdma::Client& rdma_client = SharedRDMAClient();
    std::vector<ScopedRDMARegistration> rdma_regs(max_inflight);
    bool rdma_connected = rdma_client.Ready();
    if (rdma_connected) {
      for (unsigned int i = 0; i < max_inflight; i++) {
        char* pool_buf = static_cast<char*>(buf_pool[i].ptr);
        if (args.part_size <= kRDMAMaxMemoryRegSize &&
            rdma_client.Register(pool_buf, args.part_size)) {
          rdma_regs[i] = ScopedRDMARegistration(&rdma_client, pool_buf);
        }
      }
    }
#endif

    utils::Multimap headers = args.Headers();
    if (!headers.Contains("Content-Type")) {
      if (args.content_type.empty()) {
        headers.Add("Content-Type", "application/octet-stream");
      } else {
        headers.Add("Content-Type", args.content_type);
      }
    }

    std::optional<uint64_t> object_size = args.object_size;
    size_t part_size = args.part_size;
    size_t uploaded_size = 0;
    unsigned int part_number = 0;
    std::string one_byte;
    bool stop = false;
    std::list<Part> parts;
    std::optional<size_t> part_count = args.part_count;
    std::string upload_id;
    error::Error first_err;
    double uploaded_bytes = 0;           // for progress
    std::optional<double> upload_speed;  // for progress

    struct InflightPart {
      unsigned int part_number;
      std::string checksum_crc64nvme;
      size_t part_bytes;
      std::future<Result<UploadPartResponse>> future;
    };
    std::deque<InflightPart> inflight;

    unsigned int buf_idx = 0;

    auto report_progress = [&](size_t part_bytes) -> bool {
      if (args.progressfunc == nullptr) return true;
      uploaded_bytes += static_cast<double>(part_bytes);
      http::ProgressFunctionArgs actual_args;
      actual_args.upload_total_bytes =
          object_size ? static_cast<double>(*object_size) : -1.0;
      actual_args.uploaded_bytes = uploaded_bytes;
      actual_args.userdata = args.progress_userdata;
      if (!args.progressfunc(actual_args)) {
        first_err = error::Error("aborted by progress function");
        return false;
      }
      return true;
    };

    auto read_part_data = [&](char* buf, size_t& bytes_read) -> error::Error {
      if (part_count.has_value()) {
        if (part_number == *part_count) {
          part_size = *object_size - uploaded_size;
          stop = true;
        }

        if (error::Error err =
                utils::ReadPart(*args.stream, buf, part_size, bytes_read)) {
          return err;
        }

        if (bytes_read != part_size) {
          return error::Error("not enough data in the stream; expected: " +
                              std::to_string(part_size) + ", got: " +
                              std::to_string(bytes_read) + " bytes");
        }
      } else {
        char* b = buf;
        size_t size = part_size + 1;

        if (!one_byte.empty()) {
          buf[0] = one_byte.front();
          b = buf + 1;
          size--;
          bytes_read = 1;
          one_byte = "";
        }

        size_t n = 0;
        if (error::Error err = utils::ReadPart(*args.stream, b, size, n)) {
          return err;
        }

        bytes_read += n;

        // If bytes read is less than or equals to part size, then we have
        // reached last part.
        if (bytes_read <= part_size) {
          part_count = std::optional<size_t>(part_number);
          part_size = bytes_read;
          stop = true;
        } else {
          one_byte = buf[part_size];
        }
      }
      return error::Error();
    };

    while (!stop && !first_err) {
      part_number++;

      // Wait for a slot before reusing the buffer at buf_idx.
      if (inflight.size() >= max_inflight) {
        InflightPart ip = std::move(inflight.front());
        inflight.pop_front();
        auto up_resp = ip.future.get();
        if (!up_resp) {
          first_err = up_resp.error();
          break;
        }
        parts.push_back(Part(ip.part_number, std::move(up_resp->etag),
                             std::move(ip.checksum_crc64nvme)));
        if (!report_progress(ip.part_bytes)) {
          break;
        }
      }

      char* buf = static_cast<char*>(buf_pool[buf_idx].ptr);
      size_t bytes_read = 0;
      if (error::Error err = read_part_data(buf, bytes_read)) {
        first_err = err;
        break;
      }

      uploaded_size += part_size;

      // Single part discovered dynamically: use PutObject directly.
      if (part_count.has_value() && *part_count == 1) {
        PutObjectApiArgs api_args;
        api_args.extra_query_params = args.extra_query_params;
        api_args.bucket = args.bucket;
        api_args.region = args.region;
        api_args.object = args.object;
        api_args.data = std::string_view(buf, part_size);
        api_args.buf = buf;
        api_args.size = part_size;
        api_args.progressfunc = args.progressfunc;
        api_args.progress_userdata = args.progress_userdata;
        api_args.headers = headers;
        return BaseClient::PutObject(api_args);
      }

      // Create multipart upload on first part.
      if (upload_id.empty()) {
        CreateMultipartUploadArgs cmu_args;
        cmu_args.extra_query_params = args.extra_query_params;
        cmu_args.bucket = args.bucket;
        cmu_args.region = args.region;
        cmu_args.object = args.object;
        cmu_args.headers = headers;
#ifdef MINIO_CPP_RDMA
        cmu_args.headers.Add("x-amz-checksum-algorithm", "CRC64NVME");
#endif
        if (auto cmu_resp = CreateMultipartUpload(cmu_args)) {
          upload_id = cmu_resp->upload_id;
        } else {
          first_err = cmu_resp.error();
          break;
        }
      }

      // Build UploadPartArgs and dispatch via std::async.
      UploadPartArgs up_args;
      up_args.bucket = args.bucket;
      up_args.region = args.region;
      up_args.object = args.object;
      up_args.upload_id = upload_id;
      up_args.part_number = part_number;
      up_args.data = std::string_view(buf, part_size);
      up_args.buf = buf;
      up_args.part_size = part_size;
#ifdef MINIO_CPP_RDMA
      if (rdma_regs[buf_idx].client != nullptr) {
        up_args.rdmaclient = &rdma_client;
      }
      if (buf != nullptr && minio::rdma::Client::GetMemoryType(buf) ==
                                minio::rdma::MemoryType::kSystem) {
        const std::string crc = utils::Crc64NvmeBase64(buf, part_size);
        up_args.checksum_crc64nvme = crc;
        up_args.headers.Add("x-amz-checksum-crc64nvme", crc);
      }
#endif
      if (headers.Contains("x-amz-content-sha256")) {
        up_args.headers.Add("x-amz-content-sha256",
                            headers.GetFront("x-amz-content-sha256"));
      }
      if (args.sse != nullptr) {
        if (SseCustomerKey* ssec = dynamic_cast<SseCustomerKey*>(args.sse)) {
          up_args.headers.AddAll(ssec->Headers());
        }
      }

      InflightPart ip;
      ip.part_number = part_number;
      ip.checksum_crc64nvme = up_args.checksum_crc64nvme;
      ip.part_bytes = part_size;
      try {
        ip.future = std::async(std::launch::async, [this, up_args]() {
          return UploadPart(up_args);
        });
      } catch (const std::system_error& e) {
        first_err =
            error::Error(std::string("unable to create thread: ") + e.what());
        break;
      }
      inflight.push_back(std::move(ip));

      buf_idx = (buf_idx + 1) % max_inflight;
    }

    // Drain all inflight parts.
    while (!inflight.empty()) {
      InflightPart ip = std::move(inflight.front());
      inflight.pop_front();
      auto drain_resp = ip.future.get();
      if (!drain_resp) {
        if (!first_err) first_err = drain_resp.error();
        continue;
      }
      parts.push_back(Part(ip.part_number, std::move(drain_resp->etag),
                           std::move(ip.checksum_crc64nvme)));
      if (!first_err) report_progress(ip.part_bytes);
    }

    if (first_err) {
      if (!upload_id.empty()) {
        AbortMultipartUploadArgs amu_args;
        amu_args.bucket = std::move(args.bucket);
        amu_args.region = std::move(args.region);
        amu_args.object = std::move(args.object);
        amu_args.upload_id = upload_id;
        (void)AbortMultipartUpload(amu_args);
      }
      return tl::make_unexpected(first_err);
    }

    // Complete multipart upload.
    CompleteMultipartUploadArgs cmu_args;
    cmu_args.bucket = args.bucket;
    cmu_args.region = args.region;
    cmu_args.object = args.object;
    cmu_args.upload_id = upload_id;
    cmu_args.parts = parts;
    auto cmu_resp = CompleteMultipartUpload(cmu_args);
    if (cmu_resp && args.progressfunc != nullptr) {
      http::ProgressFunctionArgs actual_args;
      actual_args.upload_speed = upload_speed.value_or(-1.0);
      actual_args.userdata = args.progress_userdata;
      args.progressfunc(actual_args);
    }
    if (!cmu_resp && !upload_id.empty()) {
      AbortMultipartUploadArgs amu_args;
      amu_args.bucket = std::move(args.bucket);
      amu_args.region = std::move(args.region);
      amu_args.object = std::move(args.object);
      amu_args.upload_id = upload_id;
      (void)AbortMultipartUpload(amu_args);
    }
    if (!cmu_resp) {
      return tl::make_unexpected(cmu_resp.error());
    }
    return PutObjectResponse(std::move(*cmu_resp));
  }

  void* raw = nullptr;
  if (AlignedAlloc(
          &raw, GetPageSize(),
          (args.part_count > 0) ? args.part_size : args.part_size + 1)) {
    return error::make<PutObjectResponse>(
        "unable to allocate system memory with alignment");
  }
  AlignedBuffer aligned_buf(raw);
  char* buf = static_cast<char*>(aligned_buf.ptr);

#ifdef MINIO_CPP_RDMA
  // Reuse the process-wide SharedRDMAClient() rather than constructing
  // per-call; see client.h. `buf` here is the
  // multipart part buffer, registered once for the whole multipart
  // upload and deregistered when scope exits.
  //
  // If the device is present but declines to register this buffer (e.g.
  // nvidia_peermem.ko not loaded for a GPU buffer, or the HCA is unhealthy),
  // fall back to the plain HTTP multipart path instead of failing the whole
  // call — BaseClient::PutObject keys off args.rdmaclient being non-null to
  // even attempt the RDMA path.
  minio::rdma::Client& rdma_client = SharedRDMAClient();
  ScopedRDMARegistration rdma_reg;
  if (rdma_client.Ready() && args.part_size <= kRDMAMaxMemoryRegSize &&
      rdma_client.Register(buf, args.part_size)) {
    rdma_reg = ScopedRDMARegistration(&rdma_client, buf);
    args.rdmaclient = &rdma_client;
  }
#endif

  std::string upload_id;
  auto resp = PutObject(args, upload_id, buf);

  if (!resp && !upload_id.empty()) {
    AbortMultipartUploadArgs amu_args;
    amu_args.bucket = std::move(args.bucket);
    amu_args.region = std::move(args.region);
    amu_args.object = std::move(args.object);
    amu_args.upload_id = upload_id;
    (void)AbortMultipartUpload(amu_args);
  }

  return resp;
}

Result<UploadObjectResponse> Client::UploadObject(UploadObjectArgs args) {
  if (error::Error err = args.Validate()) {
    return tl::make_unexpected(err);
  }

  std::ifstream file;
  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  try {
    file.open(args.filename);
  } catch (std::system_error& err) {
    return error::make<UploadObjectResponse>(
        "unable to open file " + args.filename + "; " + err.code().message());
  }

  PutObjectArgs po_args(file, args.object_size, 0);
  po_args.extra_headers = std::move(args.extra_headers);
  po_args.extra_query_params = std::move(args.extra_query_params);
  po_args.bucket = std::move(args.bucket);
  po_args.region = std::move(args.region);
  po_args.object = std::move(args.object);
  po_args.headers = std::move(args.headers);
  po_args.user_metadata = std::move(args.user_metadata);
  po_args.sse = std::move(args.sse);
  po_args.tags = std::move(args.tags);
  po_args.retention = std::move(args.retention);
  po_args.legal_hold = std::move(args.legal_hold);
  po_args.content_type = std::move(args.content_type);
  po_args.progressfunc = std::move(args.progressfunc);
  po_args.progress_userdata = std::move(args.progress_userdata);

  auto resp = PutObject(std::move(po_args));
  file.close();
  if (resp) {
    return UploadObjectResponse(std::move(*resp));
  }
  return tl::make_unexpected(resp.error());
}

RemoveObjectsResult Client::RemoveObjects(RemoveObjectsArgs args) {
  if (error::Error err = args.Validate()) {
    return RemoveObjectsResult(err);
  }
  return RemoveObjectsResult(this, std::move(args));
}

// ---- Async overloads ----

std::future<Result<ComposeObjectResponse>> Client::ComposeObjectAsync(
    ComposeObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return ComposeObject(std::move(args));
                    });
}

std::future<Result<CopyObjectResponse>> Client::CopyObjectAsync(
    CopyObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return CopyObject(std::move(args));
                    });
}

std::future<Result<DownloadObjectResponse>> Client::DownloadObjectAsync(
    DownloadObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return DownloadObject(std::move(args));
                    });
}

std::future<Result<GetObjectResponse>> Client::GetObjectAsync(
    GetObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return Client::GetObject(std::move(args));
                    });
}

std::future<ListObjectsResult> Client::ListObjectsAsync(ListObjectsArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return ListObjects(std::move(args));
                    });
}

std::future<Result<PutObjectResponse>> Client::PutObjectAsync(
    PutObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return PutObject(std::move(args));
                    });
}

std::future<Result<UploadObjectResponse>> Client::UploadObjectAsync(
    UploadObjectArgs args) {
  return std::async(std::launch::async,
                    [this, args = std::move(args)]() mutable {
                      return UploadObject(std::move(args));
                    });
}

}  // namespace minio::s3
