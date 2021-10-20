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
#include <stdlib.h>
#include <getopt.h>
#include <s3.h>

using namespace Minio;

bool createBucket( S3Client clnt, const std::string& aBucketName) {
  S3ClientIO io;
  clnt.MakeBucket(aBucketName, io);
  if(io.Failure()) {
    std::cerr << "ERROR: failed to create bucket" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    return false;
  }
  return true;
}

bool deleteBucket ( S3Client clnt, const std::string& aBucketName) {
  S3ClientIO io;
  clnt.RemoveBucket(aBucketName, io);
  if(io.Failure()) {
    std::cerr << "ERROR: failed to remove bucket" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    return false;
  }
  return true;
}

bool del ( S3Client clnt, const std::string& aBucketName, const std::string& aKey )
{
  S3ClientIO io;
  clnt.DeleteObject(aBucketName, aKey, io);
  if(io.Failure()) {
    std::cerr << "ERROR: delete: failed to delete the object" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    return false;
  }
  return true;
}

bool put ( S3Client clnt, const std::string& aBucketName, const std::string& aFileName, const std::string& aKey )
{
  S3ClientIO io;
  io.reqHeaders.Update("Content-Type", "text/plain"); // set content-type
  clnt.PutObject(aBucketName, aKey, aFileName, io);
  if(io.Failure()) {
    std::cerr << "ERROR: failed to put object" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    return false;
  }
  return true;
}

bool get ( S3Client clnt, const std::string& aBucketName, const std::string& aKey, const std::string& aFileName)
{
  S3ClientIO objinfo_io;
  clnt.StatObject(aBucketName, aKey, objinfo_io);

  ofstream fout(aFileName.c_str(), ios_base::binary | ios_base::out);
  S3ClientIO io(NULL, &fout);
  io.bytesToGet = objinfo_io.respHeaders.GetWithDefault("Content-Length", 0);
  clnt.GetObject(aBucketName, aKey, io);
  if(io.Failure()) {
    std::cerr << "ERROR: failed to get object" << endl;
    std::cerr << "response:\n" << io << endl;
    return false;
  }
  return true;
}

bool multipart ( S3Client clnt, const std::string& aBucketName, const std::string& aKey, const std::string& aFileName)
{
  S3ClientIO io;
  io.reqHeaders.Update("Content-Type", "text/plain"); // set content-type
  std::string upload_id = clnt.CreateMultipartUpload(aBucketName, aKey, io);
  if(io.Failure()) {
    std::cerr << "ERROR: failed to put object" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    return false;
  }
  ifstream fin(aFileName.c_str(), ios_base::binary | ios_base::in);
  if(!fin) {
    clnt.AbortMultipartUpload(aBucketName, aKey, upload_id);
    cerr << "Could not read file " << aFileName << endl;
    return false;
  }
  io.Reset();
  io.istrm = &fin;
  // upload first part.
  Minio::S3::CompletePart complPart = clnt.PutObject(aBucketName, aKey, 1, upload_id, io);
  fin.close();
  if(io.Failure()) {
    std::cerr << "ERROR: failed to put object" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    clnt.AbortMultipartUpload(aBucketName, aKey, upload_id);
    return false;
  }
  std::list<Minio::S3::CompletePart> parts;
  parts.push_back(complPart);
  io.Reset();
  clnt.CompleteMultipartUpload(aBucketName, aKey, upload_id, parts, io);
  if(io.Failure()) {
    std::cerr << "ERROR: failed to put object" << endl;
    std::cerr << "response:\n" << io << endl;
    std::cerr << "response body:\n" << io.response.str() << endl;
    clnt.AbortMultipartUpload(aBucketName, aKey, upload_id);
    return false;
  }
  return true;
}

void
usage()
{
  std::cout << "Usage: s3 <options>" << std::endl;
  std::cout << "  -i AWS Access Key Id"  << std::endl;
  std::cout << "  -s AWS Secret Access Key"  << std::endl;
  std::cout << "  -e AWS Endpoint" << std::endl;
  std::cout << "  -a <action>: action to perform" << std::endl;
  std::cout << "      action is one of the following:" << std::endl;
  std::cout << "          \"create\": create a bucket" << std::endl;
  std::cout << "          \"delete\": delete a bucket" << std::endl;
  std::cout << "          \"put\": put a file on s3" << std::endl;
  std::cout << "          \"get\": get a file from s3" << std::endl;
  std::cout << "          \"del\": delete a file from s3" << std::endl;
  std::cout << "          \"multipart\": multipart API calls"<< std::endl;
  std::cout << "  -f filename: name of file"  << std::endl;
  std::cout << "  -n name: name of bucket"  << std::endl;
  std::cout << "  -k key: key of the object" << std::endl;
}

int
main ( int argc, char** argv )
{
  char* lBucketName = 0;
  char* lAction = 0;
  char* lAccessKeyId = 0;
  char* lSecretAccessKey = 0;
  char* lEndpoint = 0;
  char* lFileName = 0;
  char* lKey = 0;
  int c;
  opterr = 0;

  while ((c = getopt (argc, argv, "hi:k:e:a:n:f:p:mx:d:s:")) != -1)
    switch (c)
    {
      case 'i':
        lAccessKeyId = optarg;
        break;
      case 's':
        lSecretAccessKey = optarg;
        break;
      case 'e':
        lEndpoint = optarg;
        break;
      case 'a':
        lAction = optarg;
        break;
      case 'n':
        lBucketName = optarg;
        break;
      case 'f':
        lFileName = optarg;
        break;
      case 'k':
        lKey = optarg;
        break;
      case 'h': {
        usage();
        exit(1);
        exit(1);
      }
      case '?':
        if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr,
              "Unknown option character `\\x%x'.\n",
              optopt);
        exit(1);
      default:
        exit(1);
    }

  if (!lAccessKeyId)
    lAccessKeyId = getenv("ACCESS_KEY");

  if (!lSecretAccessKey)
    lSecretAccessKey = getenv("SECRET_KEY");

  if (!lEndpoint)
    lEndpoint = getenv("ENDPOINT");

  if (!lAccessKeyId) {
    std::cerr << "No Access Key given" << std::endl;
    std::cerr << "Either use -i as a command line argument or set ACCESS_KEY as an environmental variable" << std::endl;
    exit(1);
  }

  if (!lSecretAccessKey) {
    std::cerr << "No Secret Access Key given" << std::endl;
    std::cerr << "Either use -s as a command line argument or set SECRET_KEY as an environmental variable" << std::endl;
    exit(1);
  }

  if (!lEndpoint) {
    std::cerr << "No Endpoint given" << std::endl;
    std::cerr << "Either use -e as a command line argument or set ENDPOINT as an environmental variable" << std::endl;
    exit(1);
  }

  S3Client s3(lEndpoint, lAccessKeyId, lSecretAccessKey);

  if (!lAction) {
    std::cerr << "No Action parameter specified." << std::endl;
    std::cerr << "Use -a as a command line argument" << std::endl;
    exit(1);
  }
  std::string lActionString(lAction);

  if ( lActionString.compare ( "mb" ) == 0 ) {
    if (!lBucketName) {
      std::cerr << "No bucket name parameter specified." << std::endl;
      std::cerr << "Use -n as a command line argument" << std::endl;
      exit(1);
    }
    createBucket(s3, lBucketName);
  } else if ( lActionString.compare ( "rb" ) == 0) {
    if (!lBucketName) {
      std::cerr << "No bucket name parameter specified." << std::endl;
      std::cerr << "Use -n as a command line argument" << std::endl;
      exit(1);
    }
    deleteBucket(s3, lBucketName);
  } else if ( lActionString.compare ( "up" ) == 0) {
    if (!lBucketName) {
      std::cerr << "No bucket name parameter specified." << std::endl;
      std::cerr << "Use -n as a command line argument" << std::endl;
      exit(1);
    }
    if (lKey==0) {
      std::cerr << "No key parameter specified." << std::endl;
      std::cerr << "Use -k as a command line argument" << std::endl;
      exit(1);
    }
    if (!lFileName) {
      std::cerr << "No file specified." << std::endl;
      std::cerr << "Use -f as a command line argument" << std::endl;
      exit(1);
    }
    put(s3, lBucketName, lFileName, lKey);
  } else if ( lActionString.compare ( "down" ) == 0) {
    if (!lBucketName) {
      std::cerr << "No bucket name parameter specified." << std::endl;
      std::cerr << "Use -n as a command line argument" << std::endl;
      exit(1);
    }
    if (lKey==0) {
      std::cerr << "No key parameter specified." << std::endl;
      std::cerr << "Use -k as a command line argument" << std::endl;
      exit(1);
    }
    if (!lFileName) {
      std::cerr << "No file specified." << std::endl;
      std::cerr << "Use -f as a command line argument" << std::endl;
      exit(1);
    }
    get(s3, lBucketName, lKey, lFileName);
  } else if ( lActionString.compare ( "rm" ) == 0) {
    if (!lBucketName) {
      std::cerr << "No bucket name parameter specified." << std::endl;
      std::cerr << "Use -n as a command line argument." << std::endl;
      exit(1);
    }
    if (!lKey) {
      std::cerr << "No key parameter specified." << std::endl;
      std::cerr << "Use -k as a command line argument." << std::endl;
      exit(1);
    }
    del(s3, lBucketName, lKey);
  } else if ( lActionString.compare ( "multipart" ) == 0) {
    if (!lBucketName) {
      std::cerr << "No bucket name parameter specified." << std::endl;
      std::cerr << "Use -n as a command line argument." << std::endl;
      exit(1);
    }
    if (!lKey) {
      std::cerr << "No key parameter specified." << std::endl;
      std::cerr << "Use -k as a command line argument." << std::endl;
      exit(1);
    }
    if (!lFileName) {
      std::cerr << "No file specified." << std::endl;
      std::cerr << "Use -f as a command line argument" << std::endl;
      exit(1);
    }
    multipart(s3, lBucketName, lKey, lFileName);
  } else {
    std::cerr << "Invalid action: \"" << lActionString << "\"." << std::endl;
  }
}
