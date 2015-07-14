// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Implementation of MuxerListener that converts the info to a MediaInfo
// protobuf and dumps it to a file.
// This is specifically for VOD.

#ifndef MEDIA_EVENT_VOD_MEDIA_INFO_DUMP_MUXER_LISTENER_H_
#define MEDIA_EVENT_VOD_MEDIA_INFO_DUMP_MUXER_LISTENER_H_

#include <string>
#include <vector>

#include "packager/base/compiler_specific.h"
#include "packager/base/memory/scoped_ptr.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/event/muxer_listener.h"

namespace edash_packager {

class MediaInfo;

namespace media {

class VodMediaInfoDumpMuxerListener : public MuxerListener {
 public:
  VodMediaInfoDumpMuxerListener(const std::string& output_file_name);
  virtual ~VodMediaInfoDumpMuxerListener();

  /// If the stream is encrypted use this as 'schemeIdUri' attribute for
  /// ContentProtection element.
  void SetContentProtectionSchemeIdUri(const std::string& scheme_id_uri);

  /// @name MuxerListener implementation overrides.
  /// @{
  virtual void OnEncryptionInfoReady(
      const std::string& content_protection_uuid,
      const std::string& content_protection_name_version,
      const std::vector<uint8_t>& default_key_id,
      const std::vector<uint8_t>& pssh) OVERRIDE;
  virtual void OnMediaStart(
      const MuxerOptions& muxer_options,
      const StreamInfo& stream_info,
      uint32_t time_scale,
      ContainerType container_type) OVERRIDE;
  virtual void OnSampleDurationReady(uint32_t sample_duration) OVERRIDE;
  virtual void OnMediaEnd(bool has_init_range,
                          uint64_t init_range_start,
                          uint64_t init_range_end,
                          bool has_index_range,
                          uint64_t index_range_start,
                          uint64_t index_range_end,
                          float duration_seconds,
                          uint64_t file_size) OVERRIDE;
  virtual void OnNewSegment(uint64_t start_time,
                            uint64_t duration,
                            uint64_t segment_file_size) OVERRIDE;
  /// @}

 private:
  // Write |media_info_| to |output_file_name_|.
  bool SerializeMediaInfoToFile();

  std::string output_file_name_;
  std::string scheme_id_uri_;
  scoped_ptr<MediaInfo> media_info_;

  bool is_encrypted_;
  // Storage for values passed to OnEncryptionInfoReady().
  std::string content_protection_uuid_;
  std::string content_protection_name_version_;
  std::string default_key_id_;
  std::string pssh_;

  DISALLOW_COPY_AND_ASSIGN(VodMediaInfoDumpMuxerListener);
};

}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_EVENT_VOD_MEDIA_INFO_DUMP_MUXER_LISTENER_H_
