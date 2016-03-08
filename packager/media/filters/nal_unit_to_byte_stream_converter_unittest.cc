// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gtest/gtest.h>

#include "packager/media/base/media_sample.h"
#include "packager/media/filters/nal_unit_to_byte_stream_converter.h"

namespace edash_packager {
namespace media {

namespace {

// This should be valud AVCDecoderConfigurationRecord that can be parsed by
// NalUnitToByteStreamConverter.
const uint8_t kTestAVCDecoderConfigurationRecord[] = {
    0x01,        // configuration version (must be 1)
    0x00,        // AVCProfileIndication (bogus)
    0x00,        // profile_compatibility (bogus)
    0x00,        // AVCLevelIndication (bogus)
    0xFF,        // Length size minus 1 == 3
    0xE1,        // 1 sps.
    0x00, 0x1D,  // SPS length == 29
    // Some valid SPS data.
    0x67, 0x64, 0x00, 0x1E, 0xAC, 0xD9, 0x40, 0xB4,
    0x2F, 0xF9, 0x7F, 0xF0, 0x00, 0x80, 0x00, 0x91,
    0x00, 0x00, 0x03, 0x03, 0xE9, 0x00, 0x00, 0xEA,
    0x60, 0x0F, 0x16, 0x2D, 0x96,
    0x01,        // 1 pps.
    0x00, 0x0A,  // PPS length == 10
    // The content of PPS is not checked except the type.
    0x68, 0xFE, 0xFD, 0xFC, 0xFB, 0x11, 0x12, 0x13, 0x14, 0x15,
};

const bool kEscapeData = true;
const bool kIsKeyFrame = true;

}  // namespace

class NalUnitToByteStreamConverterTest : public ::testing::Test {
 public:
  NalUnitToByteStreamConverter converter_;
};

// Expect a valid AVCDecoderConfigurationRecord to pass.
TEST(NalUnitToByteStreamConverterTest, ParseAVCDecoderConfigurationRecord) {
  NalUnitToByteStreamConverter converter;
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           kEscapeData));
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           !kEscapeData));
}

// Empty AVCDecoderConfigurationRecord should return false.
TEST(NalUnitToByteStreamConverterTest, EmptyAVCDecoderConfigurationRecord) {
  NalUnitToByteStreamConverter converter;
  EXPECT_FALSE(converter.Initialize(nullptr, 102, kEscapeData));
  EXPECT_FALSE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord, 0, kEscapeData));
}

// If there is no SPS, Initialize() should fail.
TEST(NalUnitToByteStreamConverterTest, NoSps) {
  NalUnitToByteStreamConverter converter;
  const uint8_t kNoSps[] = {
      0x01,        // configuration version (must be 1)
      0x00,        // AVCProfileIndication (bogus)
      0x00,        // profile_compatibility (bogus)
      0x00,        // AVCLevelIndication (bogus)
      0xFF,        // Length size minus 1 == 3
      0xE0,        // 0 sps.
      // The rest doesn't really matter, Initialize() should fail.
      0x01,        // 1 pps.
      0x00, 0x0A,  // PPS length == 10
      0x68, 0xFE, 0xFD, 0xFC, 0xFB, 0x11, 0x12, 0x13, 0x14, 0x15,
  };

  EXPECT_FALSE(converter.Initialize(kNoSps, arraysize(kNoSps), !kEscapeData));
}

// If there is no PPS, Initialize() should fail.
TEST(NalUnitToByteStreamConverterTest, NoPps) {
  NalUnitToByteStreamConverter converter;
  const uint8_t kNoPps[] = {
      0x01,        // configuration version (must be 1)
      0x00,        // AVCProfileIndication (bogus)
      0x00,        // profile_compatibility (bogus)
      0x00,        // AVCLevelIndication (bogus)
      0xFF,        // Length size minus 1 == 3
      0xE1,        // 1 sps.
      0x00, 0x1D,  // SPS length == 29
      // Some valid SPS data.
      0x67, 0x64, 0x00, 0x1E, 0xAC, 0xD9, 0x40, 0xB4,
      0x2F, 0xF9, 0x7F, 0xF0, 0x00, 0x80, 0x00, 0x91,
      0x00, 0x00, 0x03, 0x03, 0xE9, 0x00, 0x00, 0xEA,
      0x60, 0x0F, 0x16, 0x2D, 0x96,
      0x00,  // 0 pps.
  };

  EXPECT_FALSE(converter.Initialize(kNoPps, arraysize(kNoPps), !kEscapeData));
}

// If the length of SPS is 0 then Initialize() should fail.
TEST(NalUnitToByteStreamConverterTest, ZeroLengthSps) {
  NalUnitToByteStreamConverter converter;
  const uint8_t kZeroLengthSps[] = {
      0x01,        // configuration version (must be 1)
      0x00,        // AVCProfileIndication (bogus)
      0x00,        // profile_compatibility (bogus)
      0x00,        // AVCLevelIndication (bogus)
      0xFF,        // Length size minus 1 == 3
      0xE1,        // 1 sps.
      0x00, 0x00,  // SPS length == 0
      0x01,        // 1 pps.
      0x00, 0x0A,  // PPS length == 10
      0x68, 0xFE, 0xFD, 0xFC, 0xFB, 0x11, 0x12, 0x13, 0x14, 0x15,
  };

  EXPECT_FALSE(converter.Initialize(kZeroLengthSps, arraysize(kZeroLengthSps),
                                    !kEscapeData));
}

// If the length of PPS is 0 then Initialize() should fail.
TEST(NalUnitToByteStreamConverterTest, ZeroLengthPps) {
  NalUnitToByteStreamConverter converter;
  const uint8_t kZeroLengthPps[] = {
    0x01,        // configuration version (must be 1)
    0x00,        // AVCProfileIndication (bogus)
    0x00,        // profile_compatibility (bogus)
    0x00,        // AVCLevelIndication (bogus)
    0xFF,        // Length size minus 1 == 3
    0xE1,        // 1 sps.
    0x00, 0x05,  // SPS length == 5
    0x00, 0x00, 0x00, 0x01, 0x02,
    0x01,        // 1 pps.
    0x00, 0x00,  // PPS length == 0
  };

  EXPECT_FALSE(converter.Initialize(kZeroLengthPps, arraysize(kZeroLengthPps),
                                    !kEscapeData));
}

TEST(NalUnitToByteStreamConverterTest, ConvertUnitToByteStream) {
  // Only the type of the NAL units are checked.
  // This does not contain AUD, SPS, nor PPS.
  const uint8_t kUnitStreamLikeMediaSample[] = {
      0x00, 0x00, 0x00, 0x0A,  // Size 10 NALU.
      0x00,                    // Unspecified NAL unit type.
      0xFD, 0x78, 0xA4, 0xC3, 0x82, 0x62, 0x11, 0x29, 0x77,
  };
  NalUnitToByteStreamConverter converter;
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           !kEscapeData));

  std::vector<uint8_t> output;
  EXPECT_TRUE(converter.ConvertUnitToByteStream(
      kUnitStreamLikeMediaSample, arraysize(kUnitStreamLikeMediaSample),
      kIsKeyFrame, &output));

  const uint8_t kExpectedOutput[] = {
      0x00, 0x00, 0x00, 0x01,              // Start code.
      0x09,                                // AUD type.
      0xF0,                                // primary pic type is anything.
      0x00, 0x00, 0x00, 0x01,              // Start code.
      // Some valid SPS data.
      0x67, 0x64, 0x00, 0x1E, 0xAC, 0xD9, 0x40, 0xB4,
      0x2F, 0xF9, 0x7F, 0xF0, 0x00, 0x80, 0x00, 0x91,
      0x00, 0x00, 0x03, 0x03, 0xE9, 0x00, 0x00, 0xEA,
      0x60, 0x0F, 0x16, 0x2D, 0x96,
      0x00, 0x00, 0x00, 0x01,              // Start code.
      0x68, 0xFE, 0xFD, 0xFC, 0xFB, 0x11, 0x12, 0x13, 0x14, 0x15,  // PPS.
      0x00, 0x00, 0x00, 0x01,  // Start code.
      // The input NALU.
      0x00,  // Unspecified NALU type.
      0xFD, 0x78, 0xA4, 0xC3, 0x82, 0x62, 0x11, 0x29, 0x77,
  };

  EXPECT_EQ(std::vector<uint8_t>(kExpectedOutput,
                                 kExpectedOutput + arraysize(kExpectedOutput)),
            output);
}

// Verify that escaping works on all data.
TEST(NalUnitToByteStreamConverterTest, ConvertUnitToByteStreamWithEscape) {
  // Only the type of the NAL units are checked.
  // This does not contain AUD, SPS, nor PPS.
  const uint8_t kUnitStreamLikeMediaSample[] = {
      0x00, 0x00, 0x00, 0x0A,  // Size 10 NALU.
      0x00,                    // Unspecified NAL unit type.
      0x06, 0x00, 0x00, 0x00, 0xDF, 0x62, 0x11, 0x29, 0x77,
  };
  NalUnitToByteStreamConverter converter;
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           kEscapeData));

  std::vector<uint8_t> output;
  EXPECT_TRUE(converter.ConvertUnitToByteStream(
      kUnitStreamLikeMediaSample, arraysize(kUnitStreamLikeMediaSample),
      kIsKeyFrame, &output));

  const uint8_t kExpectedOutput[] = {
      0x00, 0x00, 0x00, 0x01,              // Start code.
      0x09,                                // AUD type.
      0xF0,                                // primary pic type is anything.
      0x00, 0x00, 0x00, 0x01,              // Start code.
      // Some valid SPS data.
      0x67, 0x64, 0x00, 0x1E, 0xAC, 0xD9, 0x40, 0xB4,
      0x2F, 0xF9, 0x7F, 0xF0, 0x00, 0x80, 0x00, 0x91,
      // Note that extra 0x03 is added.
      0x00, 0x00, 0x03, 0x03, 0x03, 0xE9, 0x00, 0x00,
      0xEA, 0x60, 0x0F, 0x16, 0x2D, 0x96,
      0x00, 0x00, 0x00, 0x01,              // Start code.
      0x68, 0xFE, 0xFD, 0xFC, 0xFB, 0x11, 0x12, 0x13, 0x14, 0x15,  // PPS.
      0x00, 0x00, 0x00, 0x01,  // Start code.
      // The input NALU.
      0x00,  // Unspecified NALU type.
      0x06, 0x00, 0x00, 0x03, 0x00, 0xDF, 0x62, 0x11, 0x29, 0x77,
  };

  EXPECT_EQ(std::vector<uint8_t>(kExpectedOutput,
                                 kExpectedOutput + arraysize(kExpectedOutput)),
            output);
}

// NALU ending with 0 must have 3 appended.
TEST(NalUnitToByteStreamConverterTest, NaluEndingWithZero) {
  const uint8_t kNaluEndingWithZero[] = {
      0x00, 0x00, 0x00, 0x03,  // Size 10 NALU.
      0x00,                    // Unspecified NAL unit type.
      0xAA, 0x00,              // Ends with 0.
  };
  NalUnitToByteStreamConverter converter;
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           kEscapeData));

  std::vector<uint8_t> output;
  EXPECT_TRUE(converter.ConvertUnitToByteStream(kNaluEndingWithZero,
                                                arraysize(kNaluEndingWithZero),
                                                !kIsKeyFrame, &output));

  const uint8_t kExpectedOutput[] = {
      0x00, 0x00, 0x00, 0x01,              // Start code.
      0x09,                                // AUD type.
      0xF0,                                // primary pic type is anything.
      0x00, 0x00, 0x00, 0x01,              // Start code.
      // The input NALU.
      0x00,              // Unspecified NALU type.
      0xAA, 0x00, 0x03,  // 0x03 at the end because the original ends with 0.
  };

  EXPECT_EQ(std::vector<uint8_t>(kExpectedOutput,
                                 kExpectedOutput + arraysize(kExpectedOutput)),
            output);
}

// Verify that if it is not a key frame then SPS and PPS from decoder
// configuration is not used.
TEST(NalUnitToByteStreamConverterTest, NonKeyFrameSample) {
  const uint8_t kNonKeyFrameStream[] = {
      0x00, 0x00, 0x00, 0x03,  // Size 10 NALU.
      0x00,                    // Unspecified NAL unit type.
      0x33, 0x88,
  };
  NalUnitToByteStreamConverter converter;
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           kEscapeData));

  std::vector<uint8_t> output;
  EXPECT_TRUE(converter.ConvertUnitToByteStream(kNonKeyFrameStream,
                                                arraysize(kNonKeyFrameStream),
                                                !kIsKeyFrame, &output));

  const uint8_t kExpectedOutput[] = {
      0x00, 0x00, 0x00, 0x01,  // Start code.
      0x09,                    // AUD type.
      0xF0,                    // Anything.
      0x00, 0x00, 0x00, 0x01,  // Start code.
      // The input NALU.
      0x00,  // Unspecified NALU type.
      0x33, 0x88,
  };

  EXPECT_EQ(std::vector<uint8_t>(kExpectedOutput,
                                 kExpectedOutput + arraysize(kExpectedOutput)),
            output);
}

// Bug found during unit testing.
// The zeros aren't contiguous but the escape byte was inserted.
TEST(NalUnitToByteStreamConverterTest, DispersedZeros) {
  const uint8_t kDispersedZeros[] = {
      0x00, 0x00, 0x00, 0x08,  // Size 10 NALU.
      0x00,                    // Unspecified NAL unit type.
      // After 2 zeros (including the first byte of the NALU followed by 0, 1,
      // 2, or 3 caused it to insert the escape byte.
      0x11, 0x00,
      0x01, 0x00, 0x02, 0x00, 0x44,
  };
  NalUnitToByteStreamConverter converter;
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           kEscapeData));

  std::vector<uint8_t> output;
  EXPECT_TRUE(converter.ConvertUnitToByteStream(
      kDispersedZeros, arraysize(kDispersedZeros), !kIsKeyFrame, &output));

  const uint8_t kExpectedOutput[] = {
      0x00, 0x00, 0x00, 0x01,  // Start code.
      0x09,                    // AUD type.
      0xF0,                    // Anything.
      0x00, 0x00, 0x00, 0x01,  // Start code.
      // The input NALU.
      0x00,  // Unspecified NAL unit type.
      0x11, 0x00, 0x01, 0x00, 0x02, 0x00, 0x44,
  };

  EXPECT_EQ(std::vector<uint8_t>(kExpectedOutput,
                                 kExpectedOutput + arraysize(kExpectedOutput)),
            output);
}

// Verify that CnovertUnitToByteStream() with escape_data = false works.
TEST(NalUnitToByteStreamConverterTest, DoNotEscape) {
  // This has sequences that should be escaped if escape_data = true.
  const uint8_t kNotEscaped[] = {
      0x00, 0x00, 0x00, 0x0C,  // Size 12 NALU.
      0x00,                    // Unspecified NAL unit type.
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03,
  };

  NalUnitToByteStreamConverter converter;
  EXPECT_TRUE(
      converter.Initialize(kTestAVCDecoderConfigurationRecord,
                           arraysize(kTestAVCDecoderConfigurationRecord),
                           !kEscapeData));

  std::vector<uint8_t> output;
  EXPECT_TRUE(converter.ConvertUnitToByteStream(
      kNotEscaped, arraysize(kNotEscaped), !kIsKeyFrame, &output));

  const uint8_t kExpectedOutput[] = {
      0x00, 0x00, 0x00, 0x01,  // Start code.
      0x09,                    // AUD type.
      0xF0,                    // Anything.
      0x00, 0x00, 0x00, 0x01,  // Start code.
      // Should be the same as the input.
      0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03,
  };

  EXPECT_EQ(std::vector<uint8_t>(kExpectedOutput,
                                 kExpectedOutput + arraysize(kExpectedOutput)),
            output);
}

}  // namespace media
}  // namespace edash_packager
