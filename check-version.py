#!/usr/bin/env python3

# MinIO C++ Library for Amazon S3 Compatible Cloud Storage
# Copyright 2022-2024 MinIO, Inc.
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

import json
import os
import re
import sys

ROOT_PATH = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))

def read_text_file(file_name):
  with open(file_name, "r", encoding="utf-8") as f:
    return f.read()

def read_vcpkg_version():
  package = json.loads(read_text_file(os.path.join(ROOT_PATH, "vcpkg.json")))
  return package["version"]

def read_cmake_version():
  content = read_text_file(os.path.join(ROOT_PATH, "CMakeLists.txt"))

  major = re.search("set\\(MINIO_CPP_MAJOR_VERSION \"(\\d+)\"\\)", content)
  minor = re.search("set\\(MINIO_CPP_MINOR_VERSION \"(\\d+)\"\\)", content)
  patch = re.search("set\\(MINIO_CPP_PATCH_VERSION \"(\\d+)\"\\)", content)

  return "{}.{}.{}".format(major[1], minor[1], patch[1])

def read_source_version():
  content = read_text_file(os.path.join(ROOT_PATH, "include", "miniocpp", "config.h"))

  major = re.search("#define MINIO_CPP_MAJOR_VERSION (\\d+)", content)
  minor = re.search("#define MINIO_CPP_MINOR_VERSION (\\d+)", content)
  patch = re.search("#define MINIO_CPP_PATCH_VERSION (\\d+)", content)

  return "{}.{}.{}".format(major[1], minor[1], patch[1])

def main():
  vcpkg_version = read_vcpkg_version()
  cmake_version = read_cmake_version()
  source_version = read_source_version()

  if source_version == vcpkg_version and source_version == cmake_version:
    print("minio-cpp version {} is set correctly in all required files".format(source_version))
  else:
    print("Versions don't match [source={} vcpkg={} cmake={}]\n".format(source_version, vcpkg_version, cmake_version))
    print("When increasing a version, please make sure that all of the above have the same value!\n")
    print("Versions can be found in the following files:")
    print("  * include/miniocpp/config.h")
    print("  * CMakeLists.txt")
    print("  * vcpkg.json")
    sys.exit(1)

if __name__ == "__main__":
  main()
