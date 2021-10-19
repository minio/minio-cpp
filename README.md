> NOTE: This project is a work in progress

# MinIO C++ Client SDK for Amazon S3 Compatible Cloud Storage [![Slack](https://slack.min.io/slack?type=svg)](https://slack.min.io) [![Sourcegraph](https://sourcegraph.com/github.com/minio/minio-cpp/-/badge.svg)](https://sourcegraph.com/github.com/minio/minio-cpp?badge) [![Apache V2 License](https://img.shields.io/badge/license-Apache%20V2-blue.svg)](https://github.com/minio/minio-cpp/blob/master/LICENSE)

The MinIO C++ Client SDK provides simple APIs to access any Amazon S3 compatible object storage.

This quickstart guide will show you how to install the MinIO client SDK, connect to MinIO, and provide a walkthrough for a simple file uploader. For a complete list of APIs and examples, please take a look at the [C++ Client API Reference](https://docs.min.io/docs/cpp-client-api-reference).

This document assumes that you have a working C++ development environment.

> NOTE: This library is based on original work https://github.com/cjameshuff/s3tools, but has been completely re-implemented since then.

## Build Instructions
In order to build this project, you need the Cross-Platform Make CMake 3.10 or higher. You can download it from http://www.cmake.org/. In order to build **miniocpp** you need to have the following libraries and their development headers installed.

- libcurl-dev
- openssl1.1.x 

```
git clone https://github.com/minio/minio-cpp
cd minio-cpp; git submodule update; mkdir build; cmake ../;
make -j4
```
