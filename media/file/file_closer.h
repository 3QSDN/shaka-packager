// Copyright (c) 2013 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license tha can be
// found in the LICENSE file.

#ifndef MEDIA_FILE_FILE_CLOSER_H_
#define MEDIA_FILE_FILE_CLOSER_H_

#include "base/logging.h"
#include "media/file/file.h"

namespace media {

// Use by scoped_ptr to automatically close the file when it goes out of scope.
struct FileCloser {
  inline void operator()(File* file) const {
    if (file != NULL && !file->Close()) {
      LOG(WARNING) << "Failed to close the file properly: "
                   << file->file_name();
    }
  }
};

}  // namespace media

#endif  // MEDIA_FILE_FILE_CLOSER_H_
