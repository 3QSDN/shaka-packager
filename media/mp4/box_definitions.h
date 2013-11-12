// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MP4_BOX_DEFINITIONS_H_
#define MEDIA_MP4_BOX_DEFINITIONS_H_

#include <string>
#include <vector>

#include "media/mp4/aac.h"
#include "media/mp4/box_reader.h"
#include "media/mp4/fourccs.h"

namespace media {
namespace mp4 {

enum TrackType {
  kInvalid = 0,
  kVideo,
  kAudio,
  kHint
};

#define DECLARE_BOX_METHODS(T) \
  T(); \
  virtual ~T(); \
  virtual bool Parse(BoxReader* reader) OVERRIDE; \
  virtual FourCC BoxType() const OVERRIDE; \

struct FileType : Box {
  DECLARE_BOX_METHODS(FileType);

  FourCC major_brand;
  uint32 minor_version;
};

struct ProtectionSystemSpecificHeader : Box {
  DECLARE_BOX_METHODS(ProtectionSystemSpecificHeader);

  std::vector<uint8> system_id;
  std::vector<uint8> raw_box;
};

struct SampleAuxiliaryInformationOffset : Box {
  DECLARE_BOX_METHODS(SampleAuxiliaryInformationOffset);

  std::vector<uint64> offsets;
};

struct SampleAuxiliaryInformationSize : Box {
  DECLARE_BOX_METHODS(SampleAuxiliaryInformationSize);

  uint8 default_sample_info_size;
  uint32 sample_count;
  std::vector<uint8> sample_info_sizes;
};

struct OriginalFormat : Box {
  DECLARE_BOX_METHODS(OriginalFormat);

  FourCC format;
};

struct SchemeType : Box {
  DECLARE_BOX_METHODS(SchemeType);

  FourCC type;
  uint32 version;
};

struct TrackEncryption : Box {
  DECLARE_BOX_METHODS(TrackEncryption);

  // Note: this definition is specific to the CENC protection type.
  bool is_encrypted;
  uint8 default_iv_size;
  std::vector<uint8> default_kid;
};

struct SchemeInfo : Box {
  DECLARE_BOX_METHODS(SchemeInfo);

  TrackEncryption track_encryption;
};

struct ProtectionSchemeInfo : Box {
  DECLARE_BOX_METHODS(ProtectionSchemeInfo);

  OriginalFormat format;
  SchemeType type;
  SchemeInfo info;
};

struct MovieHeader : Box {
  DECLARE_BOX_METHODS(MovieHeader);

  uint64 creation_time;
  uint64 modification_time;
  uint32 timescale;
  uint64 duration;
  int32 rate;
  int16 volume;
  uint32 next_track_id;
};

struct TrackHeader : Box {
  DECLARE_BOX_METHODS(TrackHeader);

  uint64 creation_time;
  uint64 modification_time;
  uint32 track_id;
  uint64 duration;
  int16 layer;
  int16 alternate_group;
  int16 volume;
  uint32 width;
  uint32 height;
};

struct EditListEntry {
  uint64 segment_duration;
  int64 media_time;
  int16 media_rate_integer;
  int16 media_rate_fraction;
};

struct EditList : Box {
  DECLARE_BOX_METHODS(EditList);

  std::vector<EditListEntry> edits;
};

struct Edit : Box {
  DECLARE_BOX_METHODS(Edit);

  EditList list;
};

struct HandlerReference : Box {
  DECLARE_BOX_METHODS(HandlerReference);

  TrackType type;
};

struct AVCDecoderConfigurationRecord : Box {
  DECLARE_BOX_METHODS(AVCDecoderConfigurationRecord);
  bool ParseData(BufferReader* reader);

  // Contains full avc decoder configuration record as defined in iso14496-15
  // 5.2.4.1, including possible extension bytes described in paragraph 3.
  // Known fields defined in the spec are also parsed and included in this
  // structure.
  std::vector<uint8> data;

  uint8 version;
  uint8 profile_indication;
  uint8 profile_compatibility;
  uint8 avc_level;
  uint8 length_size;

  typedef std::vector<uint8> SPS;
  typedef std::vector<uint8> PPS;

  std::vector<SPS> sps_list;
  std::vector<PPS> pps_list;
};

struct PixelAspectRatioBox : Box {
  DECLARE_BOX_METHODS(PixelAspectRatioBox);

  uint32 h_spacing;
  uint32 v_spacing;
};

struct VideoSampleEntry : Box {
  DECLARE_BOX_METHODS(VideoSampleEntry);

  FourCC format;
  uint16 data_reference_index;
  uint16 width;
  uint16 height;

  PixelAspectRatioBox pixel_aspect;
  ProtectionSchemeInfo sinf;

  // Currently expected to be present regardless of format.
  AVCDecoderConfigurationRecord avcc;
};

struct ElementaryStreamDescriptor : Box {
  DECLARE_BOX_METHODS(ElementaryStreamDescriptor);

  uint8 object_type;
  AAC aac;
};

struct AudioSampleEntry : Box {
  DECLARE_BOX_METHODS(AudioSampleEntry);

  FourCC format;
  uint16 data_reference_index;
  uint16 channelcount;
  uint16 samplesize;
  uint32 samplerate;

  ProtectionSchemeInfo sinf;
  ElementaryStreamDescriptor esds;
};

struct SampleDescription : Box {
  DECLARE_BOX_METHODS(SampleDescription);

  TrackType type;
  std::vector<VideoSampleEntry> video_entries;
  std::vector<AudioSampleEntry> audio_entries;
};

struct DecodingTime {
  uint32 sample_count;
  uint32 sample_delta;
};

// stts.
struct DecodingTimeToSample : Box {
  DECLARE_BOX_METHODS(DecodingTimeToSample);

  std::vector<DecodingTime> decoding_time;
};

struct CompositionOffset {
  uint32 sample_count;
  // If version == 0, sample_offset is uint32;
  // If version == 1, sample_offset is int32.
  // Let us always use signed version, which should work unless the offset
  // exceeds 31 bits, which shouldn't happen.
  int32 sample_offset;
};

// ctts. Optional.
struct CompositionTimeToSample : Box {
  DECLARE_BOX_METHODS(CompositionTimeToSample);

  std::vector<CompositionOffset> composition_offset;
};

struct ChunkInfo {
  uint32 first_chunk;
  uint32 samples_per_chunk;
  uint32 sample_description_index;
};

// stsc.
struct SampleToChunk : Box {
  DECLARE_BOX_METHODS(SampleToChunk);

  std::vector<ChunkInfo> chunk_info;
};

// stsz.
struct SampleSize : Box {
  DECLARE_BOX_METHODS(SampleSize);

  uint32 sample_size;
  uint32 sample_count;
  std::vector<uint32> sizes;
};

// stz2.
struct CompactSampleSize : SampleSize {
  DECLARE_BOX_METHODS(CompactSampleSize);
};

// stco.
struct ChunkOffset : Box {
  DECLARE_BOX_METHODS(ChunkOffset);

  // Chunk byte offsets into mdat relative to the beginning of the file.
  // Use 64 bits instead of 32 bits so it is large enough to hold
  // ChunkLargeOffset data.
  std::vector<uint64> offsets;
};

// co64.
struct ChunkLargeOffset : ChunkOffset {
  DECLARE_BOX_METHODS(ChunkLargeOffset);
};

// stss. Optional.
struct SyncSample : Box {
  DECLARE_BOX_METHODS(SyncSample);

  std::vector<uint32> sample_number;
};

struct SampleTable : Box {
  DECLARE_BOX_METHODS(SampleTable);

  SampleDescription description;
  DecodingTimeToSample decoding_time_to_sample;
  CompositionTimeToSample composition_time_to_sample;
  SampleToChunk sample_to_chunk;
  // Either SampleSize or CompactSampleSize must present. Store in SampleSize.
  SampleSize sample_size;
  // Either ChunkOffset or ChunkLargeOffset must present. Store in ChunkOffset.
  ChunkOffset chunk_offset;
  SyncSample sync_sample;
};

struct MediaHeader : Box {
  DECLARE_BOX_METHODS(MediaHeader);

  uint64 creation_time;
  uint64 modification_time;
  uint32 timescale;
  uint64 duration;
  // 3-char language code + 1 null terminating char.
  char language[4];
};

struct MediaInformation : Box {
  DECLARE_BOX_METHODS(MediaInformation);

  SampleTable sample_table;
};

struct Media : Box {
  DECLARE_BOX_METHODS(Media);

  MediaHeader header;
  HandlerReference handler;
  MediaInformation information;
};

struct Track : Box {
  DECLARE_BOX_METHODS(Track);

  TrackHeader header;
  Media media;
  Edit edit;
};

struct MovieExtendsHeader : Box {
  DECLARE_BOX_METHODS(MovieExtendsHeader);

  uint64 fragment_duration;
};

struct TrackExtends : Box {
  DECLARE_BOX_METHODS(TrackExtends);

  uint32 track_id;
  uint32 default_sample_description_index;
  uint32 default_sample_duration;
  uint32 default_sample_size;
  uint32 default_sample_flags;
};

struct MovieExtends : Box {
  DECLARE_BOX_METHODS(MovieExtends);

  MovieExtendsHeader header;
  std::vector<TrackExtends> tracks;
};

struct Movie : Box {
  DECLARE_BOX_METHODS(Movie);

  bool fragmented;
  MovieHeader header;
  MovieExtends extends;
  std::vector<Track> tracks;
  std::vector<ProtectionSystemSpecificHeader> pssh;
};

struct TrackFragmentDecodeTime : Box {
  DECLARE_BOX_METHODS(TrackFragmentDecodeTime);

  uint64 decode_time;
};

struct MovieFragmentHeader : Box {
  DECLARE_BOX_METHODS(MovieFragmentHeader);

  uint32 sequence_number;
};

struct TrackFragmentHeader : Box {
  DECLARE_BOX_METHODS(TrackFragmentHeader);

  uint32 track_id;

  uint32 sample_description_index;
  uint32 default_sample_duration;
  uint32 default_sample_size;
  uint32 default_sample_flags;

  // As 'flags' might be all zero, we cannot use zeroness alone to identify
  // when default_sample_flags wasn't specified, unlike the other values.
  bool has_default_sample_flags;
};

struct TrackFragmentRun : Box {
  DECLARE_BOX_METHODS(TrackFragmentRun);

  uint32 sample_count;
  uint32 data_offset;
  std::vector<uint32> sample_flags;
  std::vector<uint32> sample_sizes;
  std::vector<uint32> sample_durations;
  std::vector<int32> sample_composition_time_offsets;
};

struct TrackFragment : Box {
  DECLARE_BOX_METHODS(TrackFragment);

  TrackFragmentHeader header;
  std::vector<TrackFragmentRun> runs;
  TrackFragmentDecodeTime decode_time;
  SampleAuxiliaryInformationOffset auxiliary_offset;
  SampleAuxiliaryInformationSize auxiliary_size;
};

struct MovieFragment : Box {
  DECLARE_BOX_METHODS(MovieFragment);

  MovieFragmentHeader header;
  std::vector<TrackFragment> tracks;
  std::vector<ProtectionSystemSpecificHeader> pssh;
};

#undef DECLARE_BOX

}  // namespace mp4
}  // namespace media

#endif  // MEDIA_MP4_BOX_DEFINITIONS_H_
