// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packager/media/formats/webm/webm_cluster_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "packager/base/bind.h"
#include "packager/base/logging.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/media/base/decrypt_config.h"
#include "packager/media/base/timestamp.h"
#include "packager/media/formats/webm/cluster_builder.h"
#include "packager/media/formats/webm/opus_packet_builder.h"
#include "packager/media/formats/webm/webm_constants.h"

using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Mock;
using ::testing::_;

namespace edash_packager {
namespace media {

typedef WebMTracksParser::TextTracks TextTracks;
typedef std::map<uint32_t, BufferQueue> TextBufferQueueMap;

// Matchers for verifying common media log entry strings.
MATCHER_P(OpusPacketDurationTooHigh, actual_duration_ms, "") {
  return CONTAINS_STRING(
      arg, "Warning, demuxed Opus packet with encoded duration: " +
               base::IntToString(actual_duration_ms) +
               "ms. Should be no greater than 120ms.");
}

MATCHER_P(WebMSimpleBlockDurationEstimated, estimated_duration_ms, "") {
  return CONTAINS_STRING(arg, "Estimating WebM block duration to be " +
                                  base::IntToString(estimated_duration_ms) +
                                  "ms for the last (Simple)Block in the "
                                  "Cluster for this Track. Use BlockGroups "
                                  "with BlockDurations at the end of each "
                                  "Track in a Cluster to avoid estimation.");
}

MATCHER_P2(WebMBlockDurationMismatchesOpusDuration,
           block_duration_ms,
           opus_duration_ms,
           "") {
  return CONTAINS_STRING(
      arg, "BlockDuration (" + base::IntToString(block_duration_ms) +
               "ms) differs significantly from encoded duration (" +
               base::IntToString(opus_duration_ms) + "ms).");
}

namespace {

const int64_t kMicrosecondsPerMillisecond = 1000;
// Timecode scale for millisecond timestamps.
const int kTimecodeScale = 1000000;

const int kAudioTrackNum = 1;
const int kVideoTrackNum = 2;
const int kTextTrackNum = 3;
const int kTestAudioFrameDefaultDurationInMs = 13;
const int kTestVideoFrameDefaultDurationInMs = 17;

// Constants for AudioStreamInfo and VideoStreamInfo. Most are not used.
const uint32_t kTimeScale = 1000000u;
const uint64_t kDuration = 10000000u;
const char kCodecString[] = "codec_string";
const char kLanguage[] = "eng";
const uint8_t kBitsPerSample = 8u;
const uint8_t kNumChannels = 2u;
const uint32_t kSamplingFrequency = 48000u;
const size_t kExtraDataSize = 0u;
const bool kEncrypted = true;
const uint16_t kWidth = 320u;
const uint16_t kHeight = 180u;
const uint32_t kPixelWidth = 1u;
const uint32_t kPixelHeight = 1u;
const int16_t kTrickPlayRate = 0u;
const uint8_t kNaluLengthSize = 0u;

// Test duration defaults must differ from parser estimation defaults to know
// which durations parser used when emitting buffers.
static_assert(
    static_cast<int>(kTestAudioFrameDefaultDurationInMs) !=
        static_cast<int>(WebMClusterParser::kDefaultAudioBufferDurationInMs),
    "test default is the same as estimation fallback audio duration");
static_assert(
    static_cast<int>(kTestVideoFrameDefaultDurationInMs) !=
        static_cast<int>(WebMClusterParser::kDefaultVideoBufferDurationInMs),
    "test default is the same as estimation fallback video duration");

struct BlockInfo {
  int track_num;
  int timestamp;

  // Negative value is allowed only for block groups (not simple blocks) and
  // directs CreateCluster() to exclude BlockDuration entry from the cluster for
  // this BlockGroup. The absolute value is used for parser verification.
  // For simple blocks, this value must be non-negative, and is used only for
  // parser verification.
  double duration;

  bool use_simple_block;

  // Default data will be used if no data given.
  const uint8_t* data;
  int data_length;
};

const BlockInfo kDefaultBlockInfo[] = {
    {kAudioTrackNum, 0, 23, true, NULL, 0},
    {kAudioTrackNum, 23, 23, true, NULL, 0},
    // Assumes not using DefaultDuration
    {kVideoTrackNum, 33, 34, true, NULL, 0},
    {kAudioTrackNum, 46, 23, true, NULL, 0},
    {kVideoTrackNum, 67, 33, false, NULL, 0},
    {kAudioTrackNum, 69, 23, false, NULL, 0},
    {kVideoTrackNum, 100, 33, false, NULL, 0},
};

const uint8_t kEncryptedFrame[] = {
    // Block is encrypted
    0x01,
    // IV
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    // Some dummy encrypted data
    0x01,
};

const uint8_t kVP9Frame[] = {
    0xb1, 0x24, 0xc1, 0xa1, 0x40, 0x00, 0x4f, 0x80, 0x2c, 0xa0, 0x41, 0xc1,
    0x20, 0xe0, 0xc3, 0xf0, 0x00, 0x09, 0x00, 0x7c, 0x57, 0x77, 0x3f, 0x67,
    0x99, 0x3e, 0x1f, 0xfb, 0xdf, 0x0f, 0x02, 0x0a, 0x37, 0x81, 0x53, 0x80,
    0x00, 0x7e, 0x6f, 0xfe, 0x74, 0x31, 0xc6, 0x4f, 0x23, 0x9d, 0x6e, 0x5f,
    0xfc, 0xa8, 0xef, 0x67, 0xdc, 0xac, 0xf7, 0x3e, 0x31, 0x07, 0xab, 0xc7,
    0x0c, 0x74, 0x48, 0x8b, 0x95, 0x30, 0xc9, 0xf0, 0x37, 0x3b, 0xe6, 0x11,
    0xe1, 0xe6, 0xef, 0xff, 0xfd, 0xf7, 0x4f, 0x0f,
};

scoped_ptr<Cluster> CreateCluster(int timecode,
                                  const BlockInfo* block_info,
                                  int block_count) {
  ClusterBuilder cb;
  cb.SetClusterTimecode(0);

  // Default block data for audio, video and text.
  uint8_t kDefaultBlockData[] = {0x00, 0x0A, 0x01, 0x0D, 0x02};

  for (int i = 0; i < block_count; i++) {
    const uint8_t* data;
    int data_length;
    if (block_info[i].data != NULL) {
      data = block_info[i].data;
      data_length = block_info[i].data_length;
    } else {
      data = kDefaultBlockData;
      data_length = sizeof(kDefaultBlockData);
    }

    if (block_info[i].use_simple_block) {
      CHECK_GE(block_info[i].duration, 0);
      cb.AddSimpleBlock(block_info[i].track_num, block_info[i].timestamp, 0,
                        data, data_length);
      continue;
    }

    if (block_info[i].duration < 0) {
      cb.AddBlockGroupWithoutBlockDuration(block_info[i].track_num,
                                           block_info[i].timestamp, 0, data,
                                           data_length);
      continue;
    }

    cb.AddBlockGroup(block_info[i].track_num, block_info[i].timestamp,
                     block_info[i].duration, 0, data, data_length);
  }

  return cb.Finish();
}

// Creates a Cluster with one encrypted Block. |bytes_to_write| is number of
// bytes of the encrypted frame to write.
scoped_ptr<Cluster> CreateEncryptedCluster(int bytes_to_write) {
  CHECK_GT(bytes_to_write, 0);
  CHECK_LE(bytes_to_write, static_cast<int>(sizeof(kEncryptedFrame)));

  ClusterBuilder cb;
  cb.SetClusterTimecode(0);
  cb.AddSimpleBlock(kVideoTrackNum, 0, 0, kEncryptedFrame, bytes_to_write);
  return cb.Finish();
}

// Creates a Cluster with one vp9 frame (keyframe).
scoped_ptr<Cluster> CreateVP9Cluster() {
  ClusterBuilder cb;
  cb.SetClusterTimecode(0);
  cb.AddSimpleBlock(kVideoTrackNum, 0, 0, kVP9Frame, arraysize(kVP9Frame));
  return cb.Finish();
}

bool VerifyBuffersHelper(const BufferQueue& audio_buffers,
                         const BufferQueue& video_buffers,
                         const BufferQueue& text_buffers,
                         const BlockInfo* block_info,
                         int block_count) {
  int buffer_count = audio_buffers.size() + video_buffers.size() +
      text_buffers.size();
  if (block_count != buffer_count) {
    LOG(ERROR) << __FUNCTION__ << " : block_count (" << block_count
               << ") mismatches buffer_count (" << buffer_count << ")";
    return false;
  }

  size_t audio_offset = 0;
  size_t video_offset = 0;
  size_t text_offset = 0;
  for (int i = 0; i < block_count; i++) {
    const BufferQueue* buffers = NULL;
    size_t* offset;

    if (block_info[i].track_num == kAudioTrackNum) {
      buffers = &audio_buffers;
      offset = &audio_offset;
    } else if (block_info[i].track_num == kVideoTrackNum) {
      buffers = &video_buffers;
      offset = &video_offset;
    } else if (block_info[i].track_num == kTextTrackNum) {
      buffers = &text_buffers;
      offset = &text_offset;
    } else {
      LOG(ERROR) << "Unexpected track number " << block_info[i].track_num;
      return false;
    }

    if (*offset >= buffers->size()) {
      LOG(ERROR) << __FUNCTION__ << " : Too few buffers (" << buffers->size()
                 << ") for track_num (" << block_info[i].track_num
                 << "), expected at least " << *offset + 1 << " buffers";
      return false;
    }

    scoped_refptr<MediaSample> buffer = (*buffers)[(*offset)++];

    EXPECT_EQ(block_info[i].timestamp * kMicrosecondsPerMillisecond,
              buffer->pts());
    EXPECT_EQ(std::abs(block_info[i].duration) * kMicrosecondsPerMillisecond,
              buffer->duration());
  }

  return true;
}

bool VerifyTextBuffers(const BlockInfo* block_info_ptr,
                       int block_count,
                       int text_track_num,
                       const BufferQueue& text_buffers) {
  const BlockInfo* const block_info_end = block_info_ptr + block_count;

  typedef BufferQueue::const_iterator TextBufferIter;
  TextBufferIter buffer_iter = text_buffers.begin();
  const TextBufferIter buffer_end = text_buffers.end();

  while (block_info_ptr != block_info_end) {
    const BlockInfo& block_info = *block_info_ptr++;

    if (block_info.track_num != text_track_num)
      continue;

    EXPECT_FALSE(block_info.use_simple_block);
    EXPECT_FALSE(buffer_iter == buffer_end);

    const scoped_refptr<MediaSample> buffer = *buffer_iter++;
    EXPECT_EQ(block_info.timestamp * kMicrosecondsPerMillisecond,
              buffer->pts());
    EXPECT_EQ(std::abs(block_info.duration) * kMicrosecondsPerMillisecond,
              buffer->duration());
  }

  EXPECT_TRUE(buffer_iter == buffer_end);
  return true;
}

void VerifyEncryptedBuffer(scoped_refptr<MediaSample> buffer) {
  EXPECT_TRUE(buffer->is_encrypted());
}

}  // namespace

class WebMClusterParserTest : public testing::Test {
 public:
  WebMClusterParserTest()
      : audio_stream_info_(new AudioStreamInfo(kAudioTrackNum,
                                               kTimeScale,
                                               kDuration,
                                               kUnknownAudioCodec,
                                               kCodecString,
                                               kLanguage,
                                               kBitsPerSample,
                                               kNumChannels,
                                               kSamplingFrequency,
                                               NULL,
                                               kExtraDataSize,
                                               !kEncrypted)),
        video_stream_info_(new VideoStreamInfo(kVideoTrackNum,
                                               kTimeScale,
                                               kDuration,
                                               kCodecVP8,
                                               kCodecString,
                                               kLanguage,
                                               kWidth,
                                               kHeight,
                                               kPixelWidth,
                                               kPixelHeight,
                                               kTrickPlayRate,
                                               kNaluLengthSize,
                                               NULL,
                                               kExtraDataSize,
                                               !kEncrypted)),
        parser_(CreateDefaultParser()) {}

 protected:
  void ResetParserToHaveDefaultDurations() {
    int64_t default_audio_duration =
        kTestAudioFrameDefaultDurationInMs * kMicrosecondsPerMillisecond;
    int64_t default_video_duration =
        kTestVideoFrameDefaultDurationInMs * kMicrosecondsPerMillisecond;
    ASSERT_GE(default_audio_duration, 0);
    ASSERT_GE(default_video_duration, 0);
    ASSERT_NE(kNoTimestamp, default_audio_duration);
    ASSERT_NE(kNoTimestamp, default_video_duration);

    parser_.reset(CreateParserWithDefaultDurationsAndOptionalTextTracks(
        default_audio_duration, default_video_duration));
  }

  void InitEvent(const std::vector<scoped_refptr<StreamInfo>>& stream_info) {
    streams_from_init_event_ = stream_info;
  }

  bool NewSampleEvent(uint32_t track_id,
                      const scoped_refptr<MediaSample>& sample) {
    switch (track_id) {
      case kAudioTrackNum:
        audio_buffers_.push_back(sample);
        break;
      case kVideoTrackNum:
        video_buffers_.push_back(sample);
        break;
      case kTextTrackNum:
      case kTextTrackNum + 1:
        text_buffers_map_[track_id].push_back(sample);
        break;
      default:
        LOG(ERROR) << "Unexpected track number " << track_id;
        return false;
    }
    return true;
  }

  // Helper that hard-codes some non-varying constructor parameters.
  WebMClusterParser* CreateParserHelper(
      int64_t audio_default_duration,
      int64_t video_default_duration,
      const WebMTracksParser::TextTracks& text_tracks,
      const std::set<int64_t>& ignored_tracks,
      const std::string& audio_encryption_key_id,
      const std::string& video_encryption_key_id,
      const AudioCodec audio_codec,
      const VideoCodec video_codec) {
    audio_stream_info_->set_codec(audio_codec);
    video_stream_info_->set_codec(video_codec);
    return new WebMClusterParser(
        kTimecodeScale, audio_stream_info_, video_stream_info_,
        audio_default_duration, video_default_duration, text_tracks,
        ignored_tracks, audio_encryption_key_id, video_encryption_key_id,
        base::Bind(&WebMClusterParserTest::NewSampleEvent,
                   base::Unretained(this)),
        base::Bind(&WebMClusterParserTest::InitEvent, base::Unretained(this)));
  }

  // Create a default version of the parser for test.
  WebMClusterParser* CreateDefaultParser() {
    return CreateParserHelper(kNoTimestamp, kNoTimestamp, TextTracks(),
                              std::set<int64_t>(), std::string(), std::string(),
                              kUnknownAudioCodec, kCodecVP8);
  }

  // Create a parser for test with custom audio and video default durations, and
  // optionally custom text tracks.
  WebMClusterParser* CreateParserWithDefaultDurationsAndOptionalTextTracks(
      int64_t audio_default_duration,
      int64_t video_default_duration,
      const WebMTracksParser::TextTracks& text_tracks = TextTracks()) {
    return CreateParserHelper(audio_default_duration, video_default_duration,
                              text_tracks, std::set<int64_t>(), std::string(),
                              std::string(), kUnknownAudioCodec, kCodecVP8);
  }

  // Create a parser for test with custom ignored tracks.
  WebMClusterParser* CreateParserWithIgnoredTracks(
      std::set<int64_t>& ignored_tracks) {
    return CreateParserHelper(kNoTimestamp, kNoTimestamp, TextTracks(),
                              ignored_tracks, std::string(), std::string(),
                              kUnknownAudioCodec, kCodecVP8);
  }

  // Create a parser for test with custom encryption key ids and audio codec.
  WebMClusterParser* CreateParserWithKeyIdsAndAudioCodec(
      const std::string& audio_encryption_key_id,
      const std::string& video_encryption_key_id,
      const AudioCodec audio_codec) {
    return CreateParserHelper(kNoTimestamp, kNoTimestamp, TextTracks(),
                              std::set<int64_t>(), audio_encryption_key_id,
                              video_encryption_key_id, audio_codec, kCodecVP8);
  }

  // Create a parser for test with custom video codec.
  WebMClusterParser* CreateParserWithVideoCodec(const VideoCodec video_codec) {
    return CreateParserHelper(kNoTimestamp, kNoTimestamp, TextTracks(),
                              std::set<int64_t>(), std::string(), std::string(),
                              kUnknownAudioCodec, video_codec);
  }

  bool VerifyBuffers(const BlockInfo* block_info, int block_count) {
    bool result = VerifyBuffersHelper(audio_buffers_, video_buffers_,
                                      text_buffers_map_[kTextTrackNum],
                                      block_info, block_count);
    audio_buffers_.clear();
    video_buffers_.clear();
    text_buffers_map_.clear();
    return result;
  }

  scoped_refptr<AudioStreamInfo> audio_stream_info_;
  scoped_refptr<VideoStreamInfo> video_stream_info_;
  scoped_ptr<WebMClusterParser> parser_;
  std::vector<scoped_refptr<StreamInfo>> streams_from_init_event_;
  BufferQueue audio_buffers_;
  BufferQueue video_buffers_;
  TextBufferQueueMap text_buffers_map_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebMClusterParserTest);
};

TEST_F(WebMClusterParserTest, TracksWithSampleMissingDuration) {
  InSequence s;

  // Reset the parser to have 3 tracks: text, video (no default frame duration),
  // and audio (with a default frame duration).
  TextTracks text_tracks;
  text_tracks.insert(std::make_pair(TextTracks::key_type(kTextTrackNum),
                                    TextTrackConfig(kTextSubtitles, "", "",
                                                    "")));
  int64_t default_audio_duration = kTestAudioFrameDefaultDurationInMs;
  ASSERT_GE(default_audio_duration, 0);
  ASSERT_NE(kNoTimestamp, default_audio_duration);
  parser_.reset(CreateParserWithDefaultDurationsAndOptionalTextTracks(
      default_audio_duration * kMicrosecondsPerMillisecond, kNoTimestamp,
      text_tracks));

  const int kExpectedVideoEstimationInMs = 33;

  const BlockInfo kBlockInfo[] = {
      {kVideoTrackNum, 0, 33, true, NULL, 0},
      {kAudioTrackNum, 0, 23, false, NULL, 0},
      {kTextTrackNum, 10, 42, false, NULL, 0},
      {kAudioTrackNum, 23, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 33, 33, true, NULL, 0},
      {kAudioTrackNum, 36, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 66, kExpectedVideoEstimationInMs, true, NULL, 0},
      {kAudioTrackNum, 70, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kAudioTrackNum, 83, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
  };

  // Samples are not emitted in the same order as |kBlockInfo| due to missing of
  // duration in some samples.
  const BlockInfo kExpectedBlockInfo[] = {
      {kAudioTrackNum, 0, 23, false, NULL, 0},
      {kTextTrackNum, 10, 42, false, NULL, 0},
      {kAudioTrackNum, 23, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 0, 33, true, NULL, 0},
      {kAudioTrackNum, 36, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 33, 33, true, NULL, 0},
      {kAudioTrackNum, 70, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kAudioTrackNum, 83, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 66, kExpectedVideoEstimationInMs, true, NULL, 0},
  };
  const int kExpectedBuffersOnPartialCluster[] = {
    0,  // Video simple block without DefaultDuration should be held back
    1,  // Audio buffer ready
    2,  // Text buffer ready
    3,  // 2nd audio buffer ready
    4,  // 1st video emitted, 2nd video held back with no duration
    5,  // 3rd audio ready
    6,  // 2nd video emitted, 3rd video held back with no duration
    7,  // 4th audio ready
    8,  // 5th audio ready
  };

  ASSERT_EQ(arraysize(kBlockInfo), arraysize(kExpectedBuffersOnPartialCluster));
  int block_count = arraysize(kBlockInfo);

  // Iteratively create a cluster containing the first N+1 blocks and parse the
  // cluster. Verify that the corresponding entry in
  // |kExpectedBuffersOnPartialCluster| identifies the exact subset of
  // |kBlockInfo| returned by the parser.
  for (int i = 0; i < block_count; ++i) {
    parser_->Reset();

    const int blocks_in_cluster = i + 1;
    scoped_ptr<Cluster> cluster(
        CreateCluster(0, kBlockInfo, blocks_in_cluster));

    EXPECT_EQ(cluster->size(),
              parser_->Parse(cluster->data(), cluster->size()));
    EXPECT_TRUE(
        VerifyBuffers(kExpectedBlockInfo, kExpectedBuffersOnPartialCluster[i]));
  }

  // The last (3rd) video is emitted on flush with duration estimated.
  parser_->Flush();
  EXPECT_TRUE(VerifyBuffers(&kExpectedBlockInfo[block_count - 1], 1));
}

TEST_F(WebMClusterParserTest, Reset) {
  InSequence s;

  int block_count = arraysize(kDefaultBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kDefaultBlockInfo, block_count));

  // Send slightly less than the full cluster so all but the last block is
  // parsed.
  int result = parser_->Parse(cluster->data(), cluster->size() - 1);
  EXPECT_GT(result, 0);
  EXPECT_LT(result, cluster->size());

  ASSERT_TRUE(VerifyBuffers(kDefaultBlockInfo, block_count - 1));
  parser_->Reset();

  // Now parse a whole cluster to verify that all the blocks will get parsed.
  result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  ASSERT_TRUE(VerifyBuffers(kDefaultBlockInfo, block_count));
}

TEST_F(WebMClusterParserTest, ParseClusterWithSingleCall) {
  int block_count = arraysize(kDefaultBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kDefaultBlockInfo, block_count));

  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  ASSERT_TRUE(VerifyBuffers(kDefaultBlockInfo, block_count));
  // Verify init event called.
  ASSERT_EQ(2u, streams_from_init_event_.size());
  EXPECT_EQ(kStreamAudio, streams_from_init_event_[0]->stream_type());
  EXPECT_EQ(kStreamVideo, streams_from_init_event_[1]->stream_type());
}

TEST_F(WebMClusterParserTest, ParseClusterWithMultipleCalls) {
  int block_count = arraysize(kDefaultBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kDefaultBlockInfo, block_count));

  BufferQueue audio_buffers;
  BufferQueue video_buffers;
  const BufferQueue no_text_buffers;

  const uint8_t* data = cluster->data();
  int size = cluster->size();
  int default_parse_size = 3;
  int parse_size = std::min(default_parse_size, size);

  while (size > 0) {
    int result = parser_->Parse(data, parse_size);
    ASSERT_GE(result, 0);
    ASSERT_LE(result, parse_size);

    if (result == 0) {
      // The parser needs more data so increase the parse_size a little.
      parse_size += default_parse_size;
      parse_size = std::min(parse_size, size);
      continue;
    }

    parse_size = default_parse_size;

    data += result;
    size -= result;
  }
  ASSERT_TRUE(VerifyBuffers(kDefaultBlockInfo, block_count));
}

// Verify that both BlockGroups with the BlockDuration before the Block
// and BlockGroups with the BlockDuration after the Block are supported
// correctly.
// Note: Raw bytes are use here because ClusterBuilder only generates
// one of these scenarios.
TEST_F(WebMClusterParserTest, ParseBlockGroup) {
  const BlockInfo kBlockInfo[] = {
      {kAudioTrackNum, 0, 23, false, NULL, 0},
      {kVideoTrackNum, 33, 34, false, NULL, 0},
  };
  int block_count = arraysize(kBlockInfo);

  const uint8_t kClusterData[] = {
    0x1F, 0x43, 0xB6, 0x75, 0x9B,  // Cluster(size=27)
    0xE7, 0x81, 0x00,  // Timecode(size=1, value=0)
    // BlockGroup with BlockDuration before Block.
    0xA0, 0x8A,  // BlockGroup(size=10)
    0x9B, 0x81, 0x17,  // BlockDuration(size=1, value=23)
    0xA1, 0x85, 0x81, 0x00, 0x00, 0x00, 0xaa,  // Block(size=5, track=1, ts=0)
    // BlockGroup with BlockDuration after Block.
    0xA0, 0x8A,  // BlockGroup(size=10)
    0xA1, 0x85, 0x82, 0x00, 0x21, 0x00, 0x55,  // Block(size=5, track=2, ts=33)
    0x9B, 0x81, 0x22,  // BlockDuration(size=1, value=34)
  };
  const int kClusterSize = sizeof(kClusterData);

  int result = parser_->Parse(kClusterData, kClusterSize);
  EXPECT_EQ(kClusterSize, result);
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));
}

TEST_F(WebMClusterParserTest, ParseSimpleBlockAndBlockGroupMixture) {
  const BlockInfo kBlockInfo[] = {
      {kAudioTrackNum, 0, 23, true, NULL, 0},
      {kAudioTrackNum, 23, 23, false, NULL, 0},
      {kVideoTrackNum, 33, 34, true, NULL, 0},
      {kAudioTrackNum, 46, 23, false, NULL, 0},
      {kVideoTrackNum, 67, 33, false, NULL, 0},
  };
  int block_count = arraysize(kBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));

  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));
}

TEST_F(WebMClusterParserTest, IgnoredTracks) {
  std::set<int64_t> ignored_tracks;
  ignored_tracks.insert(kTextTrackNum);

  parser_.reset(CreateParserWithIgnoredTracks(ignored_tracks));

  const BlockInfo kInputBlockInfo[] = {
      {kAudioTrackNum, 0, 23, true, NULL, 0},
      {kAudioTrackNum, 23, 23, true, NULL, 0},
      {kVideoTrackNum, 33, 34, true, NULL, 0},
      {kTextTrackNum, 33, 99, true, NULL, 0},
      {kAudioTrackNum, 46, 23, true, NULL, 0},
      {kVideoTrackNum, 67, 34, true, NULL, 0},
  };
  int input_block_count = arraysize(kInputBlockInfo);

  const BlockInfo kOutputBlockInfo[] = {
      {kAudioTrackNum, 0, 23, true, NULL, 0},
      {kAudioTrackNum, 23, 23, true, NULL, 0},
      {kVideoTrackNum, 33, 34, true, NULL, 0},
      {kAudioTrackNum, 46, 23, true, NULL, 0},
      {kVideoTrackNum, 67, 34, true, NULL, 0},
  };
  int output_block_count = arraysize(kOutputBlockInfo);

  scoped_ptr<Cluster> cluster(
      CreateCluster(0, kInputBlockInfo, input_block_count));

  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kOutputBlockInfo, output_block_count));
}

TEST_F(WebMClusterParserTest, ParseTextTracks) {
  TextTracks text_tracks;

  text_tracks.insert(std::make_pair(TextTracks::key_type(kTextTrackNum),
                                    TextTrackConfig(kTextSubtitles, "", "",
                                                    "")));

  parser_.reset(CreateParserWithDefaultDurationsAndOptionalTextTracks(
      kNoTimestamp, kNoTimestamp, text_tracks));

  const BlockInfo kInputBlockInfo[] = {
      {kAudioTrackNum, 0, 23, true, NULL, 0},
      {kAudioTrackNum, 23, 23, true, NULL, 0},
      {kVideoTrackNum, 33, 34, true, NULL, 0},
      {kTextTrackNum, 33, 42, false, NULL, 0},
      {kAudioTrackNum, 46, 23, true, NULL, 0},
      {kTextTrackNum, 55, 44, false, NULL, 0},
      {kVideoTrackNum, 67, 34, true, NULL, 0},
  };
  int input_block_count = arraysize(kInputBlockInfo);

  scoped_ptr<Cluster> cluster(
      CreateCluster(0, kInputBlockInfo, input_block_count));

  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kInputBlockInfo, input_block_count));
}

TEST_F(WebMClusterParserTest, TextTracksSimpleBlock) {
  TextTracks text_tracks;

  text_tracks.insert(std::make_pair(TextTracks::key_type(kTextTrackNum),
                                    TextTrackConfig(kTextSubtitles, "", "",
                                                    "")));

  parser_.reset(CreateParserWithDefaultDurationsAndOptionalTextTracks(
      kNoTimestamp, kNoTimestamp, text_tracks));

  const BlockInfo kInputBlockInfo[] = {
    { kTextTrackNum,  33, 42, true },
  };
  int input_block_count = arraysize(kInputBlockInfo);

  scoped_ptr<Cluster> cluster(
      CreateCluster(0, kInputBlockInfo, input_block_count));

  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_LT(result, 0);
}

TEST_F(WebMClusterParserTest, ParseMultipleTextTracks) {
  TextTracks text_tracks;

  const int kSubtitleTextTrackNum = kTextTrackNum;
  const int kCaptionTextTrackNum = kTextTrackNum + 1;

  text_tracks.insert(std::make_pair(TextTracks::key_type(kSubtitleTextTrackNum),
                                    TextTrackConfig(kTextSubtitles, "", "",
                                                    "")));

  text_tracks.insert(std::make_pair(TextTracks::key_type(kCaptionTextTrackNum),
                                    TextTrackConfig(kTextCaptions, "", "",
                                                    "")));

  parser_.reset(CreateParserWithDefaultDurationsAndOptionalTextTracks(
      kNoTimestamp, kNoTimestamp, text_tracks));

  const BlockInfo kInputBlockInfo[] = {
      {kAudioTrackNum, 0, 23, true, NULL, 0},
      {kAudioTrackNum, 23, 23, true, NULL, 0},
      {kVideoTrackNum, 33, 34, true, NULL, 0},
      {kSubtitleTextTrackNum, 33, 42, false, NULL, 0},
      {kAudioTrackNum, 46, 23, true, NULL, 0},
      {kCaptionTextTrackNum, 55, 44, false, NULL, 0},
      {kVideoTrackNum, 67, 34, true, NULL, 0},
      {kSubtitleTextTrackNum, 67, 33, false, NULL, 0},
  };
  int input_block_count = arraysize(kInputBlockInfo);

  scoped_ptr<Cluster> cluster(
      CreateCluster(0, kInputBlockInfo, input_block_count));

  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);

  for (TextBufferQueueMap::const_iterator itr = text_buffers_map_.begin();
       itr != text_buffers_map_.end(); ++itr) {
    const TextTracks::const_iterator find_result =
        text_tracks.find(itr->first);
    ASSERT_TRUE(find_result != text_tracks.end());
    ASSERT_TRUE(VerifyTextBuffers(kInputBlockInfo, input_block_count,
                                  itr->first, itr->second));
  }
}

TEST_F(WebMClusterParserTest, ParseVP9) {
  scoped_ptr<Cluster> cluster(CreateVP9Cluster());
  parser_.reset(CreateParserWithVideoCodec(kCodecVP9));

  EXPECT_EQ(cluster->size(), parser_->Parse(cluster->data(), cluster->size()));

  ASSERT_EQ(2u, streams_from_init_event_.size());
  EXPECT_EQ(kStreamAudio, streams_from_init_event_[0]->stream_type());
  EXPECT_EQ(kStreamVideo, streams_from_init_event_[1]->stream_type());
  EXPECT_EQ("vp09.03.00.12.00.03.00.00",
            streams_from_init_event_[1]->codec_string());
}

TEST_F(WebMClusterParserTest, ParseEncryptedBlock) {
  scoped_ptr<Cluster> cluster(CreateEncryptedCluster(sizeof(kEncryptedFrame)));

  parser_.reset(CreateParserWithKeyIdsAndAudioCodec(
      std::string(), "video_key_id", kUnknownAudioCodec));

  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  parser_->Flush();
  ASSERT_EQ(1UL, video_buffers_.size());
  scoped_refptr<MediaSample> buffer = video_buffers_[0];
  VerifyEncryptedBuffer(buffer);
}

TEST_F(WebMClusterParserTest, ParseBadEncryptedBlock) {
  scoped_ptr<Cluster> cluster(
      CreateEncryptedCluster(sizeof(kEncryptedFrame) - 2));

  parser_.reset(CreateParserWithKeyIdsAndAudioCodec(
      std::string(), "video_key_id", kUnknownAudioCodec));
  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(-1, result);
}

TEST_F(WebMClusterParserTest, ParseInvalidZeroSizedCluster) {
  const uint8_t kBuffer[] = {
    0x1F, 0x43, 0xB6, 0x75, 0x80,  // CLUSTER (size = 0)
  };

  EXPECT_EQ(-1, parser_->Parse(kBuffer, sizeof(kBuffer)));
  // Verify init event not called.
  ASSERT_EQ(0u, streams_from_init_event_.size());
}

TEST_F(WebMClusterParserTest, ParseInvalidUnknownButActuallyZeroSizedCluster) {
  const uint8_t kBuffer[] = {
    0x1F, 0x43, 0xB6, 0x75, 0xFF,  // CLUSTER (size = "unknown")
    0x1F, 0x43, 0xB6, 0x75, 0x85,  // CLUSTER (size = 5)
  };

  EXPECT_EQ(-1, parser_->Parse(kBuffer, sizeof(kBuffer)));
}

TEST_F(WebMClusterParserTest, ParseInvalidTextBlockGroupWithoutDuration) {
  // Text track frames must have explicitly specified BlockGroup BlockDurations.
  TextTracks text_tracks;

  text_tracks.insert(std::make_pair(TextTracks::key_type(kTextTrackNum),
                                    TextTrackConfig(kTextSubtitles, "", "",
                                                    "")));

  parser_.reset(CreateParserWithDefaultDurationsAndOptionalTextTracks(
      kNoTimestamp, kNoTimestamp, text_tracks));

  const BlockInfo kBlockInfo[] = {
    { kTextTrackNum,  33, -42, false },
  };
  int block_count = arraysize(kBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));
  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_LT(result, 0);
}

TEST_F(WebMClusterParserTest, ParseWithDefaultDurationsSimpleBlocks) {
  InSequence s;
  ResetParserToHaveDefaultDurations();

  EXPECT_LT(kTestAudioFrameDefaultDurationInMs, 23);
  EXPECT_LT(kTestVideoFrameDefaultDurationInMs, 33);

  const BlockInfo kBlockInfo[] = {
      {kAudioTrackNum, 0, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kAudioTrackNum, 23, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 33, kTestVideoFrameDefaultDurationInMs, true, NULL, 0},
      {kAudioTrackNum, 46, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 67, kTestVideoFrameDefaultDurationInMs, true, NULL, 0},
      {kAudioTrackNum, 69, kTestAudioFrameDefaultDurationInMs, true, NULL, 0},
      {kVideoTrackNum, 100, kTestVideoFrameDefaultDurationInMs, true, NULL, 0},
  };

  int block_count = arraysize(kBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));

  // Send slightly less than the full cluster so all but the last block is
  // parsed. Though all the blocks are simple blocks, none should be held aside
  // for duration estimation prior to end of cluster detection because all the
  // tracks have DefaultDurations.
  int result = parser_->Parse(cluster->data(), cluster->size() - 1);
  EXPECT_GT(result, 0);
  EXPECT_LT(result, cluster->size());
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count - 1));

  parser_->Reset();

  // Now parse a whole cluster to verify that all the blocks will get parsed.
  result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));
}

TEST_F(WebMClusterParserTest, ParseWithoutAnyDurationsSimpleBlocks) {
  InSequence s;

  // Absent DefaultDuration information, SimpleBlock durations are derived from
  // inter-buffer track timestamp delta either within or across clusters.
  // Duration for the last block is estimated independently for each track when
  // Flush() is called. We use the maximum seen so far for estimation.

  const BlockInfo kBlockInfo1[] = {
      {kAudioTrackNum, 0, 23, true, NULL, 0},
      {kAudioTrackNum, 23, 22, true, NULL, 0},
      {kVideoTrackNum, 33, 33, true, NULL, 0},
      {kAudioTrackNum, 45, 23, true, NULL, 0},
      {kVideoTrackNum, 66, 34, true, NULL, 0},
      {kAudioTrackNum, 68, 24, true, NULL, 0},
      {kVideoTrackNum, 100, 35, true, NULL, 0},
  };

  int block_count1 = arraysize(kBlockInfo1);
  scoped_ptr<Cluster> cluster1(CreateCluster(0, kBlockInfo1, block_count1));

  // Verify the last fully parsed audio and video buffer are both missing from
  // the result (parser should hold them aside for duration estimation until
  // Flush() called in the absence of DefaultDurations).
  EXPECT_EQ(cluster1->size(),
            parser_->Parse(cluster1->data(), cluster1->size()));
  EXPECT_EQ(3UL, audio_buffers_.size());
  EXPECT_EQ(2UL, video_buffers_.size());
  ASSERT_TRUE(VerifyBuffers(kBlockInfo1, block_count1 - 2));

  // Verify that the estimated frame duration is tracked across clusters for
  // each track.
  const int kExpectedAudioEstimationInMs = 24;
  const int kExpectedVideoEstimationInMs = 35;
  const BlockInfo kBlockInfo2[] = {
      {kAudioTrackNum, 92, kExpectedAudioEstimationInMs, true, NULL, 0},
      {kVideoTrackNum, 135, kExpectedVideoEstimationInMs, true, NULL, 0},
  };

  int block_count2 = arraysize(kBlockInfo2);
  scoped_ptr<Cluster> cluster2(CreateCluster(0, kBlockInfo2, block_count2));
  EXPECT_EQ(cluster2->size(),
            parser_->Parse(cluster2->data(), cluster2->size()));

  // Verify that remaining blocks of cluster1 are emitted.
  ASSERT_TRUE(VerifyBuffers(&kBlockInfo1[block_count1 - 2], 2));

  // Now flush and verify blocks in cluster2 are emitted.
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kBlockInfo2, block_count2));
}

TEST_F(WebMClusterParserTest, ParseWithoutAnyDurationsBlockGroups) {
  InSequence s;

  // Absent DefaultDuration and BlockDuration information, BlockGroup block
  // durations are derived from inter-buffer track timestamp delta either within
  // or across clusters. Duration for the last block is estimated independently
  // for each track when Flush() is called. We use the maximum seen so far.

  const BlockInfo kBlockInfo1[] = {
      {kAudioTrackNum, 0, -23, false, NULL, 0},
      {kAudioTrackNum, 23, -22, false, NULL, 0},
      {kVideoTrackNum, 33, -33, false, NULL, 0},
      {kAudioTrackNum, 45, -23, false, NULL, 0},
      {kVideoTrackNum, 66, -34, false, NULL, 0},
      {kAudioTrackNum, 68, -24, false, NULL, 0},
      {kVideoTrackNum, 100, -35, false, NULL, 0},
  };

  int block_count1 = arraysize(kBlockInfo1);
  scoped_ptr<Cluster> cluster1(CreateCluster(0, kBlockInfo1, block_count1));

  // Verify the last fully parsed audio and video buffer are both missing from
  // the result (parser should hold them aside for duration estimation until
  // Flush() called in the absence of DefaultDurations).
  EXPECT_EQ(cluster1->size(),
            parser_->Parse(cluster1->data(), cluster1->size()));
  EXPECT_EQ(3UL, audio_buffers_.size());
  EXPECT_EQ(2UL, video_buffers_.size());
  ASSERT_TRUE(VerifyBuffers(kBlockInfo1, block_count1 - 2));

  // Verify that the estimated frame duration is tracked across clusters for
  // each track.
  const int kExpectedAudioEstimationInMs = 24;
  const int kExpectedVideoEstimationInMs = 35;
  const BlockInfo kBlockInfo2[] = {
      {kAudioTrackNum, 92, -kExpectedAudioEstimationInMs, false, NULL, 0},
      {kVideoTrackNum, 135, -kExpectedVideoEstimationInMs, false, NULL, 0},
  };

  int block_count2 = arraysize(kBlockInfo2);
  scoped_ptr<Cluster> cluster2(CreateCluster(0, kBlockInfo2, block_count2));
  EXPECT_EQ(cluster2->size(),
            parser_->Parse(cluster2->data(), cluster2->size()));

  // Verify that remaining blocks of cluster1 are emitted.
  ASSERT_TRUE(VerifyBuffers(&kBlockInfo1[block_count1 - 2], 2));

  // Now flush and verify blocks in cluster2 are emitted.
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kBlockInfo2, block_count2));
}

// TODO: Is parser behavior correct? See http://crbug.com/363433.
TEST_F(WebMClusterParserTest,
       ParseWithDefaultDurationsBlockGroupsWithoutDurations) {
  InSequence s;
  ResetParserToHaveDefaultDurations();

  EXPECT_LT(kTestAudioFrameDefaultDurationInMs, 23);
  EXPECT_LT(kTestVideoFrameDefaultDurationInMs, 33);

  const BlockInfo kBlockInfo[] = {
      {kAudioTrackNum, 0, -kTestAudioFrameDefaultDurationInMs, false, NULL, 0},
      {kAudioTrackNum, 23, -kTestAudioFrameDefaultDurationInMs, false, NULL, 0},
      {kVideoTrackNum, 33, -kTestVideoFrameDefaultDurationInMs, false, NULL, 0},
      {kAudioTrackNum, 46, -kTestAudioFrameDefaultDurationInMs, false, NULL, 0},
      {kVideoTrackNum, 67, -kTestVideoFrameDefaultDurationInMs, false, NULL, 0},
      {kAudioTrackNum, 69, -kTestAudioFrameDefaultDurationInMs, false, NULL, 0},
      {kVideoTrackNum,
       100,
       -kTestVideoFrameDefaultDurationInMs,
       false,
       NULL,
       0},
  };

  int block_count = arraysize(kBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));

  // Send slightly less than the full cluster so all but the last block is
  // parsed. None should be held aside for duration estimation prior to end of
  // cluster detection because all the tracks have DefaultDurations.
  int result = parser_->Parse(cluster->data(), cluster->size() - 1);
  EXPECT_GT(result, 0);
  EXPECT_LT(result, cluster->size());
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count - 1));

  // Now parse a whole cluster to verify that all the blocks will get parsed.
  result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));
}

TEST_F(WebMClusterParserTest,
       ParseDegenerateClusterYieldsHardcodedEstimatedDurations) {
  const BlockInfo kBlockInfo[] = {
    {
      kAudioTrackNum,
      0,
      WebMClusterParser::kDefaultAudioBufferDurationInMs,
      true
    }, {
      kVideoTrackNum,
      0,
      WebMClusterParser::kDefaultVideoBufferDurationInMs,
      true
    },
  };

  int block_count = arraysize(kBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));
  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));
}

TEST_F(WebMClusterParserTest,
       ParseDegenerateClusterWithDefaultDurationsYieldsDefaultDurations) {
  ResetParserToHaveDefaultDurations();

  const BlockInfo kBlockInfo[] = {
    { kAudioTrackNum, 0, kTestAudioFrameDefaultDurationInMs, true },
    { kVideoTrackNum, 0, kTestVideoFrameDefaultDurationInMs, true },
  };

  int block_count = arraysize(kBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));
  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);
  parser_->Flush();
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));
}

TEST_F(WebMClusterParserTest, ReadOpusDurationsSimpleBlockAtEndOfCluster) {
  int loop_count = 0;
  for (const auto* packet_ptr : BuildAllOpusPackets()) {
    InSequence s;

    // Get a new parser each iteration to prevent exceeding the log cap.
    parser_.reset(CreateParserWithKeyIdsAndAudioCodec(
        std::string(), std::string(), kCodecOpus));

    const BlockInfo kBlockInfo[] = {{kAudioTrackNum,
                                     0,
                                     packet_ptr->duration_ms(),
                                     true,  // Make it a SimpleBlock.
                                     packet_ptr->data(),
                                     packet_ptr->size()}};

    int block_count = arraysize(kBlockInfo);
    scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));

    int result = parser_->Parse(cluster->data(), cluster->size());
    EXPECT_EQ(cluster->size(), result);
    ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));

    loop_count++;
  }

  // Test should minimally cover all the combinations of config and frame count.
  ASSERT_GE(loop_count, kNumPossibleOpusConfigs * kMaxOpusPacketFrameCount);
}

TEST_F(WebMClusterParserTest, PreferOpusDurationsOverBlockDurations) {
  int loop_count = 0;
  for (const auto* packet_ptr : BuildAllOpusPackets()) {
    InSequence s;

    // Get a new parser each iteration to prevent exceeding the log cap.
    parser_.reset(CreateParserWithKeyIdsAndAudioCodec(
        std::string(), std::string(), kCodecOpus));

    // Setting BlockDuration != Opus duration to see which one the parser uses.
    int block_duration_ms = packet_ptr->duration_ms() + 10;
    BlockInfo block_infos[] = {{kAudioTrackNum,
                                0,
                                static_cast<double>(block_duration_ms),
                                false,  // Not a SimpleBlock.
                                packet_ptr->data(),
                                packet_ptr->size()}};

    int block_count = arraysize(block_infos);
    scoped_ptr<Cluster> cluster(CreateCluster(0, block_infos, block_count));
    int result = parser_->Parse(cluster->data(), cluster->size());
    EXPECT_EQ(cluster->size(), result);

    // BlockInfo duration will be used to verify buffer duration, so changing
    // duration to be that of the Opus packet to verify it was preferred.
    block_infos[0].duration = packet_ptr->duration_ms();

    ASSERT_TRUE(VerifyBuffers(block_infos, block_count));

    loop_count++;
  }

  // Test should minimally cover all the combinations of config and frame count.
  ASSERT_GE(loop_count, kNumPossibleOpusConfigs * kMaxOpusPacketFrameCount);
}

// Tests that BlockDuration is used to set duration on buffer rather than
// encoded duration in Opus packet (or hard coded duration estimates). Encoded
// Opus duration is usually preferred but cannot be known when encrypted.
TEST_F(WebMClusterParserTest, DontReadEncodedDurationWhenEncrypted) {
  // Non-empty dummy value signals encryption is active for audio.
  std::string audio_encryption_id("audio_key_id");

  // Reset parser to expect Opus codec audio and use audio encryption key id.
  parser_.reset(CreateParserWithKeyIdsAndAudioCodec(audio_encryption_id,
                                                    std::string(), kCodecOpus));

  // Single Block with BlockDuration and encrypted data.
  const BlockInfo kBlockInfo[] = {{kAudioTrackNum,
                                   0,
                                   kTestAudioFrameDefaultDurationInMs,
                                   false,            // Not a SimpleBlock
                                   kEncryptedFrame,  // Encrypted frame data
                                   arraysize(kEncryptedFrame)}};

  int block_count = arraysize(kBlockInfo);
  scoped_ptr<Cluster> cluster(CreateCluster(0, kBlockInfo, block_count));
  int result = parser_->Parse(cluster->data(), cluster->size());
  EXPECT_EQ(cluster->size(), result);

  // Will verify that duration of buffer matches that of BlockDuration.
  ASSERT_TRUE(VerifyBuffers(kBlockInfo, block_count));
}

}  // namespace media
}  // namespace edash_packager
