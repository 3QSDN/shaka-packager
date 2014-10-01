// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_BASE_MEDIA_SAMPLE_H_
#define MEDIA_BASE_MEDIA_SAMPLE_H_

#include <deque>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"

namespace edash_packager {
namespace media {

/// Class to hold a media sample.
class MediaSample : public base::RefCountedThreadSafe<MediaSample> {
 public:
  /// Create a MediaSample object from input.
  /// @param data points to the buffer containing the sample data.
  ///        Must not be NULL.
  /// @param size indicates sample size in bytes. Must not be negative.
  /// @param is_key_frame indicates whether the sample is a key frame.
  static scoped_refptr<MediaSample> CopyFrom(const uint8_t* data,
                                             size_t size,
                                             bool is_key_frame);

  /// Create a MediaSample object from input.
  /// @param data points to the buffer containing the sample data.
  ///        Must not be NULL.
  /// @param side_data points to the buffer containing the additional data.
  ///        Some containers allow additional data to be specified.
  ///        Must not be NULL.
  /// @param size indicates sample size in bytes. Must not be negative.
  /// @param side_data_size indicates additional sample data size in bytes.
  ///        Must not be negative.
  /// @param is_key_frame indicates whether the sample is a key frame.
  static scoped_refptr<MediaSample> CopyFrom(const uint8_t* data,
                                             size_t size,
                                             const uint8_t* side_data,
                                             size_t side_data_size,
                                             bool is_key_frame);

  /// Create a MediaSample object with default members.
  static scoped_refptr<MediaSample> CreateEmptyMediaSample();

  /// Create a MediaSample indicating we've reached end of stream.
  /// Calling any method other than end_of_stream() on the resulting buffer
  /// is disallowed.
  static scoped_refptr<MediaSample> CreateEOSBuffer();

  int64_t dts() const {
    DCHECK(!end_of_stream());
    return dts_;
  }

  void set_dts(int64_t dts) { dts_ = dts; }

  int64_t pts() const {
    DCHECK(!end_of_stream());
    return pts_;
  }

  void set_pts(int64_t pts) { pts_ = pts; }

  int64_t duration() const {
    DCHECK(!end_of_stream());
    return duration_;
  }

  void set_duration(int64_t duration) {
    DCHECK(!end_of_stream());
    duration_ = duration;
  }

  bool is_key_frame() const {
    DCHECK(!end_of_stream());
    return is_key_frame_;
  }

  const uint8_t* data() const {
    DCHECK(!end_of_stream());
    return &data_[0];
  }

  uint8_t* writable_data() {
    DCHECK(!end_of_stream());
    return &data_[0];
  }

  size_t data_size() const {
    DCHECK(!end_of_stream());
    return data_.size();
  }

  const uint8_t* side_data() const {
    DCHECK(!end_of_stream());
    return &side_data_[0];
  }

  size_t side_data_size() const {
    DCHECK(!end_of_stream());
    return side_data_.size();
  }

  void set_data(const uint8_t* data, const size_t data_size) {
    data_.assign(data, data + data_size);
  }

  void set_is_key_frame(bool value) {
    is_key_frame_ = value;
  }

  // If there's no data in this buffer, it represents end of stream.
  bool end_of_stream() const { return data_.size() == 0; }

  /// @return a human-readable string describing |*this|.
  std::string ToString() const;

 private:
  friend class base::RefCountedThreadSafe<MediaSample>;

  // Create a MediaSample. Buffer will be padded and aligned as necessary.
  // |data|,|side_data| can be NULL, which indicates an empty sample.
  // |size|,|side_data_size| should not be negative.
  MediaSample(const uint8_t* data,
              size_t size,
              const uint8_t* side_data,
              size_t side_data_size,
              bool is_key_frame);
  MediaSample();
  virtual ~MediaSample();

  // Decoding time stamp.
  int64_t dts_;
  // Presentation time stamp.
  int64_t pts_;
  int64_t duration_;
  bool is_key_frame_;

  // Main buffer data.
  std::vector<uint8_t> data_;
  // Contain additional buffers to complete the main one. Needed by WebM
  // http://www.matroska.org/technical/specs/index.html BlockAdditional[A5].
  // Not used by mp4 and other containers.
  std::vector<uint8_t> side_data_;

  DISALLOW_COPY_AND_ASSIGN(MediaSample);
};

typedef std::deque<scoped_refptr<MediaSample> > BufferQueue;

}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_BASE_MEDIA_SAMPLE_H_
