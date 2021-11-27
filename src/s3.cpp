// MinIO C++ Library for Amazon S3 Compatible Cloud Storage
// Copyright 2021 MinIO, Inc.
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

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <locale>

#include <curlpp/cURLpp.hpp>
#include <curlpp/Options.hpp>
#include <pugixml.hpp>

#include "s3.h"
#include "s3_signature_v2.h"

using namespace std;
using namespace Minio;
using namespace Minio::Http;
using namespace Minio::XML;
using namespace Minio::SignatureV2;

namespace Minio
{
  std::ostream & operator<<(std::ostream & ostrm, Minio::S3ClientIO & io)
  {
    ostrm << "result: " << io.result << std::endl;
    ostrm << "headers:" << std::endl;
    Headers::iterator i;
    for(i = io.respHeaders.begin(); i != io.respHeaders.end(); ++i)
      ostrm << i->first << ": " << i->second << std::endl;
    return ostrm;
  }
}

S3Client::S3Client(const std::string & endpoint, const std::string & kid, const std::string & sk):
    endpoint(endpoint),
    keyID(kid), secret(sk),
    verbosity(0)
{
}

S3Client::~S3Client()
{
  // Teardown connections, etc
}

std::string S3Client::ParseCreateMultipartUpload(const std::string & xml)
{
  string data;
  ExtractXMLXPath(data, "/InitiateMultipartUploadResult/UploadId", xml);
  return data;
}

void S3Client::ParseBucketsList(list<Minio::S3::Bucket> & buckets, const std::string & xml)
{
  string::size_type crsr = 0;
  string data;
  string name, date;
  string ownerName, ownerID;
  ExtractXML(ownerID, crsr, "ID", xml);
  ExtractXML(ownerName, crsr, "DisplayName", xml);

  while(ExtractXML(data, crsr, "Name", xml))
  {
    name = data;
    if(ExtractXML(data, crsr, "CreationDate", xml))
      date = data;
    else
      date = "";
    buckets.push_back(Minio::S3::Bucket(name, date));
  }
}

void S3Client::ParseObjectsList(list<Minio::S3::Object> & objects, const std::string & xml)
{
  string::size_type crsr = 0;
  string data;

  while(ExtractXML(data, crsr, "Key", xml))
  {
    Minio::S3::Object obj;
    obj.key = data;

    if(ExtractXML(data, crsr, "LastModified", xml))
      obj.lastModified = data;
    if(ExtractXML(data, crsr, "ETag", xml)) {
      // eTag starts and ends with &quot;, remove these
      //obj.eTag = data;
      obj.eTag = data.substr(6, data.size() - 12);
    }
    if(ExtractXML(data, crsr, "Size", xml))
      obj.size = data;

    if(ExtractXML(data, crsr, "ID", xml))
      obj.ownerID = data;
    if(ExtractXML(data, crsr, "DisplayName", xml))
      obj.ownerDisplayName = data;

    if(ExtractXML(data, crsr, "StorageClass", xml))
      obj.storageClass = data;

    objects.push_back(obj);
  }
}

void S3Client::ListObjects(Minio::S3::Bucket & bucket, S3Connection ** conn)
{
  std::ostringstream objectList;
  Minio::S3ClientIO io(NULL, &objectList);
  ListObjects(bucket.name, io, conn);
  ParseObjectsList(bucket.objects, objectList.str());
}

string S3Client::SignV2Request(const Minio::S3ClientIO & io, const std::string & uri, const std::string & mthd)
{
  std::ostringstream sigstrm;
  sigstrm << mthd << "\n";
  sigstrm << io.reqHeaders.GetWithDefault("Content-MD5", "") << "\n";
  sigstrm << io.reqHeaders.GetWithDefault("Content-Type", "") << "\n";
  sigstrm << io.httpDate << "\n";

  // http://docs.amazonwebservices.com/AmazonS3/latest/index.html?RESTAccessPolicy.html
  // CanonicalizedAmzHeaders
  HeaderValueCollection::const_iterator i;
  for(i = io.reqHeaders.begin(); i != io.reqHeaders.end(); ++i) {
    if(i->first.substr(0, 6) == "x-amz-")
      sigstrm << i->first + ":" + i->second << '\n';
  }
  sigstrm << PathSeparator << uri;

  if(verbosity >= 3)
    cout << "#### sigtext:\n" << sigstrm.str() << "\n#### end sigtext" << endl;

  return GenerateSignature(secret, sigstrm.str());
}


// read by libcurl...read data to send
struct ReadDataCB {
  Minio::S3ClientIO & io;
  ReadDataCB(Minio::S3ClientIO & ioio): io(ioio) {}
  size_t operator()(char * buf, size_t size, size_t nmemb) {return io.Read(buf, size, nmemb);}
};

// write by libcurl...handle data received
struct WriteDataCB {
  Minio::S3ClientIO & io;
  WriteDataCB(Minio::S3ClientIO & ioio): io(ioio) {}
  size_t operator()(char * buf, size_t size, size_t nmemb) {return io.Write(buf, size, nmemb);}
};

// Handle header data.
//"The header callback will be called once for each header and
// only complete header lines are passed on to the callback."
struct HeaderCB {
  Minio::S3ClientIO & io;
  HeaderCB(Minio::S3ClientIO & ioio): io(ioio) {}
  size_t operator()(char * buf, size_t size, size_t nmemb) {return io.HandleHeader(buf, size, nmemb);}
};

void S3Client::Submit(const std::string & url, const std::string & uri,
                      Method method, Minio::S3ClientIO & io,
                      S3Connection ** reqPtr)
{
  string signature;
  io.httpDate = HTTP_Date();
  signature = SignV2Request(io, uri, methodToString(method));

  if(verbosity >= 2)
    io.printProgress = true;

  try {
    // myCleanup required to do cleanup of used resources (RAII style) as specified in curlpp docs.
    curlpp::Cleanup myCleanup;
    curlpp::Easy request;

    std::ostringstream authstrm, datestrm, urlstrm;
    datestrm << "Date: " << io.httpDate;
    authstrm << "Authorization: AWS " << keyID << ":" << signature;

    std::list<std::string> headers;
    headers.push_back(datestrm.str());
    headers.push_back(authstrm.str());

    HeaderValueCollection::iterator i;
    for(i = io.reqHeaders.begin(); i != io.reqHeaders.end(); ++i) {
      headers.push_back(i->first + ": " + i->second);
      if(verbosity >= 3)
        cout << "special header: " << i->first + ": " + i->second << endl;
    }

    request.setOpt(new cURLpp::Options::WriteFunction(cURLpp::Types::WriteFunctionFunctor(WriteDataCB(io))));
    request.setOpt(new cURLpp::Options::HeaderFunction(cURLpp::Types::WriteFunctionFunctor(HeaderCB(io))));

    switch(method) {
      case Method::HTTP_GET:
        request.setOpt(new cURLpp::Options::HttpGet(true));
        break;
      case Method::HTTP_PUT:
        if (io.bytesToPut > 0) {
          request.setOpt(new cURLpp::Options::Put(true));
          request.setOpt(new cURLpp::Options::InfileSize(io.bytesToPut));
          request.setOpt(new cURLpp::Options::ReadFunction(cURLpp::Types::ReadFunctionFunctor(ReadDataCB(io))));
        } else {
          request.setOpt(new cURLpp::Options::CustomRequest(methodToString(method)));
        }
        break;
      case Method::HTTP_POST:
        if (io.bytesToPut > 0) {
          request.setOpt(new cURLpp::Options::Post(true));
          request.setOpt(new cURLpp::Options::PostFieldSize(io.bytesToPut));
          request.setOpt(new cURLpp::Options::ReadFunction(cURLpp::Types::ReadFunctionFunctor(ReadDataCB(io))));
        } else {
          request.setOpt(new cURLpp::Options::CustomRequest(methodToString(method)));
        }
        break;
      case Method::HTTP_HEAD:
        request.setOpt(new cURLpp::Options::Header(true));
        request.setOpt(new cURLpp::Options::NoBody(true));
        break;
      default:
        request.setOpt(new cURLpp::Options::CustomRequest(methodToString(method)));
        break;
    }

    request.setOpt(new cURLpp::Options::Url(url));
    request.setOpt(new cURLpp::Options::Verbose(verbosity >= 3));
    request.setOpt(new cURLpp::Options::HttpHeader(headers));

    io.WillStart();
    request.perform();
    io.DidFinish();
  }
  catch(cURLpp::RuntimeError & e) {
    io.error = true;
    cerr << "Error: " << e.what() << endl;
  }
  catch(cURLpp::LogicError & e) {
    io.error = true;
    cerr << "Error: " << e.what() << endl;
  }
}

void S3Client::PutObject(const std::string & bkt, const std::string & key,
                         Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt << PathSeparator << key;

  istream & fin = *io.istrm;
  uint8_t md5[EVP_MAX_MD_SIZE];
  size_t mdLen = ComputeMD5(md5, fin);
  io.reqHeaders.Update("Content-MD5", EncodeB64(md5, mdLen));

  fin.clear();
  fin.seekg(0, std::ios_base::end);
  ifstream::pos_type endOfFile = fin.tellg();
  fin.seekg(0, std::ios_base::beg);
  ifstream::pos_type startOfFile = fin.tellg();

  io.bytesReceived = 0;
  io.bytesToPut = static_cast<size_t>(endOfFile - startOfFile);

  Submit(urlstrm.str(), bkt + PathSeparator + key, Method::HTTP_PUT, io, reqPtr);
}

void S3Client::PutObject(const std::string & bkt, const std::string & key,
                         const std::string & path,
                         Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  ifstream fin(path.c_str(), ios_base::binary | ios_base::in);
  if(!fin) {
    cerr << "Could not read file " << path << endl;
    return;
  }
  io.istrm = &fin;
  PutObject(bkt, key, io, reqPtr);
  fin.close();
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

Minio::S3::CompletePart S3Client::PutObject(const std::string & bkt,
                                            const std::string & key,
                                            const int & partNumber,
                                            const std::string & upload_id,
                                            Minio::S3ClientIO & io,
                                            S3Connection ** reqPtr)
{
  if(partNumber <= 0)
    throw std::invalid_argument( "received invalid value" );
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator
          << bkt << PathSeparator << key
          << "?partNumber="+ std::to_string(partNumber) + "&uploadId=" + upload_id;

  istream & fin = *io.istrm;
  uint8_t md5[EVP_MAX_MD_SIZE];
  size_t mdLen = ComputeMD5(md5, fin);
  io.reqHeaders.Update("Content-MD5", EncodeB64(md5, mdLen));

  fin.clear();
  fin.seekg(0, std::ios_base::end);
  ifstream::pos_type endOfFile = fin.tellg();
  fin.seekg(0, std::ios_base::beg);
  ifstream::pos_type startOfFile = fin.tellg();

  io.bytesReceived = 0;
  io.bytesToPut = static_cast<size_t>(endOfFile - startOfFile);

  Submit(urlstrm.str(), bkt + PathSeparator + key +
         "?partNumber="+ std::to_string(partNumber) +
         "&uploadId=" + upload_id,
         Method::HTTP_PUT, io, reqPtr);

  Minio::S3::CompletePart complPart;
  complPart.eTag = io.respHeaders.GetWithDefault("ETag", "");
  trim(complPart.eTag);
  complPart.partNumber = partNumber;
  return complPart;
}

std::string S3Client::CreateMultipartUpload(const std::string & bkt,
                                            const std::string & key,
                                            Minio::S3ClientIO & io,
                                            S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  std::ostringstream uploadIDResult;
  io.ostrm = &uploadIDResult;

  urlstrm <<  endpoint << PathSeparator
          << bkt << PathSeparator << key
          << "?uploads";

  Submit(urlstrm.str(), bkt + PathSeparator + key + "?uploads",
         Method::HTTP_POST, io, reqPtr);

  return ParseCreateMultipartUpload(uploadIDResult.str());
}

void S3Client::AbortMultipartUpload(const std::string & bkt,
                                    const std::string & key,
                                    const std::string & upload_id,
                                    S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  Minio::S3ClientIO io;
  urlstrm <<  endpoint << PathSeparator
          << bkt << PathSeparator << key
          << "?uploadId=" + upload_id;

  Submit(urlstrm.str(), bkt + PathSeparator + key +
         "?uploadId=" + upload_id,
         Method::HTTP_DELETE, io, reqPtr);
}

void S3Client::CompleteMultipartUpload(const std::string & bkt,
                                       const std::string & key,
                                       const std::string & upload_id,
                                       const std::list<Minio::S3::CompletePart> & parts,
                                       Minio::S3ClientIO & io,
                                       S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator
          << bkt << PathSeparator << key
          << "?uploadId=" + upload_id;

  std::string completeMultipartRequest;
  completeMultipartRequest.append("<CompleteMultipartUpload>");
  for (auto& item : parts) {
    completeMultipartRequest.append("<Part>");
    completeMultipartRequest.append("<PartNumber>" + std::to_string(item.partNumber) + "</PartNumber>");
    completeMultipartRequest.append("<ETag>"+ item.eTag +"</ETag>");
    completeMultipartRequest.append("</Part>");
  }
  completeMultipartRequest.append("</CompleteMultipartUpload>");

  istringstream body(completeMultipartRequest);
  io.istrm = &body;
  io.bytesToPut = completeMultipartRequest.length();

  io.reqHeaders.Update("Content-Type", "application/octet-stream");
  Submit(urlstrm.str(), bkt + PathSeparator + key +
         "?uploadId=" + upload_id,
         Method::HTTP_POST, io, reqPtr);
}

void S3Client::GetObject(const std::string & bkt,
                         const std::string & key,
                         const int & partNumber,
                         Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt << PathSeparator << key << "?partNumber=" + std::to_string(partNumber);
  Submit(urlstrm.str(), bkt + PathSeparator + key + "?partNumber=" + std::to_string(partNumber), Method::HTTP_GET, io, reqPtr);
}

void S3Client::GetObject(const std::string & bkt, const std::string & key,
                         Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt << PathSeparator << key;
  Submit(urlstrm.str(), bkt + PathSeparator + key, Method::HTTP_GET, io, reqPtr);
}

void S3Client::StatObject(const std::string & bkt, const std::string & key,
                          Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt << PathSeparator << key;
  Submit(urlstrm.str(), bkt + PathSeparator + key, Method::HTTP_HEAD, io, reqPtr);
}

void S3Client::DeleteObject(const std::string & bkt, const std::string & key,
                            Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt << PathSeparator << key;
  Submit(urlstrm.str(), bkt + PathSeparator + key, Method::HTTP_DELETE, io, reqPtr);
}

void S3Client::CopyObject(const std::string & srcbkt, const std::string & srckey,
                          const std::string & dstbkt, const std::string & dstkey, bool copyMD,
                          Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << dstbkt << PathSeparator << dstkey;
  io.reqHeaders.Update("x-amz-copy-source", string(PathSeparator) + srcbkt + PathSeparator + srckey);
  io.reqHeaders.Update("x-amz-metadata-directive", copyMD? "COPY" : "REPLACE");
  //    io.reqHeaders["x-amz-copy-source-if-match"] =  etag
  //    io.reqHeaders["x-amz-copy-source-if-none-match"] =  etag
  //    io.reqHeaders["x-amz-copy-source-if-unmodified-since"] =  time_stamp
  //    io.reqHeaders["x-amz-copy-source-if-modified-since"] =  time_stamp
  Submit(urlstrm.str(), dstbkt + PathSeparator + dstkey,Method::HTTP_PUT, io, reqPtr);
}

//************************************************************************************************
// Buckets
//************************************************************************************************

void S3Client::ListBuckets(Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  Submit(endpoint, "", Method::HTTP_GET, io, reqPtr);
}

void S3Client::MakeBucket(const std::string & bkt, Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt;
  io.bytesToPut = 0;
  Submit(urlstrm.str(), bkt,Method::HTTP_PUT, io, reqPtr);
}

void S3Client::ListObjects(const std::string & bkt, Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt;
  Submit(urlstrm.str(), bkt, Method::HTTP_GET, io, reqPtr);
}

void S3Client::RemoveBucket(const std::string & bkt, Minio::S3ClientIO & io, S3Connection ** reqPtr)
{
  std::ostringstream urlstrm;
  urlstrm <<  endpoint << PathSeparator << bkt;
  Submit(urlstrm.str(), bkt, Method::HTTP_DELETE, io, reqPtr);
}


