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

#include "packager/base/memory/ref_counted.h"
#include "packager/base/memory/scoped_ptr.h"
#include "packager/base/time/clock.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/status.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/event/progress_listener.h"

namespace edash_packager {
namespace media {

class KeySource;
class MediaSample;
class MediaStream;

/// Muxer is responsible for taking elementary stream samples and producing
/// media containers. An optional KeySource can be provided to Muxer
/// to generate encrypted outputs.
class Muxer {
 public:
  explicit Muxer(const MuxerOptions& options);
  virtual ~Muxer();

  /// Set encryption key source.
  /// @param encryption_key_source points to the encryption key source. The
  ///        caller retains ownership, and should not be NULL.
  /// @param max_sd_pixels specifies the threshold to determine whether a video
  ///        track should be considered as SD or HD. If the track has more
  ///        pixels per frame than max_sd_pixels, it is HD, SD otherwise.
  /// @param clear_lead_in_seconds specifies clear lead duration in seconds.
  /// @param crypto_period_duration_in_seconds specifies crypto period duration
  ///        in seconds. A positive value means key rotation is enabled, the
  ///        key source must support key rotation in this case.
  void SetKeySource(KeySource* encryption_key_source,
                    uint32_t max_sd_pixels,
                    double clear_lead_in_seconds,
                    double crypto_period_duration_in_seconds);

  /// Add video/audio stream.
  void AddStream(MediaStream* stream);

  /// Drive the remuxing from muxer side (pull).
  Status Run();

  /// Cancel a muxing job in progress. Will cause @a Run to exit with an error
  /// status of type CANCELLED.
  void Cancel();

  /// Set a MuxerListener event handler for this object.
  /// @param muxer_listener should not be NULL.
  void SetMuxerListener(scoped_ptr<MuxerListener> muxer_listener);

  /// Set a ProgressListener event handler for this object.
  /// @param progress_listener should not be NULL.
  void SetProgressListener(scoped_ptr<ProgressListener> progress_listener);

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
  KeySource* encryption_key_source() {
    return encryption_key_source_;
  }
  uint32_t max_sd_pixels() const { return max_sd_pixels_; }
  double clear_lead_in_seconds() const { return clear_lead_in_seconds_; }
  double crypto_period_duration_in_seconds() const {
    return crypto_period_duration_in_seconds_;
  }
  MuxerListener* muxer_listener() { return muxer_listener_.get(); }
  ProgressListener* progress_listener() { return progress_listener_.get(); }
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
  bool initialized_;
  std::vector<MediaStream*> streams_;
  KeySource* encryption_key_source_;
  uint32_t max_sd_pixels_;
  double clear_lead_in_seconds_;
  double crypto_period_duration_in_seconds_;
  bool cancelled_;

  scoped_ptr<MuxerListener> muxer_listener_;
  scoped_ptr<ProgressListener> progress_listener_;
  // An external injected clock, can be NULL.
  base::Clock* clock_;

  DISALLOW_COPY_AND_ASSIGN(Muxer);
};

}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_BASE_MUXER_H_
