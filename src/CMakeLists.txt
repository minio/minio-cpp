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

list(APPEND SRCS args.cc baseclient.cc client.cc http.cc request.cc response.cc select.cc signer.cc sse.cc types.cc utils.cc)

add_library(miniocpp STATIC ${SRCS})
target_link_libraries(miniocpp ${requiredlibs})

list(APPEND S3CLIENT_INSTALL_LIST miniocpp)

SET_TARGET_PROPERTIES(miniocpp PROPERTIES 
  VERSION ${MINIOCPP_MAJOR_VERSION}.${MINIOCPP_MINOR_VERSION}.${MINIOCPP_PATCH_VERSION}
  CLEAN_DIRECT_OUTPUT 1
)

# install the library
INSTALL(TARGETS ${S3CLIENT_INSTALL_LIST}
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
