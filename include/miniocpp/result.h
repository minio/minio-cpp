// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022-2024 MinIO, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MINIO_CPP_RESULT_H_INCLUDED
#define MINIO_CPP_RESULT_H_INCLUDED

#include "error.h"
#include "tl/expected.hpp"

namespace minio {

template <typename T>
using Result = tl::expected<T, error::Error>;

}  // namespace minio

#endif  // MINIO_CPP_RESULT_H_INCLUDED
