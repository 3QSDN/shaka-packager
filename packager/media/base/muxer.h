// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Defines the muxer interface.

#ifndef MEDIA_BASE_MUXER_H_
#define MEDIA_BASE_MUXER_H_

#include <memory>
#include <vector>

#include "packager/base/time/clock.h"
#include "packager/media/base/fourccs.h"
#include "packager/media/base/media_handler.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/status.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/event/progress_listener.h"

namespace shaka {
namespace media {

class KeySource;
class MediaSample;
class MediaStream;

/// Muxer is responsible for taking elementary stream samples and producing
/// media containers. An optional KeySource can be provided to Muxer
/// to generate encrypted outputs.
class Muxer : public MediaHandler {
 public:
  explicit Muxer(const MuxerOptions& options);
  virtual ~Muxer();

  // TODO(kqyang): refactor max_sd_pixels through crypto_period_duration into
  // an encapsulated EncryptionParams structure.

  /// Set encryption key source.
  /// @param encryption_key_source points to the encryption key source. The
  ///        caller retains ownership, and should not be NULL.
  /// @param max_sd_pixels specifies the threshold to determine whether a video
  ///        track should be considered as SD. If the max pixels per frame is
  ///        no higher than max_sd_pixels, it is SD.
  /// @param max_hd_pixels specifies the threshold to determine whether a video
  ///        track should be considered as HD. If the max pixels per frame is
  ///        higher than max_sd_pixels, but no higher than max_hd_pixels,
  ///        it is HD.
  /// @param max_uhd1_pixels specifies the threshold to determine whether a video
  ///        track should be considered as UHD1. If the max pixels per frame is
  ///        higher than max_hd_pixels, but no higher than max_uhd1_pixels,
  ///        it is UHD1. Otherwise it is UHD2.
  /// @param clear_lead_in_seconds specifies clear lead duration in seconds.
  /// @param crypto_period_duration_in_seconds specifies crypto period duration
  ///        in seconds. A positive value means key rotation is enabled, the
  ///        key source must support key rotation in this case.
  /// @param protection_scheme specifies the protection scheme: 'cenc', 'cens',
  ///        'cbc1', 'cbcs'.
  void SetKeySource(KeySource* encryption_key_source,
                    uint32_t max_sd_pixels,
                    uint32_t max_hd_pixels,
                    uint32_t max_uhd1_pixels,
                    double clear_lead_in_seconds,
                    double crypto_period_duration_in_seconds,
                    FourCC protection_scheme);

  /// Cancel a muxing job in progress. Will cause @a Run to exit with an error
  /// status of type CANCELLED.
  void Cancel();

  /// Set a MuxerListener event handler for this object.
  /// @param muxer_listener should not be NULL.
  void SetMuxerListener(std::unique_ptr<MuxerListener> muxer_listener);

  /// Set a ProgressListener event handler for this object.
  /// @param progress_listener should not be NULL.
  void SetProgressListener(std::unique_ptr<ProgressListener> progress_listener);

  const std::vector<std::shared_ptr<StreamInfo>>& streams() const {
    return streams_;
  }

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
  /// @name MediaHandler implementation overrides.
  /// @{
  Status InitializeInternal() override { return Status::OK; }
  Status Process(std::unique_ptr<StreamData> stream_data) override;
  Status OnFlushRequest(size_t input_stream_index) override { return Finalize(); }
  /// @}

  const MuxerOptions& options() const { return options_; }
  KeySource* encryption_key_source() {
    return encryption_key_source_;
  }
  uint32_t max_sd_pixels() const { return max_sd_pixels_; }
  uint32_t max_hd_pixels() const { return max_hd_pixels_; }
  uint32_t max_uhd1_pixels() const { return max_uhd1_pixels_; }
  double clear_lead_in_seconds() const { return clear_lead_in_seconds_; }
  double crypto_period_duration_in_seconds() const {
    return crypto_period_duration_in_seconds_;
  }
  MuxerListener* muxer_listener() { return muxer_listener_.get(); }
  ProgressListener* progress_listener() { return progress_listener_.get(); }
  base::Clock* clock() { return clock_; }
  FourCC protection_scheme() const { return protection_scheme_; }

 private:
  // Initialize the muxer.
  virtual Status InitializeMuxer() = 0;

  // Final clean up.
  virtual Status Finalize() = 0;

  // Add a new sample.
  virtual Status AddSample(size_t stream_id,
                           std::shared_ptr<MediaSample> sample) = 0;

  // Finalize the segment or subsegment.
  virtual Status FinalizeSegment(size_t stream_id,
                                 std::shared_ptr<SegmentInfo> segment_info) = 0;

  MuxerOptions options_;
  std::vector<std::shared_ptr<StreamInfo>> streams_;
  KeySource* encryption_key_source_;
  uint32_t max_sd_pixels_;
  uint32_t max_hd_pixels_;
  uint32_t max_uhd1_pixels_;
  double clear_lead_in_seconds_;
  double crypto_period_duration_in_seconds_;
  FourCC protection_scheme_;
  bool cancelled_;

  std::unique_ptr<MuxerListener> muxer_listener_;
  std::unique_ptr<ProgressListener> progress_listener_;
  // An external injected clock, can be NULL.
  base::Clock* clock_;

  DISALLOW_COPY_AND_ASSIGN(Muxer);
};

}  // namespace media
}  // namespace shaka

#endif  // MEDIA_BASE_MUXER_H_
