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
  # miniocpp.pc.in has no visibility into target_link_libraries() - without this,
  # pkg-config consumers (non-CMake) are missing wsock32/ws2_32 at link time
  # (getaddrinfo, select, ...).
  set(MINIO_CPP_PC_EXTRA_LIBS " -lwsock32 -lws2_32")
endif()
if (APPLE)
  # httplib loads macOS system certificates from the Keychain
  # (SecTrustSettingsCopyCertificates); the vcpkg httplib target does not
  # link the Security framework, so add it here.
  list(APPEND MINIO_CPP_LIBS
    "-framework CFNetwork" "-framework CoreFoundation" "-framework Security")
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

# Optional C++20 module interface for the public API (modules/minio.cc).
# FILE_SET CXX_MODULES (CMake 3.28+) attaches it as a real part of the miniocpp
# target's build - the compiler gets whatever flags it needs for a module
# interface unit (e.g. /interface for MSVC) based on this declaration, not on
# the file's extension, so any CMake/MSBuild/Meson (via cmake.subproject())
# consumer of this target picks up `import minio;` automatically. Requires
# C++20 (MINIO_CPP_STD=20); silently skipped otherwise for older setups.
#
# CMake's module dependency scanning is only implemented for the Ninja,
# Ninja Multi-Config, and Visual Studio (17.4+) generators - with any other
# generator (e.g. the Linux/macOS default "Unix Makefiles") CMake hard-errors
# at the Generate step as soon as it sees FILE_SET CXX_MODULES sources, even
# if MINIO_CPP_STD=20 was requested intentionally. Generator *name* alone is
# not enough either: CMake 3.28 requires Ninja 1.11+ for the dyndep-based
# scanning, and each compiler has its own minimum version for module support
# (MSVC 14.34 / VS 17.4, reported as compiler version 19.34; Clang 16;
# GCC 14) - see https://cmake.org/cmake/help/v3.28/manual/cmake-cxxmodules.7.html.
# An older or unrecognized compiler can still match the generator check
# above and then break module dependency scanning instead of falling back
# cleanly, so reject anything not explicitly known-good rather than
# defaulting to allowed.
set(MINIO_CPP_HAS_CXX_MODULE OFF)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.28" AND MINIO_CPP_STD STREQUAL "20")
  set(MINIO_CPP_MODULE_TOOLCHAIN_OK OFF)
  if (CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
    execute_process(
      COMMAND "${CMAKE_MAKE_PROGRAM}" --version
      OUTPUT_VARIABLE MINIO_CPP_NINJA_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if (MINIO_CPP_NINJA_VERSION VERSION_GREATER_EQUAL "1.11")
      set(MINIO_CPP_MODULE_TOOLCHAIN_OK ON)
    endif()
  elseif (CMAKE_GENERATOR MATCHES "^Visual Studio ")
    set(MINIO_CPP_MODULE_TOOLCHAIN_OK ON)
  endif()

  # Applies regardless of which of the generators above is in use - Ninja can
  # also drive an MSVC/Clang/GCC toolchain, and each compiler's own module
  # support is gated on its own version, not the generator. Reject any
  # compiler not explicitly listed here, rather than silently allowing it.
  if (MINIO_CPP_MODULE_TOOLCHAIN_OK)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
      if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.34")
        set(MINIO_CPP_MODULE_TOOLCHAIN_OK OFF)
      endif()
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
        set(MINIO_CPP_MODULE_TOOLCHAIN_OK OFF)
      endif()
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14")
        set(MINIO_CPP_MODULE_TOOLCHAIN_OK OFF)
      endif()
    else()
      set(MINIO_CPP_MODULE_TOOLCHAIN_OK OFF)
    endif()
  endif()

  if (MINIO_CPP_MODULE_TOOLCHAIN_OK)
    set(MINIO_CPP_HAS_CXX_MODULE ON)
    target_sources(miniocpp
      PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS modules
        FILES modules/minio.cc
    )
  endif()
endif()

# httplib's class layout depends on CPPHTTPLIB_OPENSSL_SUPPORT; define it once
# target-wide so every translation unit compiles httplib.h identically.
target_compile_definitions(miniocpp PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
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

if (MINIO_CPP_HAS_CXX_MODULE)
  install(TARGETS miniocpp
          EXPORT miniocpp-targets
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
          FILE_SET CXX_MODULES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/miniocpp/modules")
else()
  install(TARGETS miniocpp
          EXPORT miniocpp-targets
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()

# Source-built deps must be in an export set for install(EXPORT) to resolve
# them (miniocpp_inih does not export itself).
if (MINIO_CPP_DEPS_EXPORT_TARGETS)
  install(TARGETS ${MINIO_CPP_DEPS_EXPORT_TARGETS}
          EXPORT miniocpp-targets
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

# Known limitation: this export does not pass CXX_MODULES_DIRECTORY, so an
# installed package (via find_package(miniocpp) + CMake, as opposed to
# consuming subprojects/miniocpp source directly, or via pkg-config/vcpkg)
# is missing the generated module metadata CMake needs to let a downstream
# CMake consumer `import minio;` - see
# https://cmake.org/cmake/help/latest/command/install.html#exporting-c-modules.
# Separately, installing module interfaces is not supported at all with
# the Visual Studio generator (independent of the MINIO_CPP_HAS_CXX_MODULE
# toolchain gate above, which only covers the build step). Neither gap is
# covered by an automated test yet.

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
