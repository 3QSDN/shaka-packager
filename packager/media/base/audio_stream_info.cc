// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/base/audio_stream_info.h"

#include "packager/base/logging.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/media/base/limits.h"

namespace edash_packager {
namespace media {

namespace {
std::string AudioCodecToString(AudioCodec audio_codec) {
  switch (audio_codec) {
    case kCodecAAC:
      return "AAC";
    case kCodecAC3:
      return "AC3";
    case kCodecDTSC:
      return "DTSC";
    case kCodecDTSE:
      return "DTSE";
    case kCodecDTSH:
      return "DTSH";
    case kCodecDTSL:
      return "DTSL";
    case kCodecDTSM:
      return "DTS-";
    case kCodecDTSP:
      return "DTS+";
    case kCodecEAC3:
      return "EAC3";
    case kCodecOpus:
      return "Opus";
    case kCodecVorbis:
      return "Vorbis";
    default:
      NOTIMPLEMENTED() << "Unknown Audio Codec: " << audio_codec;
      return "UnknownAudioCodec";
  }
}
}  // namespace

AudioStreamInfo::AudioStreamInfo(int track_id,
                                 uint32_t time_scale,
                                 uint64_t duration,
                                 AudioCodec codec,
                                 const std::string& codec_string,
                                 const std::string& language,
                                 uint8_t sample_bits,
                                 uint8_t num_channels,
                                 uint32_t sampling_frequency,
                                 uint32_t max_bitrate,
                                 uint32_t avg_bitrate,
                                 const uint8_t* extra_data,
                                 size_t extra_data_size,
                                 bool is_encrypted)
    : StreamInfo(kStreamAudio,
                 track_id,
                 time_scale,
                 duration,
                 codec_string,
                 language,
                 extra_data,
                 extra_data_size,
                 is_encrypted),
      codec_(codec),
      sample_bits_(sample_bits),
      num_channels_(num_channels),
      sampling_frequency_(sampling_frequency),
      max_bitrate_(max_bitrate),
      avg_bitrate_(avg_bitrate) {}

AudioStreamInfo::~AudioStreamInfo() {}

bool AudioStreamInfo::IsValidConfig() const {
  return codec_ != kUnknownAudioCodec && num_channels_ != 0 &&
         num_channels_ <= limits::kMaxChannels && sample_bits_ > 0 &&
         sample_bits_ <= limits::kMaxBitsPerSample &&
         sampling_frequency_ > 0 &&
         sampling_frequency_ <= limits::kMaxSampleRate;
}

std::string AudioStreamInfo::ToString() const {
  return base::StringPrintf(
      "%s codec: %s\n sample_bits: %d\n num_channels: %d\n "
      "sampling_frequency: %d\n language: %s\n",
      StreamInfo::ToString().c_str(), AudioCodecToString(codec_).c_str(),
      sample_bits_, num_channels_, sampling_frequency_, language().c_str());
}

std::string AudioStreamInfo::GetCodecString(AudioCodec codec,
                                            uint8_t audio_object_type) {
  switch (codec) {
    case kCodecVorbis:
      return "vorbis";
    case kCodecOpus:
      return "opus";
    case kCodecAAC:
      return "mp4a.40." + base::UintToString(audio_object_type);
    case kCodecDTSC:
      return "dtsc";
    case kCodecDTSH:
      return "dtsh";
    case kCodecDTSL:
      return "dtsl";
    case kCodecDTSE:
      return "dtse";
    case kCodecDTSP:
      return "dts+";
    case kCodecDTSM:
      return "dts-";
    case kCodecAC3:
      return "ac-3";
    case kCodecEAC3:
      return "ec-3";
    default:
      NOTIMPLEMENTED() << "Codec: " << codec;
      return "unknown";
  }
}

}  // namespace media
}  // namespace edash_packager
