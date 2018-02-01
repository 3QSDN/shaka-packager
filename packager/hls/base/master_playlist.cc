// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/hls/base/master_playlist.h"

#include <algorithm>  // std::max

#include <inttypes.h>

#include "packager/base/files/file_path.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/file/file.h"
#include "packager/hls/base/media_playlist.h"
#include "packager/version/version.h"

namespace shaka {
namespace hls {

namespace {
struct Variant {
  std::string audio_codec;
  const std::string* audio_group_id = nullptr;
  uint64_t audio_bitrate = 0;
};

uint64_t MaxBitrate(const std::list<const MediaPlaylist*> playlists) {
  uint64_t max = 0;
  for (const auto& playlist : playlists) {
    max = std::max(max, playlist->Bitrate());
  }
  return max;
}

std::string GetAudioGroupCodecString(
    const std::list<const MediaPlaylist*>& group) {
  // TODO(vaage): Should be a concatenation of all the codecs in the group.
  return group.front()->codec();
}

std::list<Variant> AudioGroupsToVariants(
    const std::map<std::string, std::list<const MediaPlaylist*>>& groups) {
  std::list<Variant> variants;

  for (const auto& group : groups) {
    Variant variant;
    variant.audio_codec = GetAudioGroupCodecString(group.second);
    variant.audio_group_id = &group.first;
    variant.audio_bitrate = MaxBitrate(group.second);

    variants.push_back(variant);
  }

  // Make sure we return at least one variant so create a null variant if there
  // are no variants.
  if (variants.empty()) {
    variants.emplace_back();
  }

  return variants;
}

class Tag {
 public:
  Tag(const std::string& name, std::string* buffer) : buffer_(buffer) {
    base::StringAppendF(buffer_, "%s:", name.c_str());
  }

  void AddString(const std::string& key, const std::string& value) {
    NextField();
    base::StringAppendF(buffer_, "%s=%s", key.c_str(), value.c_str());
  }

  void AddQuotedString(const std::string& key, const std::string& value) {
    NextField();
    base::StringAppendF(buffer_, "%s=\"%s\"", key.c_str(), value.c_str());
  }

  void AddNumber(const std::string& key, uint64_t value) {
    NextField();
    base::StringAppendF(buffer_, "%s=%" PRIu64, key.c_str(), value);
  }

  void AddResolution(const std::string& key, uint32_t width, uint32_t height) {
    NextField();
    base::StringAppendF(buffer_, "%s=%" PRIu32 "x%" PRIu32, key.c_str(), width,
                        height);
  }

 private:
  Tag(const Tag&) = delete;
  Tag& operator=(const Tag&) = delete;

  std::string* buffer_;
  size_t fields = 0;

  void NextField() {
    if (fields++) {
      buffer_->append(",");
    }
  }
};

void BuildAudioTag(const std::string& base_url,
                   const std::string& group_id,
                   const MediaPlaylist& audio_playlist,
                   bool is_default,
                   bool is_autoselect,
                   std::string* out) {
  DCHECK(out);

  Tag tag("#EXT-X-MEDIA", out);
  tag.AddString("TYPE", "AUDIO");
  tag.AddQuotedString("URI", base_url + audio_playlist.file_name());
  tag.AddQuotedString("GROUP-ID", group_id);
  const std::string& language = audio_playlist.GetLanguage();
  if (!language.empty()) {
    tag.AddQuotedString("LANGUAGE", language);
  }
  tag.AddQuotedString("NAME", audio_playlist.name());
  if (is_default) {
    tag.AddString("DEFAULT", "YES");
  }
  if (is_autoselect) {
    tag.AddString("AUTOSELECT", "YES");
  }
  tag.AddQuotedString("CHANNELS",
                      std::to_string(audio_playlist.GetNumChannels()));

  out->append("\n");
}

void BuildVideoTag(const MediaPlaylist& playlist,
                   uint64_t max_audio_bitrate,
                   const std::string& audio_codec,
                   const std::string* audio_group_id,
                   const std::string& base_url,
                   std::string* out) {
  DCHECK(out);

  const uint64_t bitrate = playlist.Bitrate() + max_audio_bitrate;

  uint32_t width;
  uint32_t height;
  CHECK(playlist.GetDisplayResolution(&width, &height));

  std::string codecs = playlist.codec();
  if (!audio_codec.empty()) {
    base::StringAppendF(&codecs, ",%s", audio_codec.c_str());
  }

  Tag tag("#EXT-X-STREAM-INF", out);

  tag.AddNumber("BANDWIDTH", bitrate);
  tag.AddQuotedString("CODECS", codecs);
  tag.AddResolution("RESOLUTION", width, height);

  if (audio_group_id) {
    tag.AddQuotedString("AUDIO", *audio_group_id);
  }

  base::StringAppendF(out, "\n%s%s\n", base_url.c_str(),
                      playlist.file_name().c_str());
}
}  // namespace

MasterPlaylist::MasterPlaylist(const std::string& file_name,
                               const std::string& default_language)
    : file_name_(file_name), default_language_(default_language) {}
MasterPlaylist::~MasterPlaylist() {}

void MasterPlaylist::AddMediaPlaylist(MediaPlaylist* media_playlist) {
  DCHECK(media_playlist);
  switch (media_playlist->stream_type()) {
    case MediaPlaylist::MediaPlaylistStreamType::kAudio: {
      const std::string& group_id = media_playlist->group_id();
      audio_playlist_groups_[group_id].push_back(media_playlist);
      break;
    }
    case MediaPlaylist::MediaPlaylistStreamType::kVideo: {
      video_playlists_.push_back(media_playlist);
      break;
    }
    default: {
      NOTIMPLEMENTED() << static_cast<int>(media_playlist->stream_type())
                       << " not handled.";
      break;
    }
  }
  // Sometimes we need to iterate over all playlists, so keep a collection
  // of all playlists to make iterating easier.
  all_playlists_.push_back(media_playlist);
}

bool MasterPlaylist::WriteMasterPlaylist(const std::string& base_url,
                                         const std::string& output_dir) {
  // TODO(rkuroiwa): Handle audio only.
  std::string audio_output;
  std::string video_output;

  // Write out all the audio tags.
  for (const auto& group : audio_playlist_groups_) {
    const auto& group_id = group.first;
    const auto& playlists = group.second;

    // Tracks the language of the playlist in this group.
    // According to HLS spec: https://goo.gl/MiqjNd 4.3.4.1.1. Rendition Groups
    // - A Group MUST NOT have more than one member with a DEFAULT attribute of
    //   YES.
    // - Each EXT-X-MEDIA tag with an AUTOSELECT=YES attribute SHOULD have a
    //   combination of LANGUAGE[RFC5646], ASSOC-LANGUAGE, FORCED, and
    //   CHARACTERISTICS attributes that is distinct from those of other
    //   AUTOSELECT=YES members of its Group.
    // We tag the first rendition encountered with a particular language with
    // 'AUTOSELECT'; it is tagged with 'DEFAULT' too if the language matches
    // |default_language_|.
    std::set<std::string> languages;

    for (const auto& playlist : playlists) {
      bool is_default = false;
      bool is_autoselect = false;

      const std::string language = playlist->GetLanguage();
      if (languages.find(language) == languages.end()) {
        is_default = !language.empty() && language == default_language_;
        is_autoselect = true;
        languages.insert(language);
      }

      BuildAudioTag(base_url, group_id, *playlist, is_default, is_autoselect,
                    &audio_output);
    }
  }

  std::list<Variant> variants = AudioGroupsToVariants(audio_playlist_groups_);

  // Write all the video tags out.
  for (const auto& playlist : video_playlists_) {
    for (const auto& variant : variants) {
      BuildVideoTag(*playlist, variant.audio_bitrate, variant.audio_codec,
                    variant.audio_group_id, base_url, &video_output);
    }
  }

  const std::string version = GetPackagerVersion();
  std::string version_line;
  if (!version.empty()) {
    version_line =
        base::StringPrintf("## Generated with %s version %s\n",
                           GetPackagerProjectUrl().c_str(), version.c_str());
  }

  std::string content = "";
  base::StringAppendF(&content, "#EXTM3U\n%s%s%s", version_line.c_str(),
                      audio_output.c_str(), video_output.c_str());

  // Skip if the playlist is already written.
  if (content == written_playlist_)
    return true;

  std::string file_path =
      base::FilePath::FromUTF8Unsafe(output_dir)
          .Append(base::FilePath::FromUTF8Unsafe(file_name_))
          .AsUTF8Unsafe();
  if (!File::WriteFileAtomically(file_path.c_str(), content)) {
    LOG(ERROR) << "Failed to write master playlist to: " << file_path;
    return false;
  }
  written_playlist_ = content;
  return true;
}

}  // namespace hls
}  // namespace shaka
