// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/hls/base/media_playlist.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "packager/base/logging.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/media/base/language_utils.h"
#include "packager/media/file/file.h"
#include "packager/media/file/file_closer.h"
#include "packager/version/version.h"

namespace shaka {
namespace hls {

namespace {
uint32_t GetTimeScale(const MediaInfo& media_info) {
  if (media_info.has_reference_time_scale())
    return media_info.reference_time_scale();

  if (media_info.has_video_info())
    return media_info.video_info().time_scale();

  if (media_info.has_audio_info())
    return media_info.audio_info().time_scale();
  return 0u;
}

std::string CreatePlaylistHeader(const std::string& init_segment_name,
                                 uint32_t target_duration,
                                 MediaPlaylist::MediaPlaylistType type,
                                 int media_sequence_number,
                                 int discontinuity_sequence_number) {
  const std::string version = GetPackagerVersion();
  std::string version_line;
  if (!version.empty()) {
    version_line =
        base::StringPrintf("## Generated with %s version %s\n",
                           GetPackagerProjectUrl().c_str(), version.c_str());
  }

  // 6 is required for EXT-X-MAP without EXT-X-I-FRAMES-ONLY.
  std::string header = base::StringPrintf(
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "%s"
      "#EXT-X-TARGETDURATION:%d\n",
      version_line.c_str(), target_duration);

  switch (type) {
    case MediaPlaylist::MediaPlaylistType::kVod:
      header += "#EXT-X-PLAYLIST-TYPE:VOD\n";
      break;
    case MediaPlaylist::MediaPlaylistType::kEvent:
      header += "#EXT-X-PLAYLIST-TYPE:EVENT\n";
      break;
    case MediaPlaylist::MediaPlaylistType::kLive:
      if (media_sequence_number > 0) {
        base::StringAppendF(&header, "#EXT-X-MEDIA-SEQUENCE:%d\n",
                            media_sequence_number);
      }
      if (discontinuity_sequence_number > 0) {
        base::StringAppendF(&header, "#EXT-X-DISCONTINUITY-SEQUENCE:%d\n",
                            discontinuity_sequence_number);
      }
      break;
    default:
      NOTREACHED() << "Unexpected MediaPlaylistType " << static_cast<int>(type);
  }

  // Put EXT-X-MAP at the end since the rest of the playlist is about the
  // segment and key info.
  if (!init_segment_name.empty()) {
    header += "#EXT-X-MAP:URI=\"" + init_segment_name + "\"\n";
  }

  return header;
}

class SegmentInfoEntry : public HlsEntry {
 public:
  SegmentInfoEntry(const std::string& file_name,
                   double start_time,
                   double duration);
  ~SegmentInfoEntry() override;

  std::string ToString() override;
  double start_time() const { return start_time_; }
  double duration() const { return duration_; }

 private:
  const std::string file_name_;
  const double start_time_;
  const double duration_;

  DISALLOW_COPY_AND_ASSIGN(SegmentInfoEntry);
};

SegmentInfoEntry::SegmentInfoEntry(const std::string& file_name,
                                   double start_time,
                                   double duration)
    : HlsEntry(HlsEntry::EntryType::kExtInf),
      file_name_(file_name),
      start_time_(start_time),
      duration_(duration) {}
SegmentInfoEntry::~SegmentInfoEntry() {}

std::string SegmentInfoEntry::ToString() {
  return base::StringPrintf("#EXTINF:%.3f,\n%s\n", duration_,
                            file_name_.c_str());
}

class EncryptionInfoEntry : public HlsEntry {
 public:
  EncryptionInfoEntry(MediaPlaylist::EncryptionMethod method,
                      const std::string& url,
                      const std::string& key_id,
                      const std::string& iv,
                      const std::string& key_format,
                      const std::string& key_format_versions);

  ~EncryptionInfoEntry() override;

  std::string ToString() override;

 private:
  const MediaPlaylist::EncryptionMethod method_;
  const std::string url_;
  const std::string key_id_;
  const std::string iv_;
  const std::string key_format_;
  const std::string key_format_versions_;

  DISALLOW_COPY_AND_ASSIGN(EncryptionInfoEntry);
};

EncryptionInfoEntry::EncryptionInfoEntry(MediaPlaylist::EncryptionMethod method,
                                         const std::string& url,
                                         const std::string& key_id,
                                         const std::string& iv,
                                         const std::string& key_format,
                                         const std::string& key_format_versions)
    : HlsEntry(HlsEntry::EntryType::kExtKey),
      method_(method),
      url_(url),
      key_id_(key_id),
      iv_(iv),
      key_format_(key_format),
      key_format_versions_(key_format_versions) {}

EncryptionInfoEntry::~EncryptionInfoEntry() {}

std::string EncryptionInfoEntry::ToString() {
  std::string method_attribute;
  if (method_ == MediaPlaylist::EncryptionMethod::kSampleAes) {
    method_attribute = "METHOD=SAMPLE-AES";
  } else if (method_ == MediaPlaylist::EncryptionMethod::kAes128) {
    method_attribute = "METHOD=AES-128";
  } else if (method_ == MediaPlaylist::EncryptionMethod::kSampleAesCenc) {
    method_attribute = "METHOD=SAMPLE-AES-CENC";
  } else {
    DCHECK(method_ == MediaPlaylist::EncryptionMethod::kNone);
    method_attribute = "METHOD=NONE";
  }
  std::string ext_key = "#EXT-X-KEY:" + method_attribute + ",URI=\"" + url_ +
                        "\"";
  if (!key_id_.empty()) {
    ext_key += ",KEYID=" + key_id_;
  }
  if (!iv_.empty()) {
    ext_key += ",IV=" + iv_;
  }
  if (!key_format_versions_.empty()) {
    ext_key += ",KEYFORMATVERSIONS=\"" + key_format_versions_ + "\"";
  }
  if (key_format_.empty())
    return ext_key + "\n";

  return ext_key + ",KEYFORMAT=\"" + key_format_ + "\"\n";
}

class DiscontinuityEntry : public HlsEntry {
 public:
  DiscontinuityEntry();

  ~DiscontinuityEntry() override;

  std::string ToString() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(DiscontinuityEntry);
};

DiscontinuityEntry::DiscontinuityEntry()
    : HlsEntry(HlsEntry::EntryType::kExtDiscontinuity) {}

DiscontinuityEntry::~DiscontinuityEntry() {}

std::string DiscontinuityEntry::ToString() {
  return "#EXT-X-DISCONTINUITY\n";
}

double LatestSegmentStartTime(
    const std::list<std::unique_ptr<HlsEntry>>& entries) {
  DCHECK(!entries.empty());
  for (auto iter = entries.rbegin(); iter != entries.rend(); ++iter) {
    if (iter->get()->type() == HlsEntry::EntryType::kExtInf) {
      const SegmentInfoEntry* segment_info =
          reinterpret_cast<SegmentInfoEntry*>(iter->get());
      return segment_info->start_time();
    }
  }
  return 0.0;
}

}  // namespace

HlsEntry::HlsEntry(HlsEntry::EntryType type) : type_(type) {}
HlsEntry::~HlsEntry() {}

MediaPlaylist::MediaPlaylist(MediaPlaylistType type,
                             double time_shift_buffer_depth,
                             const std::string& file_name,
                             const std::string& name,
                             const std::string& group_id)
    : type_(type),
      time_shift_buffer_depth_(time_shift_buffer_depth),
      file_name_(file_name),
      name_(name),
      group_id_(group_id) {}

MediaPlaylist::~MediaPlaylist() {}

void MediaPlaylist::SetStreamTypeForTesting(
    MediaPlaylistStreamType stream_type) {
  stream_type_ = stream_type;
}

void MediaPlaylist::SetCodecForTesting(const std::string& codec) {
  codec_ = codec;
}

bool MediaPlaylist::SetMediaInfo(const MediaInfo& media_info) {
  const uint32_t time_scale = GetTimeScale(media_info);
  if (time_scale == 0) {
    LOG(ERROR) << "MediaInfo does not contain a valid timescale.";
    return false;
  }

  if (media_info.has_video_info()) {
    stream_type_ = MediaPlaylistStreamType::kPlayListVideo;
    codec_ = media_info.video_info().codec();
  } else if (media_info.has_audio_info()) {
    stream_type_ = MediaPlaylistStreamType::kPlayListAudio;
    codec_ = media_info.audio_info().codec();
  } else {
    NOTIMPLEMENTED();
    return false;
  }

  time_scale_ = time_scale;
  media_info_ = media_info;
  return true;
}

void MediaPlaylist::AddSegment(const std::string& file_name,
                               uint64_t start_time,
                               uint64_t duration,
                               uint64_t size) {
  if (time_scale_ == 0) {
    LOG(WARNING) << "Timescale is not set and the duration for " << duration
                 << " cannot be calculated. The output will be wrong.";

    entries_.emplace_back(new SegmentInfoEntry(file_name, 0.0, 0.0));
    return;
  }

  const double start_time_seconds =
      static_cast<double>(start_time) / time_scale_;
  const double segment_duration_seconds =
      static_cast<double>(duration) / time_scale_;
  if (segment_duration_seconds > longest_segment_duration_)
    longest_segment_duration_ = segment_duration_seconds;

  const int kBitsInByte = 8;
  const uint64_t bitrate = kBitsInByte * size / segment_duration_seconds;
  max_bitrate_ = std::max(max_bitrate_, bitrate);
  entries_.emplace_back(new SegmentInfoEntry(file_name, start_time_seconds,
                                             segment_duration_seconds));
  SlideWindow();
}

void MediaPlaylist::AddEncryptionInfo(MediaPlaylist::EncryptionMethod method,
                                      const std::string& url,
                                      const std::string& key_id,
                                      const std::string& iv,
                                      const std::string& key_format,
                                      const std::string& key_format_versions) {
  if (!inserted_discontinuity_tag_) {
    // Insert discontinuity tag only for the first EXT-X-KEY, only if there
    // are non-encrypted media segments.
    if (!entries_.empty())
      entries_.emplace_back(new DiscontinuityEntry());
    inserted_discontinuity_tag_ = true;
  }
  entries_.emplace_back(new EncryptionInfoEntry(
      method, url, key_id, iv, key_format, key_format_versions));
}

bool MediaPlaylist::WriteToFile(const std::string& file_path) {
  if (!target_duration_set_) {
    SetTargetDuration(ceil(GetLongestSegmentDuration()));
  }

  std::string header = CreatePlaylistHeader(
      media_info_.init_segment_name(), target_duration_, type_,
      media_sequence_number_, discontinuity_sequence_number_);

  std::string body;
  for (const auto& entry : entries_)
    body.append(entry->ToString());

  std::string content = header + body;

  if (type_ == MediaPlaylistType::kVod) {
    content += "#EXT-X-ENDLIST\n";
  }

  std::unique_ptr<media::File, media::FileCloser> file(
      media::File::Open(file_path.c_str(), "w"));
  if (!file) {
    LOG(ERROR) << "Failed to open file " << file_path;
    return false;
  }
  int64_t bytes_written = file->Write(content.data(), content.size());
  if (bytes_written < 0) {
    LOG(ERROR) << "Error while writing playlist to file.";
    return false;
  }

  // TODO(rkuroiwa): There are at least 2 while (remaining_bytes > 0) logic in
  // this library to handle partial writes by File. Dedup them and use it here
  // has well.
  if (static_cast<size_t>(bytes_written) < content.size()) {
    LOG(ERROR) << "Failed to write the whole playlist. Wrote " << bytes_written
               << " but the playlist is " << content.size() << " bytes.";
    return false;
  }

  return true;
}

uint64_t MediaPlaylist::Bitrate() const {
  if (media_info_.has_bandwidth())
    return media_info_.bandwidth();
  return max_bitrate_;
}

double MediaPlaylist::GetLongestSegmentDuration() const {
  return longest_segment_duration_;
}

void MediaPlaylist::SetTargetDuration(uint32_t target_duration) {
  if (target_duration_set_) {
    if (target_duration_ == target_duration)
      return;
    VLOG(1) << "Updating target duration from " << target_duration << " to "
            << target_duration_;
  }
  target_duration_ = target_duration;
  target_duration_set_ = true;
}

// Duplicated from MpdUtils because:
// 1. MpdUtils header depends on libxml header, which is not in the deps here
// 2. GetLanguage depends on MediaInfo from packager/mpd/
// 3. Moving GetLanguage to LanguageUtils would create a a media => mpd dep.
// TODO: fix this dependency situation and factor this out to a common location
std::string MediaPlaylist::GetLanguage() const {
  std::string lang;
  if (media_info_.has_audio_info()) {
    lang = media_info_.audio_info().language();
  } else if (media_info_.has_text_info()) {
    lang = media_info_.text_info().language();
  }
  return LanguageToShortestForm(lang);
}

bool MediaPlaylist::GetResolution(uint32_t* width, uint32_t* height) const {
  DCHECK(width);
  DCHECK(height);
  if (media_info_.has_video_info()) {
    *width = media_info_.video_info().width();
    *height = media_info_.video_info().height();
    return true;
  }
  return false;
}

void MediaPlaylist::SlideWindow() {
  DCHECK(!entries_.empty());
  if (time_shift_buffer_depth_ <= 0.0 || type_ != MediaPlaylistType::kLive)
    return;
  DCHECK_GT(time_scale_, 0u);

  // The start time of the latest segment is considered the current_play_time,
  // and this should guarantee that the latest segment will stay in the list.
  const double current_play_time = LatestSegmentStartTime(entries_);
  if (current_play_time <= time_shift_buffer_depth_)
    return;

  const double timeshift_limit = current_play_time - time_shift_buffer_depth_;

  // Temporary list to hold the EXT-X-KEYs. For example, this allows us to
  // remove <3> without removing <1> and <2> below (<1> and <2> are moved to the
  // temporary list and added back later).
  //    #EXT-X-KEY   <1>
  //    #EXT-X-KEY   <2>
  //    #EXTINF      <3>
  //    #EXTINF      <4>
  std::list<std::unique_ptr<HlsEntry>> ext_x_keys;
  // Consecutive key entries are either fully removed or not removed at all.
  // Keep track of entry types so we know if it is consecutive key entries.
  HlsEntry::EntryType prev_entry_type = HlsEntry::EntryType::kExtInf;

  std::list<std::unique_ptr<HlsEntry>>::iterator last = entries_.begin();
  size_t num_segments_removed = 0;
  for (; last != entries_.end(); ++last) {
    HlsEntry::EntryType entry_type = last->get()->type();
    if (entry_type == HlsEntry::EntryType::kExtKey) {
      if (prev_entry_type != HlsEntry::EntryType::kExtKey)
        ext_x_keys.clear();
      ext_x_keys.push_back(std::move(*last));
    } else if (entry_type == HlsEntry::EntryType::kExtDiscontinuity) {
      ++discontinuity_sequence_number_;
    } else {
      DCHECK_EQ(entry_type, HlsEntry::EntryType::kExtInf);
      const SegmentInfoEntry* segment_info =
          reinterpret_cast<SegmentInfoEntry*>(last->get());
      const double last_segment_end_time =
          segment_info->start_time() + segment_info->duration();
      if (timeshift_limit < last_segment_end_time)
        break;
      ++num_segments_removed;
    }
    prev_entry_type = entry_type;
  }
  entries_.erase(entries_.begin(), last);
  // Add key entries back.
  entries_.insert(entries_.begin(), std::make_move_iterator(ext_x_keys.begin()),
                  std::make_move_iterator(ext_x_keys.end()));
  media_sequence_number_ += num_segments_removed;
}

}  // namespace hls
}  // namespace shaka
