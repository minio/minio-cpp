// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2022 MinIO, Inc.
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

#include "client.h"

minio::s3::ListObjectsResult::ListObjectsResult(error::Error err) {
  this->failed_ = true;
  this->resp_.contents.push_back(Item(err));
  this->itr_ = resp_.contents.begin();
}

minio::s3::ListObjectsResult::ListObjectsResult(Client* client,
                                                ListObjectsArgs args) {
  this->client_ = client;
  this->args_ = args;
  Populate();
}

void minio::s3::ListObjectsResult::Populate() {
  if (args_.include_versions) {
    args_.key_marker = resp_.next_key_marker;
    args_.version_id_marker = resp_.next_version_id_marker;
  } else if (args_.use_api_v1) {
    args_.marker = resp_.next_marker;
  } else {
    args_.start_after = resp_.start_after;
    args_.continuation_token = resp_.next_continuation_token;
  }

  std::string region;
  if (GetRegionResponse resp = client_->GetRegion(args_.bucket, args_.region)) {
    region = resp.region;
    if (args_.recursive) {
      args_.delimiter = "";
    } else if (args_.delimiter.empty()) {
      args_.delimiter = "/";
    }

    if (args_.include_versions || !args_.version_id_marker.empty()) {
      resp_ = client_->ListObjectVersions(args_);
    } else if (args_.use_api_v1) {
      resp_ = client_->ListObjectsV1(args_);
    } else {
      resp_ = client_->ListObjectsV2(args_);
    }

    if (!resp_) {
      failed_ = true;
      resp_.contents.push_back(Item(resp_));
    }
  } else {
    failed_ = true;
    resp_.contents.push_back(Item(resp));
  }

  itr_ = resp_.contents.begin();
}

minio::s3::RemoveObjectsResult::RemoveObjectsResult(error::Error err) {
  done_ = true;
  resp_.errors.push_back(DeleteError(err));
  itr_ = resp_.errors.begin();
}

minio::s3::RemoveObjectsResult::RemoveObjectsResult(Client* client,
                                                    RemoveObjectsArgs args) {
  client_ = client;
  args_ = args;
  Populate();
}

void minio::s3::RemoveObjectsResult::Populate() {
  while (!done_ && resp_.errors.size() == 0) {
    RemoveObjectsApiArgs args;
    args.extra_headers = args_.extra_headers;
    args.extra_query_params = args_.extra_query_params;
    args.bucket = args_.bucket;
    args.region = args_.region;
    args.quiet = true;
    args.bypass_governance_mode = args_.bypass_governance_mode;

    for (int i = 0; i < 1000; i++) {
      DeleteObject object;
      if (!args_.func(object)) break;
      args.objects.push_back(object);
    }

    if (args.objects.size() != 0) {
      resp_ = client_->BaseClient::RemoveObjects(args);
      if (!resp_) resp_.errors.push_back(DeleteError(resp_));
      itr_ = resp_.errors.begin();
    } else {
      done_ = true;
    }
  }
}

minio::s3::Client::Client(BaseUrl& base_url, creds::Provider* provider)
    : BaseClient(base_url, provider) {}

minio::s3::StatObjectResponse minio::s3::Client::CalculatePartCount(
    size_t& part_count, std::list<ComposeSource> sources) {
  size_t object_size = 0;
  int i = 0;
  for (auto& source : sources) {
    if (source.ssec != NULL && !base_url_.https) {
      std::string msg = "source " + source.bucket + "/" + source.object;
      if (!source.version_id.empty()) msg += "?versionId=" + source.version_id;
      msg += ": SSE-C operation must be performed over a secure connection";
      return error::Error(msg);
    }

    i++;

    std::string etag;
    size_t size;

    StatObjectResponse resp = StatObject(source);
    if (!resp) return resp;
    etag = resp.etag;
    size = resp.size;
    if (error::Error err = source.BuildHeaders(size, etag)) return err;

    if (source.length != NULL) {
      size = *source.length;
    } else if (source.offset != NULL) {
      size -= *source.offset;
    }

    if (size < utils::kMinPartSize && sources.size() != 1 &&
        i != sources.size()) {
      std::string msg = "source " + source.bucket + "/" + source.object;
      if (!source.version_id.empty()) msg += "?versionId=" + source.version_id;
      msg += ": size " + std::to_string(size) + " must be greater than " +
             std::to_string(utils::kMinPartSize);
      return error::Error(msg);
    }

    object_size += size;
    if (object_size > utils::kMaxObjectSize) {
      return error::Error("destination object size must be less than " +
                          std::to_string(utils::kMaxObjectSize));
    }

    if (size > utils::kMaxPartSize) {
      size_t count = size / utils::kMaxPartSize;
      size_t last_part_size = size - (count * utils::kMaxPartSize);
      if (last_part_size > 0) {
        count++;
      } else {
        last_part_size = utils::kMaxPartSize;
      }

      if (last_part_size < utils::kMinPartSize && sources.size() != 1 &&
          i != sources.size()) {
        std::string msg = "source " + source.bucket + "/" + source.object;
        if (!source.version_id.empty()) {
          msg += "?versionId=" + source.version_id;
        }
        msg += ": size " + std::to_string(size) +
               " for multipart split upload of " + std::to_string(size) +
               ", last part size is less than " +
               std::to_string(utils::kMinPartSize);
        return error::Error(msg);
      }

      part_count += count;
    } else {
      part_count++;
    }

    if (part_count > utils::kMaxMultipartCount) {
      return error::Error(
          "Compose sources create more than allowed multipart count " +
          std::to_string(utils::kMaxMultipartCount));
    }
  }

  return error::SUCCESS;
}

minio::s3::ComposeObjectResponse minio::s3::Client::ComposeObject(
    ComposeObjectArgs args, std::string& upload_id) {
  size_t part_count = 0;
  {
    StatObjectResponse resp = CalculatePartCount(part_count, args.sources);
    if (!resp) return resp;
  }

  ComposeSource& source = args.sources.front();
  if (part_count == 1 && source.offset == NULL && source.length == NULL) {
    CopyObjectArgs coargs;
    coargs.extra_headers = args.extra_headers;
    coargs.extra_query_params = args.extra_query_params;
    coargs.bucket = args.bucket;
    coargs.region = args.region;
    coargs.object = args.object;
    coargs.sse = args.sse;
    coargs.source = source;

    return CopyObject(coargs);
  }

  utils::Multimap headers = args.Headers();

  {
    CreateMultipartUploadArgs cmu_args;
    cmu_args.extra_query_params = args.extra_query_params;
    cmu_args.bucket = args.bucket;
    cmu_args.region = args.region;
    cmu_args.object = args.object;
    cmu_args.headers = headers;
    if (CreateMultipartUploadResponse resp = CreateMultipartUpload(cmu_args)) {
      upload_id = resp.upload_id;
    } else {
      return resp;
    }
  }

  unsigned int part_number = 0;
  utils::Multimap ssecheaders;
  if (args.sse != NULL) {
    if (SseCustomerKey* ssec = dynamic_cast<SseCustomerKey*>(args.sse)) {
      ssecheaders = ssec->Headers();
    }
  }

  std::list<Part> parts;
  for (auto& source : args.sources) {
    size_t size = source.ObjectSize();
    if (source.length != NULL) {
      size = *source.length;
    } else if (source.offset != NULL) {
      size -= *source.offset;
    }

    size_t offset = 0;
    if (source.offset != NULL) offset = *source.offset;

    utils::Multimap headers;
    headers.AddAll(source.Headers());
    headers.AddAll(ssecheaders);

    if (size <= utils::kMaxPartSize) {
      part_number++;
      if (source.length != NULL) {
        headers.Add("x-amz-copy-source-range",
                    "bytes=" + std::to_string(offset) + "-" +
                        std::to_string(offset + *source.length - 1));
      } else if (source.offset != NULL) {
        headers.Add("x-amz-copy-source-range",
                    "bytes=" + std::to_string(offset) + "-" +
                        std::to_string(offset + size - 1));
      }

      UploadPartCopyArgs upc_args;
      upc_args.bucket = args.bucket;
      upc_args.region = args.region;
      upc_args.object = args.object;
      upc_args.headers = headers;
      upc_args.upload_id = upload_id;
      upc_args.part_number = part_number;
      UploadPartCopyResponse resp = UploadPartCopy(upc_args);
      if (!resp) return resp;
      parts.push_back(Part{part_number, resp.etag});
    } else {
      while (size > 0) {
        part_number++;

        size_t start_bytes = offset;
        size_t end_bytes = start_bytes + utils::kMaxPartSize;
        if (size < utils::kMaxPartSize) end_bytes = start_bytes + size;

        utils::Multimap headerscopy;
        headerscopy.AddAll(headers);
        headerscopy.Add("x-amz-copy-source-range",
                        "bytes=" + std::to_string(start_bytes) + "-" +
                            std::to_string(end_bytes));

        UploadPartCopyArgs upc_args;
        upc_args.bucket = args.bucket;
        upc_args.region = args.region;
        upc_args.object = args.object;
        upc_args.headers = headerscopy;
        upc_args.upload_id = upload_id;
        upc_args.part_number = part_number;
        UploadPartCopyResponse resp = UploadPartCopy(upc_args);
        if (!resp) return resp;
        parts.push_back(Part{part_number, resp.etag});

        offset = start_bytes;
        size -= (end_bytes - start_bytes);
      }
    }
  }

  CompleteMultipartUploadArgs cmu_args;
  cmu_args.bucket = args.bucket;
  cmu_args.region = args.region;
  cmu_args.object = args.object;
  cmu_args.upload_id = upload_id;
  cmu_args.parts = parts;
  return CompleteMultipartUpload(cmu_args);
}

minio::s3::PutObjectResponse minio::s3::Client::PutObject(
    PutObjectArgs& args, std::string& upload_id, char* buf) {
  utils::Multimap headers = args.Headers();
  if (!headers.Contains("Content-Type")) {
    if (args.content_type.empty()) {
      headers.Add("Content-Type", "application/octet-stream");
    } else {
      headers.Add("Content-Type", args.content_type);
    }
  }

  long object_size = args.object_size;
  size_t part_size = args.part_size;
  size_t uploaded_size = 0;
  unsigned int part_number = 0;
  std::string one_byte;
  bool stop = false;
  std::list<Part> parts;
  long part_count = args.part_count;

  while (!stop) {
    part_number++;

    size_t bytes_read = 0;
    if (part_count > 0) {
      if (part_number == part_count) {
        part_size = object_size - uploaded_size;
        stop = true;
      }

      if (error::Error err =
              utils::ReadPart(args.stream, buf, part_size, bytes_read)) {
        return err;
      }

      if (bytes_read != part_size) {
        return error::Error("not enough data in the stream; expected: " +
                            std::to_string(part_size) +
                            ", got: " + std::to_string(bytes_read) + " bytes");
      }
    } else {
      char* b = buf;
      size_t size = part_size + 1;

      if (!one_byte.empty()) {
        buf[0] = one_byte.front();
        b = buf + 1;
        size--;
        bytes_read = 1;
        one_byte = "";
      }

      size_t n = 0;
      if (error::Error err = utils::ReadPart(args.stream, b, size, n)) {
        return err;
      }

      bytes_read += n;

      // If bytes read is less than or equals to part size, then we have reached
      // last part.
      if (bytes_read <= part_size) {
        part_count = part_number;
        part_size = bytes_read;
        stop = true;
      } else {
        one_byte = buf[part_size + 1];
      }
    }

    std::string_view data(buf, part_size);

    uploaded_size += part_size;

    if (part_count == 1) {
      PutObjectApiArgs api_args;
      api_args.extra_query_params = args.extra_query_params;
      api_args.bucket = args.bucket;
      api_args.region = args.region;
      api_args.object = args.object;
      api_args.data = data;
      api_args.headers = headers;

      return BaseClient::PutObject(api_args);
    }

    if (upload_id.empty()) {
      CreateMultipartUploadArgs cmu_args;
      cmu_args.extra_query_params = args.extra_query_params;
      cmu_args.bucket = args.bucket;
      cmu_args.region = args.region;
      cmu_args.object = args.object;
      cmu_args.headers = headers;
      if (CreateMultipartUploadResponse resp =
              CreateMultipartUpload(cmu_args)) {
        upload_id = resp.upload_id;
      } else {
        return resp;
      }
    }

    UploadPartArgs up_args;
    up_args.bucket = args.bucket;
    up_args.region = args.region;
    up_args.object = args.object;
    up_args.upload_id = upload_id;
    up_args.part_number = part_number;
    up_args.data = data;
    if (args.sse != NULL) {
      if (SseCustomerKey* ssec = dynamic_cast<SseCustomerKey*>(args.sse)) {
        up_args.headers = ssec->Headers();
      }
    }

    if (UploadPartResponse resp = UploadPart(up_args)) {
      parts.push_back(Part{part_number, resp.etag});
    } else {
      return resp;
    }
  }

  CompleteMultipartUploadArgs cmu_args;
  cmu_args.bucket = args.bucket;
  cmu_args.region = args.region;
  cmu_args.object = args.object;
  cmu_args.upload_id = upload_id;
  cmu_args.parts = parts;
  return CompleteMultipartUpload(cmu_args);
}

//////////////////////////////////////////////////////////////////////////////

minio::s3::ComposeObjectResponse minio::s3::Client::ComposeObject(
    ComposeObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.sse != NULL && args.sse->TlsRequired() && !base_url_.https) {
    return error::Error(
        "SSE operation must be performed over a secure connection");
  }

  std::string upload_id;
  ComposeObjectResponse resp = ComposeObject(args, upload_id);
  if (!resp && !upload_id.empty()) {
    AbortMultipartUploadArgs amu_args;
    amu_args.bucket = args.bucket;
    amu_args.region = args.region;
    amu_args.object = args.object;
    amu_args.upload_id = upload_id;
    AbortMultipartUpload(amu_args);
  }

  return resp;
}

minio::s3::CopyObjectResponse minio::s3::Client::CopyObject(
    CopyObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.sse != NULL && args.sse->TlsRequired() && !base_url_.https) {
    return error::Error(
        "SSE operation must be performed over a secure connection");
  }

  if (args.source.ssec != NULL && !base_url_.https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string etag;
  size_t size;
  {
    StatObjectResponse resp = StatObject(args.source);
    if (!resp) return resp;
    etag = resp.etag;
    size = resp.size;
  }

  if (args.source.offset != NULL || args.source.length != NULL ||
      size > utils::kMaxPartSize) {
    if (args.metadata_directive != NULL &&
        *args.metadata_directive == Directive::kCopy) {
      return error::Error(
          "COPY metadata directive is not applicable to source object size "
          "greater than 5 GiB");
    }

    if (args.tagging_directive != NULL &&
        *args.tagging_directive == Directive::kCopy) {
      return error::Error(
          "COPY tagging directive is not applicable to source object size "
          "greater than 5 GiB");
    }

    ComposeSource src;
    src.extra_headers = args.source.extra_headers;
    src.extra_query_params = args.source.extra_query_params;
    src.bucket = args.source.bucket;
    src.region = args.source.region;
    src.object = args.source.object;
    src.ssec = args.source.ssec;
    src.offset = args.source.offset;
    src.length = args.source.length;
    src.match_etag = args.source.match_etag;
    src.not_match_etag = args.source.not_match_etag;
    src.modified_since = args.source.modified_since;
    src.unmodified_since = args.source.unmodified_since;

    ComposeObjectArgs coargs;
    coargs.extra_headers = args.extra_headers;
    coargs.extra_query_params = args.extra_query_params;
    coargs.bucket = args.bucket;
    coargs.region = args.region;
    coargs.object = args.object;
    coargs.sse = args.sse;
    coargs.sources.push_back(src);

    return ComposeObject(coargs);
  }

  utils::Multimap headers;
  headers.AddAll(args.extra_headers);
  headers.AddAll(args.Headers());
  if (args.metadata_directive != NULL) {
    headers.Add("x-amz-metadata-directive",
                DirectiveToString(*args.metadata_directive));
  }
  if (args.tagging_directive != NULL) {
    headers.Add("x-amz-tagging-directive",
                DirectiveToString(*args.tagging_directive));
  }
  headers.AddAll(args.source.CopyHeaders());

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kPut, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  req.headers.AddAll(headers);

  Response response = Execute(req);
  if (!response) return response;

  CopyObjectResponse resp;
  resp.etag = utils::Trim(response.headers.GetFront("etag"), '"');
  resp.version_id = response.headers.GetFront("x-amz-version-id");

  return resp;
}

minio::s3::DownloadObjectResponse minio::s3::Client::DownloadObject(
    DownloadObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.ssec != NULL && !base_url_.https) {
    return error::Error(
        "SSE-C operation must be performed over a secure connection");
  }

  std::string etag;
  size_t size;
  {
    StatObjectArgs soargs;
    soargs.bucket = args.bucket;
    soargs.region = args.region;
    soargs.object = args.object;
    soargs.version_id = args.version_id;
    soargs.ssec = args.ssec;
    StatObjectResponse resp = StatObject(soargs);
    if (!resp) return resp;
    etag = resp.etag;
    size = resp.size;
  }

  std::string temp_filename =
      args.filename + "." + curlpp::escape(etag) + ".part.minio";
  std::ofstream fout(temp_filename, fout.trunc | fout.out);
  if (!fout.is_open()) {
    return error::Error("unable to open file " + temp_filename);
  }

  std::string region;
  if (GetRegionResponse resp = GetRegion(args.bucket, args.region)) {
    region = resp.region;
  } else {
    return resp;
  }

  Request req(http::Method::kGet, region, base_url_, args.extra_headers,
              args.extra_query_params);
  req.bucket_name = args.bucket;
  req.object_name = args.object;
  if (!args.version_id.empty()) {
    req.query_params.Add("versionId", args.version_id);
  }
  req.datafunc = [&fout = fout](http::DataFunctionArgs args) -> bool {
    fout << args.datachunk;
    return true;
  };

  Response response = Execute(req);
  fout.close();
  if (response) std::filesystem::rename(temp_filename, args.filename);
  return response;
}

minio::s3::ListObjectsResult minio::s3::Client::ListObjects(
    ListObjectsArgs args) {
  if (error::Error err = args.Validate()) return err;
  return ListObjectsResult(this, args);
}

minio::s3::PutObjectResponse minio::s3::Client::PutObject(PutObjectArgs args) {
  if (error::Error err = args.Validate()) return err;

  if (args.sse != NULL && args.sse->TlsRequired() && !base_url_.https) {
    return error::Error(
        "SSE operation must be performed over a secure connection");
  }

  char* buf = NULL;
  if (args.part_count > 0) {
    buf = new char[args.part_size];
  } else {
    buf = new char[args.part_size + 1];
  }

  std::string upload_id;
  PutObjectResponse resp = PutObject(args, upload_id, buf);
  delete buf;

  if (!resp && !upload_id.empty()) {
    AbortMultipartUploadArgs amu_args;
    amu_args.bucket = args.bucket;
    amu_args.region = args.region;
    amu_args.object = args.object;
    amu_args.upload_id = upload_id;
    AbortMultipartUpload(amu_args);
  }

  return resp;
}

minio::s3::UploadObjectResponse minio::s3::Client::UploadObject(
    UploadObjectArgs args) {
  if (error::Error err = args.Validate()) return err;
  file_size = args.object_size;

  std::ifstream file;
  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  try {
    file.open(args.filename);
  } catch (std::system_error& err) {
    return error::Error("unable to open file " + args.filename + "; " +
                        err.code().message());
  }

  PutObjectArgs po_args(file, args.object_size, 0);
  po_args.extra_headers = args.extra_headers;
  po_args.extra_query_params = args.extra_query_params;
  po_args.bucket = args.bucket;
  po_args.region = args.region;
  po_args.object = args.object;
  po_args.headers = args.headers;
  po_args.user_metadata = args.user_metadata;
  po_args.sse = args.sse;
  po_args.tags = args.tags;
  po_args.retention = args.retention;
  po_args.legal_hold = args.legal_hold;
  po_args.content_type = args.content_type;

  PutObjectResponse resp = PutObject(po_args);
  file.close();
  return resp;
}

minio::s3::RemoveObjectsResult minio::s3::Client::RemoveObjects(
    RemoveObjectsArgs args) {
  if (error::Error err = args.Validate()) return err;
  return RemoveObjectsResult(this, args);
}

int minio::s3::Client::GetUploadProgress() {
  static double current_size = 0;
  int progress_value = 0;
  if (file_size > 0) {
    current_size += uploaded_size;
    progress_value = static_cast<int>((current_size / file_size)*100);
  }
  return progress_value;
}