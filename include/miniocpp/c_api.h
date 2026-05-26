// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2026 MinIO, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0
//
// Stable C ABI for libminiocpp. Provided so language bindings (minio-go,
// minio-py, etc.) can call a single shared library instead of vendoring
// per-language C++ glue. The API is intentionally minimal: one constructor,
// one destructor, two object-IO functions, an aligned-buffer allocator pair,
// and an error accessor. Both put and get are unified — passing `buf` selects
// the RDMA / direct-buffer path, omitting it selects callback-streaming.

#ifndef MINIO_CPP_C_API_H_INCLUDED
#define MINIO_CPP_C_API_H_INCLUDED

#ifdef MINIO_CPP_RDMA

#include <stddef.h>
#include <sys/types.h>  // ssize_t

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MINIOCPP_API __attribute__((visibility("default")))
#else
#define MINIOCPP_API
#endif

typedef struct miniocpp_client miniocpp_client;

// Error codes returned by miniocpp_put_object / miniocpp_get_object.
// >=0 indicates success and is the byte count actually transferred.
#define MINIOCPP_ERR_GENERIC (-1)  // error; call miniocpp_last_error()
#define MINIOCPP_ERR_RDMA_DECLINED (-2)
#define MINIOCPP_ERR_INVALID_ARG (-3)

// Streaming-source callback (PUT). Called repeatedly to fill `buf` with up to
// `size` bytes. Return the number of bytes written, 0 on EOF, or a negative
// value to abort the upload.
typedef ssize_t (*miniocpp_read_cb)(void* userdata, char* buf, size_t size);

// Streaming-sink callback (GET). Called repeatedly with the next chunk.
// Return the number of bytes consumed (must equal `size` to continue), or a
// negative value to abort the download.
typedef ssize_t (*miniocpp_write_cb)(void* userdata, const char* buf,
                                     size_t size);

// Construct a client. region/session_token may be NULL/"".
// Returned pointer is owned by the caller; release with miniocpp_client_free.
MINIOCPP_API miniocpp_client* miniocpp_client_new(
    const char* endpoint, const char* region, const char* access_key,
    const char* secret_key, const char* session_token, int use_https);

MINIOCPP_API void miniocpp_client_free(miniocpp_client* client);

// PUT. When `buf` is non-NULL, attempts RDMA with HTTP-from-buf fallback;
// `read_cb` is ignored. When `buf` is NULL, streams from `read_cb`.
// Writes the resulting ETag (max 63 chars + NUL) into etag_out if non-NULL.
// Writes the CRC64NVME checksum (base64, ~32 chars) into checksum_out if
// non-NULL. Returns bytes transferred on success, or one of MINIOCPP_ERR_*.
MINIOCPP_API ssize_t miniocpp_put_object(miniocpp_client* client,
                                         const char* bucket, const char* object,
                                         void* buf, size_t size,
                                         miniocpp_read_cb read_cb,
                                         void* userdata, char etag_out[64],
                                         char checksum_out[64]);

// GET. When `buf` is non-NULL, attempts RDMA into the caller's buffer with
// HTTP-into-buf fallback; `write_cb` is ignored. When `buf` is NULL, streams
// the body through `write_cb`. Returns bytes transferred or MINIOCPP_ERR_*.
MINIOCPP_API ssize_t miniocpp_get_object(miniocpp_client* client,
                                         const char* bucket, const char* object,
                                         void* buf, size_t size,
                                         miniocpp_write_cb write_cb,
                                         void* userdata);

// Page-aligned host allocator suitable for RDMA registration. Caller must
// release with miniocpp_free_aligned. Returns NULL on allocation failure.
MINIOCPP_API void* miniocpp_alloc_aligned(size_t size);
MINIOCPP_API void miniocpp_free_aligned(void* ptr);

// Returns 1 if the process-wide cuObjClient is currently connected to a
// cuObjServer (i.e. an RDMA transfer is likely to succeed), 0 otherwise.
// Safe to call before any IO. Does not require an existing miniocpp_client.
MINIOCPP_API int miniocpp_rdma_available(void);

// Thread-local last-error message from the most recent failing call on the
// calling thread. Returns NULL when no error has been recorded. The returned
// pointer is owned by the library and stable until the next failing call on
// this thread.
MINIOCPP_API const char* miniocpp_last_error(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MINIO_CPP_RDMA
#endif  // MINIO_CPP_C_API_H_INCLUDED
