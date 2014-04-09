// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_FILE_LOCAL_FILE_H_
#define PACKAGER_FILE_LOCAL_FILE_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "media/file/file.h"

namespace media {

/// Implement LocalFile which deals with local storage.
class LocalFile : public File {
 public:
  /// @param file_name C string containing the name of the file to be accessed.
  /// @param mode C string containing a file access mode, refer to fopen for
  ///        the available modes.
  LocalFile(const char* file_name, const char* mode);

  /// @name File implementation overrides.
  /// @{
  virtual bool Close() OVERRIDE;
  virtual int64 Read(void* buffer, uint64 length) OVERRIDE;
  virtual int64 Write(const void* buffer, uint64 length) OVERRIDE;
  virtual int64 Size() OVERRIDE;
  virtual bool Flush() OVERRIDE;
  virtual bool Eof() OVERRIDE;
  /// @}

 protected:
  virtual ~LocalFile();

  virtual bool Open() OVERRIDE;

 private:
  std::string file_mode_;
  FILE* internal_file_;

  DISALLOW_COPY_AND_ASSIGN(LocalFile);
};

}  // namespace media

#endif  // PACKAGER_FILE_LOCAL_FILE_H_

