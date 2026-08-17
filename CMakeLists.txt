# MinIO C++ Library for Amazon S3 Compatible Cloud Storage
# Copyright 2021-2024 MinIO, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.4)
if (POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# Minio C++ Project
# -----------------

set(MINIO_CPP_MAJOR_VERSION "0")
set(MINIO_CPP_MINOR_VERSION "6")
set(MINIO_CPP_PATCH_VERSION "0")
set(MINIO_CPP_VERSION_STRING "${MINIO_CPP_MAJOR_VERSION}.${MINIO_CPP_MINOR_VERSION}.${MINIO_CPP_PATCH_VERSION}")

project(miniocpp
  DESCRIPTION "MinIO C++ Client SDK provides simple APIs to access S3 compatible object storage"
  VERSION ${MINIO_CPP_VERSION_STRING}
  LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CheckIncludeFiles)
include(CMakePackageConfigHelpers)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

option(MINIO_CPP_TEST "Build tests" OFF)
option(MINIO_CPP_MAKE_DOC "Build documentation" OFF)
# RDMA support. OFF by default so consumers with no RDMA hardware do not
# need the library on the host. When ON, links against the vendored
# libs3rdma in vendor/s3rdma/ and defines MINIO_CPP_RDMA so headers expose
# the GetObjectRDMAArgs / PutObjectRDMAArgs API surface.
option(MINIO_CPP_ENABLE_RDMA "Enable RDMA support" OFF)

set(MINIO_CPP_CFLAGS)
set(MINIO_CPP_INCLUDES)
set(MINIO_CPP_LIBS)
set(MINIO_CPP_STD "17" CACHE STRING "C++ standard (17 or 20)")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC" OR "x${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" STREQUAL "xMSVC")
  # MSVC
else()
  # GCC/Clang/AppleClang/...
  list(APPEND MINIO_CPP_CFLAGS -Wall -Wextra -Wconversion)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 8.0 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0 AND NOT MINGW)
    list(APPEND MINIO_CPP_LIBS stdc++fs)
  endif()
endif()

if (MINIO_CPP_ENABLE_RDMA)
  if (WIN32 OR APPLE)
    message(FATAL_ERROR "MINIO_CPP_ENABLE_RDMA is only supported on Linux")
  endif()
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(MINIO_CPP_RDMA_ARCH "x86_64")
  elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(MINIO_CPP_RDMA_ARCH "aarch64")
  else()
    message(FATAL_ERROR "MINIO_CPP_ENABLE_RDMA: unsupported CMAKE_SYSTEM_PROCESSOR '${CMAKE_SYSTEM_PROCESSOR}'")
  endif()
  set(MINIO_CPP_RDMA_LIBDIR "${CMAKE_SOURCE_DIR}/vendor/s3rdma/lib/${MINIO_CPP_RDMA_ARCH}")
  if (NOT EXISTS "${MINIO_CPP_RDMA_LIBDIR}/libs3rdma.so")
    message(FATAL_ERROR "MINIO_CPP_ENABLE_RDMA: libs3rdma not vendored for ${MINIO_CPP_RDMA_ARCH} at ${MINIO_CPP_RDMA_LIBDIR}. Run ./build-libs.sh in the s3rdma repo to build and vendor both arches.")
  endif()
  # libs3rdma resolves the RDMA stack itself, at runtime, so nothing else is
  # linked here and the SDK builds on a host with no RDMA packages installed.
  #
  # No -rpath here: this list is exported to consumers, and a source-tree path
  # baked into an installed library only resolves on the machine that built it.
  # The rpath is set per-target below, build tree and install tree separately.
  list(APPEND MINIO_CPP_INCLUDES ${CMAKE_SOURCE_DIR}/vendor/s3rdma/include)
  list(APPEND MINIO_CPP_LIBS "-Bdynamic -L${MINIO_CPP_RDMA_LIBDIR} -ls3rdma")
endif()

# Dependencies
# ------------
#
# Resolution (vcpkg -> pkg-config -> upstream source) lives in
# cmake/miniocpp-deps.cmake, shared with the installed miniocpp-config.cmake.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/miniocpp-deps.cmake)

list(APPEND MINIO_CPP_LIBS ${MINIO_CPP_DEPS_LINK_LIBS})

if (WIN32)
  list(APPEND MINIO_CPP_LIBS wsock32)
  list(APPEND MINIO_CPP_LIBS ws2_32)
endif()

# Minio C++ Library
# -----------------

set(MINIO_CPP_SOURCES
  src/args.cc
  src/baseclient.cc
  src/client.cc
  src/credentials.cc
  src/error.cc
  src/http.cc
  src/providers.cc
  src/request.cc
  src/response.cc
  src/select.cc
  src/signer.cc
  src/sse.cc
  src/types.cc
  src/utils.cc
)

set(MINIO_CPP_HEADERS
  include/miniocpp/args.h
  include/miniocpp/baseclient.h
  include/miniocpp/client.h
  include/miniocpp/config.h
  include/miniocpp/credentials.h
  include/miniocpp/error.h
  include/miniocpp/http.h
  include/miniocpp/providers.h
  include/miniocpp/request.h
  include/miniocpp/response.h
  include/miniocpp/result.h
  include/miniocpp/select.h
  include/miniocpp/signer.h
  include/miniocpp/sse.h
  include/miniocpp/types.h
  include/miniocpp/utils.h
)

if (MINIO_CPP_ENABLE_RDMA)
  list(APPEND MINIO_CPP_HEADERS
    include/miniocpp/c_api.h
    include/miniocpp/rdma.h
    include/miniocpp/rdma_client.h
  )
  list(APPEND MINIO_CPP_SOURCES src/c_api.cc)
endif()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

IF (BUILD_SHARED_LIBS)
  IF (WIN32)
    message(FATAL_ERROR "Unable to build shared library on Windows yet, this library lacks decorator support.")
  ELSE ()
    add_library(miniocpp SHARED ${MINIO_CPP_SOURCES} ${MINIO_CPP_HEADERS})
  ENDIF ()
ELSE ()
  add_library(miniocpp STATIC ${MINIO_CPP_SOURCES} ${MINIO_CPP_HEADERS})
ENDIF ()

target_compile_options(miniocpp PRIVATE ${MINIO_CPP_CFLAGS})
target_compile_features(miniocpp PUBLIC cxx_std_${MINIO_CPP_STD})
target_include_directories(miniocpp PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<BUILD_INTERFACE:${MINIO_CPP_INCLUDES}>
)
target_link_libraries(miniocpp PUBLIC ${MINIO_CPP_LIBS})
if (MINIO_CPP_ENABLE_RDMA)
  target_compile_definitions(miniocpp PUBLIC MINIO_CPP_RDMA)
endif()
set_target_properties(miniocpp PROPERTIES VERSION "${MINIO_CPP_VERSION_STRING}")
set_target_properties(miniocpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Add a cmake alias - this is how users should use minio-cpp in their cmake projects.
add_library(miniocpp::miniocpp ALIAS miniocpp)

# Minio C++ Tests
# ---------------

if (MINIO_CPP_TEST)
  set(EXAMPLE_APPS
    MakeBucket
    RemoveBucket
    BucketExists
    ListBuckets
    StatObject
    RemoveObject
    DownloadObject
    UploadObject
    GetObject
    ListObjects
    PutObject
    CopyObject
    ComposeObject
    RemoveObjects
    SelectObjectContent
    ListenBucketNotification
    DeleteBucketPolicy
    GetBucketPolicy
    SetBucketPolicy
    DeleteBucketNotification
    GetBucketNotification
    SetBucketNotification
    DeleteBucketEncryption
    GetBucketEncryption
    SetBucketEncryption
    GetBucketVersioning
    SetBucketVersioning
    DeleteBucketReplication
    GetBucketReplication
    SetBucketReplication
    DeleteBucketLifecycle
    GetBucketLifecycle
    SetBucketLifecycle
    DeleteBucketTags
    GetBucketTags
    SetBucketTags
    DeleteObjectLockConfig
    GetObjectLockConfig
    SetObjectLockConfig
    DeleteObjectTags
    GetObjectTags
    SetObjectTags
    DisableObjectLegalHold
    EnableObjectLegalHold
    IsObjectLegalHoldEnabled
    GetObjectRetention
    SetObjectRetention
    GetPresignedObjectUrl
    GetPresignedPostFormData
    PutObjectProgress
    GetObjectProgress
  )

  if (MINIO_CPP_ENABLE_RDMA)
    list(APPEND EXAMPLE_APPS GetPutRDMA GPUHostDisk)
  endif()

  foreach(target ${EXAMPLE_APPS})
    add_executable(${target} examples/${target}.cc)
    target_compile_features(${target} PUBLIC cxx_std_${MINIO_CPP_STD})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
    target_link_libraries(${target} PRIVATE miniocpp::miniocpp ${MINIO_CPP_LIBS})
  endforeach()

  add_executable(tests tests/tests.cc)
  target_compile_features(tests PUBLIC cxx_std_${MINIO_CPP_STD})
  target_include_directories(tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
  target_link_libraries(tests miniocpp ${MINIO_CPP_LIBS})
endif()

# Minio C++ Documentation
# -----------------------

if (MINIO_CPP_MAKE_DOC)
  # check if Doxygen is installed
  find_package(Doxygen)
  if (DOXYGEN_FOUND)
    # set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

    # note the option ALL which allows to build the docs together with the application
    add_custom_target(doc_doxygen ALL
      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM )
  else()
    message("Doxygen need to be installed to generate the doxygen documentation")
  endif()
endif()

# Installation Instructions
# -------------------------

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/miniocpp-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/miniocpp-config.cmake"
  INSTALL_DESTINATION
    "{CMAKE_INSTALL_LIBDIR}/cmake/miniocpp"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

if (MINIO_CPP_ENABLE_RDMA)
  # Ship the vendored transport alongside the library and resolve it relative
  # to whatever installed thing loads it, so an installed libminiocpp does not
  # depend on this source tree still being present. Both names go in: the
  # soname is what the loader asks for, the link name is what -ls3rdma finds.
  install(FILES
          "${MINIO_CPP_RDMA_LIBDIR}/libs3rdma.so.0"
          "${MINIO_CPP_RDMA_LIBDIR}/libs3rdma.so"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  set_target_properties(miniocpp PROPERTIES
                        BUILD_RPATH "${MINIO_CPP_RDMA_LIBDIR}"
                        INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS miniocpp
        EXPORT miniocpp-targets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# Source-built deps must be in an export set for install(EXPORT) to resolve
# them (curlpp exports itself; miniocpp_inih does not).
if (MINIO_CPP_DEPS_EXPORT_TARGETS)
  install(TARGETS ${MINIO_CPP_DEPS_EXPORT_TARGETS}
          EXPORT miniocpp-targets
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

# The link interface is not exported; miniocpp-config.cmake re-applies it.
install(EXPORT miniocpp-targets
        NAMESPACE miniocpp::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/miniocpp")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/miniocpp-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/miniocpp")

# The consumer config includes the dependency resolver.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/miniocpp-deps.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/miniocpp")

install(FILES
        ${MINIO_CPP_HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/miniocpp")

# Vendored headers referenced by public miniocpp headers must ship too.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/tl"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

configure_file(miniocpp.pc.in ${CMAKE_CURRENT_BINARY_DIR}/miniocpp.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/miniocpp.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
