# Threading and concurrency

This page describes the concurrency model of `minio::s3::Client`: what you may
do from several threads at once, what the SDK synchronizes for you, and which
rules remain on your side.

## Summary

Create **one** `minio::s3::Client` and share it. Every bucket/object operation
— sync or `*Async` — may be called from any number of threads on the same
instance. Create the client, configure it, then hand it to your worker threads.

Three rules cover everything that is *not* the SDK's job:

1. Finish configuration before concurrent use. `Debug()`,
   `IgnoreCertCheck()`, `SetSslCertFile()` and `SetAppInfo()` are not
   synchronized; call them while the client is still single-threaded.
2. Data and progress callbacks may run on a thread other than the one that
   started the request (see below). Make them thread-safe.
3. Do not share one call's arguments or result iterators across in-flight
   calls: `ListObjectsResult` and `RemoveObjectsResult` are iterators, not
   thread-safe cursors. Pass `Args` by value; they are copied into each call.

## What the SDK synchronizes

- **Per-call state.** Every operation builds its `Request`/`Response` on the
  calling thread's stack (or copies the args into the async task). Nothing
  request-scoped is shared between calls.
- **Bucket region cache.** `BaseClient` caches the region of each bucket it
  has resolved; the map is guarded by a shared mutex, so concurrent calls may
  look up regions freely. A region not yet cached is resolved with one extra
  request — concurrent first calls to the same bucket may race to issue it;
  they all succeed, one result wins.
- **Credentials.** Each S3 request asks the credential provider for keys via
  `Fetch()`. Note that STS-based providers (AssumeRole, WebIdentity/
  ClientGrants, IamAws, LdapIdentity, certificate and custom-token providers)
  fetch **once** and then cache the credentials for the provider's lifetime;
  they are not refreshed on expiry. A `StaticProvider` — the common case — has
  nothing to refresh.
- **HTTP connections.** `minio::http::Client` may be called from several
  threads at once: every request builds its own `httplib::Client`, since that
  type is not thread-safe. No two in-flight requests share a socket or wait on
  another thread's transfer.

## Callback threading

Each request owns its thread for the whole transfer, so your `datafunc` and
`progressfunc` run on the calling thread.

## Async overloads

Every `*Async` method returns a `std::future` (or `std::shared_future` for
`ListObjectsAsync`) backed by `std::async`. The standard does not guarantee
that tasks run on pooled threads — with libstdc++ each task gets a fresh
thread — so do not build per-thread assumptions on top of them.

Lifetime rules for async calls:

- The stream passed in `PutObjectArgs`/`GetObjectArgs` must outlive the
  returned future, exactly as it must outlive the synchronous call.
- The future owns the task; dropping it blocks until the task finishes
  (`std::async` semantics).

## RDMA

`minio::rdma::Shared()` is a process-wide singleton (thread-safe
initialization). Buffer registration is reference-counted per address, so
concurrent `Register`/`Deregister`/`GetToken` calls for the same buffer pin it
exactly once. RDMA transfers that the server declines fall back to HTTP and
follow all the rules above.
