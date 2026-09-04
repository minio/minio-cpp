module;

#include <miniocpp/client.h>

export module minio;

// Re-exporting the public API through the module requires an EXPLICIT using-
// declaration per symbol (using ns::Symbol;) - not a using-directive (using
// namespace ns;), because declarations from the global module fragment (the
// #include above) only become "reachable", not automatic members of an export
// namespace. The list below covers what a typical consumer needs today -
// extend it by adding more using-declarations as needed.
export namespace minio::s3 {
using s3::BaseUrl;
using s3::BucketExistsArgs;
using s3::BucketExistsResponse;
using s3::Client;
}  // namespace minio::s3

export namespace minio::creds {
using minio::creds::StaticProvider;
}

export namespace minio {
using minio::Result;
}
