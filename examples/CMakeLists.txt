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

ADD_EXECUTABLE(RemoveObjects RemoveObjects.cc)
TARGET_LINK_LIBRARIES(RemoveObjects miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SelectObjectContent SelectObjectContent.cc)
TARGET_LINK_LIBRARIES(SelectObjectContent miniocpp ${S3_LIBS})

ADD_EXECUTABLE(ListenBucketNotification ListenBucketNotification.cc)
TARGET_LINK_LIBRARIES(ListenBucketNotification miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteBucketPolicy DeleteBucketPolicy.cc)
TARGET_LINK_LIBRARIES(DeleteBucketPolicy miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetBucketPolicy GetBucketPolicy.cc)
TARGET_LINK_LIBRARIES(GetBucketPolicy miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetBucketPolicy SetBucketPolicy.cc)
TARGET_LINK_LIBRARIES(SetBucketPolicy miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteBucketNotification DeleteBucketNotification.cc)
TARGET_LINK_LIBRARIES(DeleteBucketNotification miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetBucketNotification GetBucketNotification.cc)
TARGET_LINK_LIBRARIES(GetBucketNotification miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetBucketNotification SetBucketNotification.cc)
TARGET_LINK_LIBRARIES(SetBucketNotification miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteBucketEncryption DeleteBucketEncryption.cc)
TARGET_LINK_LIBRARIES(DeleteBucketEncryption miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetBucketEncryption GetBucketEncryption.cc)
TARGET_LINK_LIBRARIES(GetBucketEncryption miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetBucketEncryption SetBucketEncryption.cc)
TARGET_LINK_LIBRARIES(SetBucketEncryption miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetBucketVersioning GetBucketVersioning.cc)
TARGET_LINK_LIBRARIES(GetBucketVersioning miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetBucketVersioning SetBucketVersioning.cc)
TARGET_LINK_LIBRARIES(SetBucketVersioning miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteBucketReplication DeleteBucketReplication.cc)
TARGET_LINK_LIBRARIES(DeleteBucketReplication miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetBucketReplication GetBucketReplication.cc)
TARGET_LINK_LIBRARIES(GetBucketReplication miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetBucketReplication SetBucketReplication.cc)
TARGET_LINK_LIBRARIES(SetBucketReplication miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteBucketLifecycle DeleteBucketLifecycle.cc)
TARGET_LINK_LIBRARIES(DeleteBucketLifecycle miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetBucketLifecycle GetBucketLifecycle.cc)
TARGET_LINK_LIBRARIES(GetBucketLifecycle miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetBucketLifecycle SetBucketLifecycle.cc)
TARGET_LINK_LIBRARIES(SetBucketLifecycle miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteBucketTags DeleteBucketTags.cc)
TARGET_LINK_LIBRARIES(DeleteBucketTags miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetBucketTags GetBucketTags.cc)
TARGET_LINK_LIBRARIES(GetBucketTags miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetBucketTags SetBucketTags.cc)
TARGET_LINK_LIBRARIES(SetBucketTags miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteObjectLockConfig DeleteObjectLockConfig.cc)
TARGET_LINK_LIBRARIES(DeleteObjectLockConfig miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetObjectLockConfig GetObjectLockConfig.cc)
TARGET_LINK_LIBRARIES(GetObjectLockConfig miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetObjectLockConfig SetObjectLockConfig.cc)
TARGET_LINK_LIBRARIES(SetObjectLockConfig miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DeleteObjectTags DeleteObjectTags.cc)
TARGET_LINK_LIBRARIES(DeleteObjectTags miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetObjectTags GetObjectTags.cc)
TARGET_LINK_LIBRARIES(GetObjectTags miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetObjectTags SetObjectTags.cc)
TARGET_LINK_LIBRARIES(SetObjectTags miniocpp ${S3_LIBS})

ADD_EXECUTABLE(DisableObjectLegalHold DisableObjectLegalHold.cc)
TARGET_LINK_LIBRARIES(DisableObjectLegalHold miniocpp ${S3_LIBS})

ADD_EXECUTABLE(EnableObjectLegalHold EnableObjectLegalHold.cc)
TARGET_LINK_LIBRARIES(EnableObjectLegalHold miniocpp ${S3_LIBS})

ADD_EXECUTABLE(IsObjectLegalHoldEnabled IsObjectLegalHoldEnabled.cc)
TARGET_LINK_LIBRARIES(IsObjectLegalHoldEnabled miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetObjectRetention GetObjectRetention.cc)
TARGET_LINK_LIBRARIES(GetObjectRetention miniocpp ${S3_LIBS})

ADD_EXECUTABLE(SetObjectRetention SetObjectRetention.cc)
TARGET_LINK_LIBRARIES(SetObjectRetention miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetPresignedObjectUrl GetPresignedObjectUrl.cc)
TARGET_LINK_LIBRARIES(GetPresignedObjectUrl miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetPresignedPostFormData GetPresignedPostFormData.cc)
TARGET_LINK_LIBRARIES(GetPresignedPostFormData miniocpp ${S3_LIBS})

ADD_EXECUTABLE(PutObjectProgress PutObjectProgress.cc)
TARGET_LINK_LIBRARIES(PutObjectProgress miniocpp ${S3_LIBS})

ADD_EXECUTABLE(GetObjectProgress GetObjectProgress.cc)
TARGET_LINK_LIBRARIES(GetObjectProgress miniocpp ${S3_LIBS})
