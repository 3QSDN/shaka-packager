// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/base/text_stream_info.h"

namespace shaka {
namespace media {

TextStreamInfo::TextStreamInfo(int track_id,
                               uint32_t time_scale,
                               uint64_t duration,
                               const std::string& codec_string,
                               const std::string& language,
                               const std::string& codec_config,
                               uint16_t width,
                               uint16_t height)
    : StreamInfo(kStreamText,
                 track_id,
                 time_scale,
                 duration,
                 codec_string,
                 language,
                 reinterpret_cast<const uint8_t*>(codec_config.data()),
                 codec_config.size(),
                 false),
      width_(width),
      height_(height) {}

TextStreamInfo::~TextStreamInfo() {}

bool TextStreamInfo::IsValidConfig() const {
  return true;
}

}  // namespace media
}  // namespace shaka
