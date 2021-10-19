# MinIO C++ Library for Amazon S3 Compatible Cloud Storage
# Copyright 2021 MinIO, Inc.
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

project(miniocpp)

cmake_minimum_required(VERSION 3.10)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# prohibit in-source-builds
IF (${CMAKE_BINARY_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
  MESSAGE(STATUS "In-source-builds are not allowed")
  MESSAGE(STATUS "Clean your source directory (e.g. delete the CMakeCache.txt file)")
  MESSAGE(FATAL_ERROR "Please create a separate build directory and call CMake again")
ENDIF (${CMAKE_BINARY_DIR} STREQUAL ${CMAKE_SOURCE_DIR})

add_subdirectory(curlpp)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/curlpp/include)

# Look for required libraries
SET(requiredlibs)

FIND_PACKAGE(CURL)
IF(CURL_FOUND)
  INCLUDE_DIRECTORIES(${CURL_INCLUDE_DIR})
  SET(requiredlibs ${requiredlibs} ${CURL_LIBRARIES} ${curlpp_BINARY_DIR}/libcurlpp.a)
ELSE(CURL_FOUND)
  MESSAGE(FATAL_ERROR "Could not find the CURL library and development files.")
ENDIF(CURL_FOUND)

FIND_PACKAGE(OpenSSL)
IF(OPENSSL_FOUND)
  INCLUDE_DIRECTORIES(${OPENSSL_INCLUDE_DIR})
  SET(requiredlibs ${requiredlibs} ${OPENSSL_LIBRARIES} ${LIBCRYPTO}) # bugfix, because libcrypto is not found automatically
ELSE(OPENSSL_FOUND)
  MESSAGE(FATAL_ERROR "Could not find the OpenSSL library and development files.")
ENDIF(OPENSSL_FOUND)

INCLUDE (CheckIncludeFiles)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)

SET(MINIOCPP_MAJOR_VERSION "0")
SET(MINIOCPP_MINOR_VERSION "0")
SET(MINIOCPP_PATCH_VERSION "1")

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(examples)

configure_file(miniocpp.pc.in miniocpp.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/miniocpp.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

