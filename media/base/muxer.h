// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Defines the muxer interface.

#ifndef MEDIA_BASE_MUXER_H_
#define MEDIA_BASE_MUXER_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "media/base/encryption_key_source.h"
#include "media/base/muxer_options.h"
#include "media/base/status.h"

namespace base {
class Clock;
}

namespace media {

class EncryptionKeySource;
class MediaSample;
class MediaStream;

namespace event {
class MuxerListener;
}

/// Muxer is responsible for taking elementary stream samples and producing
/// media containers. An optional EncryptionKeySource can be provided to Muxer
/// to generate encrypted outputs.
class Muxer {
 public:
  explicit Muxer(const MuxerOptions& options);
  virtual ~Muxer();

  /// Set encryption key source.
  /// @param encryption_key_source points to the encryption key source to be
  ///        injected. Should not be NULL.
  /// @param track_type should be either SD or HD. It affects whether SD key or
  ///        HD key is used to encrypt the video content.
  /// @param clear_lead_in_seconds specifies clear lead duration in seconds.
  void SetEncryptionKeySource(EncryptionKeySource* encryption_key_source,
                              EncryptionKeySource::TrackType track_type,
                              double clear_lead_in_seconds);

  /// Add video/audio stream.
  void AddStream(MediaStream* stream);

  /// Drive the remuxing from muxer side (pull).
  Status Run();

  /// Set a MuxerListener event handler for this object.
  /// @param muxer_listener should not be NULL.
  void SetMuxerListener(event::MuxerListener* muxer_listener);

  const std::vector<MediaStream*>& streams() const { return streams_; }

  /// Inject clock, mainly used for testing.
  /// The injected clock will be used to generate the creation time-stamp and
  /// modification time-stamp of the muxer output.
  /// If no clock is injected, the code uses base::Time::Now() to generate the
  /// time-stamps.
  /// @param clock is the Clock to be injected.
  void set_clock(base::Clock* clock) {
    clock_ = clock;
  }

 protected:
  const MuxerOptions& options() const { return options_; }
  EncryptionKeySource* encryption_key_source() {
    return encryption_key_source_;
  }
  EncryptionKeySource::TrackType track_type() const { return track_type_; }
  double clear_lead_in_seconds() const { return clear_lead_in_seconds_; }
  event::MuxerListener* muxer_listener() { return muxer_listener_; }
  base::Clock* clock() { return clock_; }

 private:
  friend class MediaStream;  // Needed to access AddSample.

  // Add new media sample.
  Status AddSample(const MediaStream* stream,
                   scoped_refptr<MediaSample> sample);

  // Initialize the muxer.
  virtual Status Initialize() = 0;

  // Final clean up.
  virtual Status Finalize() = 0;

  // AddSample implementation.
  virtual Status DoAddSample(const MediaStream* stream,
                             scoped_refptr<MediaSample> sample) = 0;

  MuxerOptions options_;
  std::vector<MediaStream*> streams_;
  EncryptionKeySource* encryption_key_source_;
  bool initialized_;
  EncryptionKeySource::TrackType track_type_;
  double clear_lead_in_seconds_;

  event::MuxerListener* muxer_listener_;
  // An external injected clock, can be NULL.
  base::Clock* clock_;

  DISALLOW_COPY_AND_ASSIGN(Muxer);
};

}  // namespace media

#endif  // MEDIA_BASE_MUXER_H_
