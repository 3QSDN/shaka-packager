// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/hls/base/hls_notifier.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/event/hls_notify_muxer_listener.h"
#include "packager/media/event/muxer_listener_test_helper.h"

namespace shaka {
namespace media {

using ::testing::Return;
using ::testing::StrEq;
using ::testing::_;

namespace {

class MockHlsNotifier : public hls::HlsNotifier {
 public:
  MockHlsNotifier()
      : HlsNotifier(hls::HlsNotifier::HlsProfile::kOnDemandProfile) {}

  MOCK_METHOD0(Init, bool());
  MOCK_METHOD5(NotifyNewStream,
               bool(const MediaInfo& media_info,
                    const std::string& playlist_name,
                    const std::string& name,
                    const std::string& group_id,
                    uint32_t* stream_id));
  MOCK_METHOD5(NotifyNewSegment,
               bool(uint32_t stream_id,
                    const std::string& segment_name,
                    uint64_t start_time,
                    uint64_t duration,
                    uint64_t size));
  MOCK_METHOD5(
      NotifyEncryptionUpdate,
      bool(uint32_t stream_id,
           const std::vector<uint8_t>& key_id,
           const std::vector<uint8_t>& system_id,
           const std::vector<uint8_t>& iv,
           const std::vector<uint8_t>& protection_system_specific_data));
  MOCK_METHOD0(Flush, bool());
};

// Doesn't really matter what the values are as long as it is a system ID (16
// bytes).
const uint8_t kAnySystemId[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

const uint8_t kAnyData[] = {
  0xFF, 0x78, 0xAA, 0x6B,
};

// This value doesn't really affect the test, it's not used by the
// implementation.
const bool kInitialEncryptionInfo = true;

const char kDefaultPlaylistName[] = "default_playlist.m3u8";

}  // namespace

class HlsNotifyMuxerListenerTest : public ::testing::Test {
 protected:
  HlsNotifyMuxerListenerTest()
      : listener_(kDefaultPlaylistName, &mock_notifier_) {}

  MockHlsNotifier mock_notifier_;
  HlsNotifyMuxerListener listener_;
};

TEST_F(HlsNotifyMuxerListenerTest, OnEncryptionInfoReady) {
  ProtectionSystemSpecificInfo info;
  std::vector<uint8_t> system_id(kAnySystemId,
                                 kAnySystemId + arraysize(kAnySystemId));
  info.set_system_id(system_id.data(), system_id.size());
  std::vector<uint8_t> pssh_data(kAnyData, kAnyData + arraysize(kAnyData));
  info.set_pssh_data(pssh_data);

  std::vector<uint8_t> key_id(16, 0x05);
  std::vector<ProtectionSystemSpecificInfo> key_system_infos;
  key_system_infos.push_back(info);

  std::vector<uint8_t> iv(16, 0x54);

  EXPECT_CALL(mock_notifier_,
              NotifyEncryptionUpdate(_, key_id, system_id, iv, pssh_data))
      .WillOnce(Return(true));
  listener_.OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbcs, key_id,
                                  iv, key_system_infos);
}

TEST_F(HlsNotifyMuxerListenerTest, OnMediaStart) {
  MuxerOptions muxer_options;
  muxer_options.hls_name = "Name";
  muxer_options.hls_group_id = "GroupID";
  SetDefaultMuxerOptionsValues(&muxer_options);
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  scoped_refptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  EXPECT_CALL(mock_notifier_,
              NotifyNewStream(_, StrEq(kDefaultPlaylistName), StrEq("Name"),
                              StrEq("GroupID"), _))
      .WillOnce(Return(true));

  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
}

TEST_F(HlsNotifyMuxerListenerTest, OnSampleDurationReady) {
  listener_.OnSampleDurationReady(2340);
}

TEST_F(HlsNotifyMuxerListenerTest, OnMediaEnd) {
  EXPECT_CALL(mock_notifier_, Flush()).WillOnce(Return(true));
  // None of these values matter, they are not used.
  listener_.OnMediaEnd(false, 0, 0, false, 0, 0, 0, 0);
}

TEST_F(HlsNotifyMuxerListenerTest, OnNewSegment) {
  const uint64_t kStartTime = 19283;
  const uint64_t kDuration = 98028;
  const uint64_t kFileSize = 756739;
  EXPECT_CALL(mock_notifier_,
              NotifyNewSegment(_, StrEq("new_segment_name10.ts"), kStartTime,
                               kDuration, kFileSize));
  listener_.OnNewSegment("new_segment_name10.ts", kStartTime, kDuration,
                         kFileSize);
}

}  // namespace media
}  // namespace shaka
