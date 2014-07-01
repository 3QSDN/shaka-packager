// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_BASE_STREAM_INFO_H_
#define MEDIA_BASE_STREAM_INFO_H_

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"

namespace media {

enum StreamType {
  kStreamUnknown = 0,
  kStreamAudio,
  kStreamVideo,
};

/// Abstract class holds stream information.
class StreamInfo : public base::RefCountedThreadSafe<StreamInfo> {
 public:
  StreamInfo(StreamType stream_type,
             int track_id,
             uint32 time_scale,
             uint64 duration,
             const std::string& codec_string,
             const std::string& language,
             const uint8* extra_data,
             size_t extra_data_size,
             bool is_encrypted);

  /// @return true if this object has appropriate configuration values, false
  ///         otherwise.
  virtual bool IsValidConfig() const = 0;

  /// @return A human-readable string describing the stream info.
  virtual std::string ToString() const;

  StreamType stream_type() const { return stream_type_; }
  uint32 track_id() const { return track_id_; }
  uint32 time_scale() const { return time_scale_; }
  uint64 duration() const { return duration_; }
  const std::string& codec_string() const { return codec_string_; }
  const std::string& language() const { return language_; }

  bool is_encrypted() const { return is_encrypted_; }

  const std::vector<uint8>& extra_data() const { return extra_data_; }

  void set_duration(int duration) { duration_ = duration; }

  void set_extra_data(const std::vector<uint8>& data) { extra_data_ = data; }

 protected:
  friend class base::RefCountedThreadSafe<StreamInfo>;
  virtual ~StreamInfo();

 private:
  // Whether the stream is Audio or Video.
  StreamType stream_type_;
  uint32 track_id_;
  // The actual time is calculated as time / time_scale_ in seconds.
  uint32 time_scale_;
  // Duration base on time_scale.
  uint64 duration_;
  std::string codec_string_;
  std::string language_;
  // Whether the stream is potentially encrypted.
  // Note that in a potentially encrypted stream, individual buffers
  // can be encrypted or not encrypted.
  bool is_encrypted_;
  // Optional byte data required for some audio/video decoders such as Vorbis
  // codebooks.
  std::vector<uint8> extra_data_;

  // Not using DISALLOW_COPY_AND_ASSIGN here intentionally to allow the compiler
  // generated copy constructor and assignment operator. Since the extra data is
  // typically small, the performance impact is minimal.
};

}  // namespace media

#endif  // MEDIA_BASE_STREAM_INFO_H_
