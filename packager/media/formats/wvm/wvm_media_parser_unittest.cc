// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "packager/base/bind.h"
#include "packager/base/bind_helpers.h"
#include "packager/base/logging.h"
#include "packager/base/memory/ref_counted.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/request_signer.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/base/timestamp.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/base/key_source.h"
#include "packager/media/formats/wvm/wvm_media_parser.h"
#include "packager/media/test/test_data_util.h"

namespace {
const int64_t kNoTimestamp = std::numeric_limits<int64_t>::min();
const char kWvmFile[] = "hb2_4stream_encrypted.wvm";
// Constants associated with kWvmFile follows.
const uint32_t kExpectedStreams = 4;
const int kExpectedVideoFrameCount = 6665;
const int kExpectedAudioFrameCount = 11964;
const uint8_t kExpectedAssetKey[] =
    "\x06\x81\x7f\x48\x6b\xf2\x7f\x3e\xc7\x39\xa8\x3f\x12\x0a\xd2\xfc";
}  // namespace

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace edash_packager {
namespace media {

class MockKeySource : public KeySource {
 public:
  MockKeySource() {}
  virtual ~MockKeySource() {}

  MOCK_METHOD1(FetchKeys, Status(uint32_t asset_id));
  MOCK_METHOD2(GetKey, Status(TrackType track_type,
                              EncryptionKey* key));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockKeySource);
};

namespace wvm {

class WvmMediaParserTest : public testing::Test {
 public:
  WvmMediaParserTest()
      : audio_frame_count_(0),
        video_frame_count_(0),
        video_max_dts_(kNoTimestamp),
        current_track_id_(-1) {
    parser_.reset(new WvmMediaParser());
    key_source_.reset(new MockKeySource());
    encryption_key_.key.assign(kExpectedAssetKey, kExpectedAssetKey + 16);
  }

 protected:
  typedef std::map<int, scoped_refptr<StreamInfo> > StreamMap;

  scoped_ptr<WvmMediaParser> parser_;
  scoped_ptr<MockKeySource> key_source_;
  StreamMap stream_map_;
  int audio_frame_count_;
  int video_frame_count_;
  int64_t video_max_dts_;
  uint32_t current_track_id_;
  EncryptionKey encryption_key_;

  void OnInit(const std::vector<scoped_refptr<StreamInfo> >& stream_infos) {
    DVLOG(1) << "OnInit: " << stream_infos.size() << " streams.";
    for (std::vector<scoped_refptr<StreamInfo> >::const_iterator iter =
             stream_infos.begin(); iter != stream_infos.end(); ++iter) {
      DVLOG(1) << (*iter)->ToString();
      stream_map_[(*iter)->track_id()] = *iter;
    }
  }

  bool OnNewSample(uint32_t track_id,
                   const scoped_refptr<MediaSample>& sample) {
    std::string stream_type;
    if (track_id != current_track_id_) {
      // onto next track.
      video_max_dts_ = kNoTimestamp;
      current_track_id_ = track_id;
    }
    StreamMap::const_iterator stream = stream_map_.find(track_id);
    if (stream != stream_map_.end()) {
      if (stream->second->stream_type() == kStreamAudio) {
        ++audio_frame_count_;
        stream_type = "audio";
      } else if (stream->second->stream_type() == kStreamVideo) {
        ++video_frame_count_;
        stream_type = "video";
        // Verify timestamps are increasing.
        if (video_max_dts_ == kNoTimestamp) {
          video_max_dts_ = sample->dts();
        } else if (video_max_dts_ >= sample->dts()) {
          LOG(ERROR) << "Video DTS not strictly increasing for track = "
                     << track_id << ", video max dts = "
                     << video_max_dts_ << ", sample dts = "
                     << sample->dts();
          return false;
        }
        video_max_dts_ = sample->dts();
      } else {
        LOG(ERROR) << "Missing StreamInfo for track ID " << track_id;
        return false;
      }
    }

    return true;
  }

  void InitializeParser() {
    parser_->Init(
        base::Bind(&WvmMediaParserTest::OnInit,
                   base::Unretained(this)),
        base::Bind(&WvmMediaParserTest::OnNewSample,
                   base::Unretained(this)),
        key_source_.get());
  }

  void Parse(const std::string& filename) {
    InitializeParser();

    std::vector<uint8_t> buffer = ReadTestDataFile(filename);
    EXPECT_TRUE(parser_->Parse(buffer.data(), buffer.size()));
  }
};

TEST_F(WvmMediaParserTest, ParseWvm) {
  EXPECT_CALL(*key_source_, FetchKeys(_)).WillOnce(Return(Status::OK));
  EXPECT_CALL(*key_source_, GetKey(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(encryption_key_), Return(Status::OK)));
  Parse(kWvmFile);
  EXPECT_EQ(kExpectedStreams, stream_map_.size());
  EXPECT_EQ(kExpectedVideoFrameCount, video_frame_count_);
  EXPECT_EQ(kExpectedAudioFrameCount, audio_frame_count_);
}

}  // namespace wvm
}  // namespace media
}  // namespace edash_packager
