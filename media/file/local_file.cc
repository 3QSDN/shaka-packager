// Copyright (c) 2013 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license tha can be
// found in the LICENSE file.

#include "media/file/local_file.h"

#include "base/file_util.h"
#include "base/logging.h"

namespace media {

LocalFile::LocalFile(const char* name, const char* mode)
    : File(name), file_mode_(mode), internal_file_(NULL) {}

bool LocalFile::Open() {
  internal_file_ =
      file_util::OpenFile(base::FilePath(file_name()), file_mode_.c_str());
  return (internal_file_ != NULL);
}

bool LocalFile::Close() {
  bool result = true;
  if (internal_file_) {
    result = file_util::CloseFile(internal_file_);
    internal_file_ = NULL;
  }
  delete this;
  return result;
}

int64 LocalFile::Read(void* buffer, uint64 length) {
  DCHECK(buffer != NULL);
  DCHECK(internal_file_ != NULL);
  return fread(buffer, sizeof(char), length, internal_file_);
}

int64 LocalFile::Write(const void* buffer, uint64 length) {
  DCHECK(buffer != NULL);
  DCHECK(internal_file_ != NULL);
  return fwrite(buffer, sizeof(char), length, internal_file_);
}

int64 LocalFile::Size() {
  DCHECK(internal_file_ != NULL);

  // Flush any buffered data, so we get the true file size.
  if (!Flush()) {
    LOG(ERROR) << "Cannot flush file.";
    return -1;
  }

  int64 file_size;
  if (!file_util::GetFileSize(base::FilePath(file_name()), &file_size)) {
    LOG(ERROR) << "Cannot get file size.";
    return -1;
  }
  return file_size;
}

bool LocalFile::Flush() {
  DCHECK(internal_file_ != NULL);
  return ((fflush(internal_file_) == 0) && !ferror(internal_file_));
}

bool LocalFile::Eof() {
  DCHECK(internal_file_ != NULL);
  return static_cast<bool>(feof(internal_file_));
}

}  // namespace media
