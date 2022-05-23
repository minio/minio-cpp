SET(S3_LIBS ${requiredlibs})

ADD_EXECUTABLE(MakeBucket MakeBucket.cc)
TARGET_LINK_LIBRARIES(MakeBucket miniocpp ${S3_LIBS})

ADD_EXECUTABLE(RemoveBucket RemoveBucket.cc)
TARGET_LINK_LIBRARIES(RemoveBucket miniocpp ${S3_LIBS})

ADD_EXECUTABLE(BucketExists BucketExists.cc)
TARGET_LINK_LIBRARIES(BucketExists miniocpp ${S3_LIBS})

ADD_EXECUTABLE(ListBuckets ListBuckets.cc)
TARGET_LINK_LIBRARIES(ListBuckets miniocpp ${S3_LIBS})

ADD_EXECUTABLE(StatObject StatObject.cc)
TARGET_LINK_LIBRARIES(StatObject miniocpp ${S3_LIBS})

ADD_EXECUTABLE(RemoveObject RemoveObject.cc)
TARGET_LINK_LIBRARIES(RemoveObject miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DownloadObject DownloadObject.cc)
TARGET_LINK_LIBRARIES(DownloadObject miniocpp ${S3_LIBS})

ADD_EXECUTABLE(UploadObject UploadObject.cc)
TARGET_LINK_LIBRARIES(UploadObject miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetObject GetObject.cc)
TARGET_LINK_LIBRARIES(GetObject miniocpp ${S3_LIBS})

ADD_EXECUTABLE(ListObjects ListObjects.cc)
TARGET_LINK_LIBRARIES(ListObjects miniocpp ${S3_LIBS})

ADD_EXECUTABLE(PutObject PutObject.cc)
TARGET_LINK_LIBRARIES(PutObject miniocpp ${S3_LIBS})

ADD_EXECUTABLE(CopyObject CopyObject.cc)
TARGET_LINK_LIBRARIES(CopyObject miniocpp ${S3_LIBS})

ADD_EXECUTABLE(ComposeObject ComposeObject.cc)
TARGET_LINK_LIBRARIES(ComposeObject miniocpp ${S3_LIBS})
