// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MP4_AAC_AUDIO_SPECIFIC_CONFIG_H_
#define MEDIA_MP4_AAC_AUDIO_SPECIFIC_CONFIG_H_

#include <vector>

#include "base/basictypes.h"

namespace media {

class BitReader;

namespace mp4 {

/// This class parses the AAC information from decoder specific information
/// embedded in the @b esds box in an ISO BMFF file.
/// Please refer to ISO 14496 Part 3 Table 1.13 - Syntax of AudioSpecificConfig
/// for more details.
class AACAudioSpecificConfig {
 public:
  AACAudioSpecificConfig();
  ~AACAudioSpecificConfig();

  /// Parse the AAC config from decoder specific information embedded in an @b
  /// esds box. The function will parse the data and get the
  /// ElementaryStreamDescriptor, then it will parse the
  /// ElementaryStreamDescriptor to get audio stream configurations.
  /// @param data contains decoder specific information from an @b esds box.
  /// @return true if successful, false otherwise.
  bool Parse(const std::vector<uint8>& data);

  /// @param sbr_in_mimetype indicates whether SBR mode is specified in the
  ///        mimetype, i.e. codecs parameter contains mp4a.40.5.
  /// @return Output sample rate for the AAC stream.
  uint32 GetOutputSamplesPerSecond(bool sbr_in_mimetype) const;

  /// @param sbr_in_mimetype indicates whether SBR mode is specified in the
  ///        mimetype, i.e. codecs parameter contains mp4a.40.5.
  /// @return Number of channels for the AAC stream.
  uint8 GetNumChannels(bool sbr_in_mimetype) const;

  /// Convert a raw AAC frame into an AAC frame with an ADTS header.
  /// @param[in,out] buffer contains the raw AAC frame on input, and the
  ///                converted frame on output if successful; it is untouched
  ///                on failure.
  /// @return true on success, false otherwise.
  bool ConvertToADTS(std::vector<uint8>* buffer) const;

  /// @return The audio object type for this AAC config.
  uint8 audio_object_type() const {
    return audio_object_type_;
  }

  /// @return The sampling frequency for this AAC config.
  uint32 frequency() const {
    return frequency_;
  }

  /// @return Number of channels for this AAC config.
  uint8 num_channels() const {
    return num_channels_;
  }

  /// Size in bytes of the ADTS header added by ConvertEsdsToADTS().
  static const size_t kADTSHeaderSize = 7;

 private:
  bool SkipDecoderGASpecificConfig(BitReader* bit_reader) const;
  bool SkipErrorSpecificConfig() const;
  bool SkipGASpecificConfig(BitReader* bit_reader) const;

  // The following variables store the AAC specific configuration information
  // that are used to generate the ADTS header.
  uint8 audio_object_type_;
  uint8 frequency_index_;
  uint8 channel_config_;
  // Is Parametric Stereo on?
  bool ps_present_;

  // The following variables store audio configuration information.
  // They are based on the AAC specific configuration but can be overridden
  // by extensions in elementary stream descriptor.
  uint32 frequency_;
  uint32 extension_frequency_;
  uint8 num_channels_;
};

}  // namespace mp4
}  // namespace media

#endif  // MEDIA_MP4_AAC_AUDIO_SPECIFIC_CONFIG_H_
