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

SET(SRCS s3.cpp s3_http.cpp s3_io.cpp s3_signature_v2.cpp)
SET(S3CLIENT_INSTALL_LIST)

add_library(miniocpp STATIC ${SRCS})
target_link_libraries(miniocpp ${requiredlibs})

SET(S3CLIENT_INSTALL_LIST ${S3CLIENT_INSTALL_LIST} miniocpp)

SET_TARGET_PROPERTIES(miniocpp PROPERTIES VERSION ${MINIOCPP_MAJOR_VERSION}.${MINIOCPP_MINOR_VERSION}.${MINIOCPP_PATCH_VERSION})
SET_TARGET_PROPERTIES(miniocpp PROPERTIES CLEAN_DIRECT_OUTPUT 1)

# install the library
INSTALL(TARGETS ${S3CLIENT_INSTALL_LIST}
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
