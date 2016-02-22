// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_FORMATS_MP4_VIDEO_SLICE_HEADER_PARSER_H_
#define MEDIA_FORMATS_MP4_VIDEO_SLICE_HEADER_PARSER_H_

#include <vector>

#include "packager/media/base/macros.h"
#include "packager/media/filters/h264_parser.h"

namespace edash_packager {
namespace media {
namespace mp4 {

class VideoSliceHeaderParser {
 public:
  VideoSliceHeaderParser() {}
  virtual ~VideoSliceHeaderParser() {}

  /// Adds decoder configuration from the given data.  This must be called
  /// once before any calls to GetHeaderSize.
  virtual bool Initialize(
      const std::vector<uint8_t>& decoder_configuration) = 0;

  /// Gets the header size of the given NALU.  Returns < 0 on error.
  virtual int64_t GetHeaderSize(const Nalu& nalu) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(VideoSliceHeaderParser);
};

class H264VideoSliceHeaderParser : public VideoSliceHeaderParser {
 public:
  H264VideoSliceHeaderParser();
  ~H264VideoSliceHeaderParser() override;

  /// @name VideoSliceHeaderParser implementation overrides.
  /// @{
  bool Initialize(const std::vector<uint8_t>& decoder_configuration) override;
  int64_t GetHeaderSize(const Nalu& nalu) override;
  /// @}

 private:
  H264Parser parser_;

  DISALLOW_COPY_AND_ASSIGN(H264VideoSliceHeaderParser);
};

// TODO(modmaker): Add H.265 parser.

}  // namespace mp4
}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_FORMATS_MP4_VIDEO_SLICE_HEADER_PARSER_H_

