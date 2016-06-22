// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/codecs/vp_codec_configuration_record.h"

#include <gtest/gtest.h>

namespace shaka {
namespace media {

TEST(VPCodecConfigurationRecordTest, Parse) {
  const uint8_t kVpCodecConfigurationData[] = {
      0x01, 0x00, 0xA2, 0x14, 0x00, 0x01, 0x00,
  };

  VPCodecConfigurationRecord vp_config;
  ASSERT_TRUE(vp_config.ParseMP4(std::vector<uint8_t>(
      kVpCodecConfigurationData,
      kVpCodecConfigurationData + arraysize(kVpCodecConfigurationData))));

  EXPECT_EQ(1u, vp_config.profile());
  EXPECT_EQ(0u, vp_config.level());
  EXPECT_EQ(10u, vp_config.bit_depth());
  EXPECT_EQ(2u, vp_config.color_space());
  EXPECT_EQ(1u, vp_config.chroma_subsampling());
  EXPECT_EQ(2u, vp_config.transfer_function());
  EXPECT_FALSE(vp_config.video_full_range_flag());

  EXPECT_EQ("vp09.01.00.10.02.01.02.00", vp_config.GetCodecString(kCodecVP9));
}

TEST(VPCodecConfigurationRecordTest, ParseWithInsufficientData) {
  const uint8_t kVpCodecConfigurationData[] = {
      0x01, 0x00, 0xA2, 0x14,
  };

  VPCodecConfigurationRecord vp_config;
  ASSERT_FALSE(vp_config.ParseMP4(std::vector<uint8_t>(
      kVpCodecConfigurationData,
      kVpCodecConfigurationData + arraysize(kVpCodecConfigurationData))));
}

TEST(VPCodecConfigurationRecordTest, WriteMP4) {
  const uint8_t kExpectedVpCodecConfigurationData[] = {
      0x02, 0x01, 0x80, 0x21, 0x00, 0x00,
  };
  VPCodecConfigurationRecord vp_config(0x02, 0x01, 0x08, 0x00, 0x02, 0x00, true,
                                       std::vector<uint8_t>());
  std::vector<uint8_t> data;
  vp_config.WriteMP4(&data);

  EXPECT_EQ(
      std::vector<uint8_t>(kExpectedVpCodecConfigurationData,
                           kExpectedVpCodecConfigurationData +
                               arraysize(kExpectedVpCodecConfigurationData)),
      data);
}

TEST(VPCodecConfigurationRecordTest, WriteWebM) {
  const uint8_t kExpectedVpCodecConfigurationData[] = {
      0x01, 0x01, 0x02,
      0x02, 0x01, 0x01,
      0x03, 0x01, 0x08,
      0x04, 0x01, 0x03
  };
  VPCodecConfigurationRecord vp_config(0x02, 0x01, 0x08, 0x00, 0x03, 0x00, true,
                                 std::vector<uint8_t>());
  std::vector<uint8_t> data;
  vp_config.WriteWebM(&data);

  EXPECT_EQ(
      std::vector<uint8_t>(kExpectedVpCodecConfigurationData,
                           kExpectedVpCodecConfigurationData +
                               arraysize(kExpectedVpCodecConfigurationData)),
      data);
}

}  // namespace media
}  // namespace shaka
