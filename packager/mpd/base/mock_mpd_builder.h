// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MPD_BASE_MOCK_MPD_BUILDER_H_
#define MPD_BASE_MOCK_MPD_BUILDER_H_

#include <gmock/gmock.h>

#include "packager/base/compiler_specific.h"
#include "packager/mpd/base/content_protection_element.h"
#include "packager/mpd/base/mpd_builder.h"

namespace edash_packager {

class MockMpdBuilder : public MpdBuilder {
 public:
  // |type| indicates whether the MPD should be for VOD or live content (kStatic
  // for VOD profile, or kDynamic for live profile).
  explicit MockMpdBuilder(MpdType type);
  virtual ~MockMpdBuilder() OVERRIDE;

  MOCK_METHOD1(AddAdaptationSet, AdaptationSet*(const std::string& lang));
  MOCK_METHOD1(ToString, bool(std::string* output));
};

class MockAdaptationSet : public AdaptationSet {
 public:
  // |adaptation_set_id| is the id for the AdaptationSet.
  explicit MockAdaptationSet(uint32_t adaptation_set_id);
  virtual ~MockAdaptationSet() OVERRIDE;

  MOCK_METHOD1(AddRepresentation, Representation*(const MediaInfo& media_info));
  MOCK_METHOD1(AddContentProtectionElement,
               void(const ContentProtectionElement& element));
  MOCK_METHOD2(UpdateContentProtectionPssh,
               void(const std::string& drm_uuid, const std::string& pssh));
  MOCK_METHOD1(AddRole, void(AdaptationSet::Role role));

  MOCK_METHOD1(SetGroup, void(int group_number));
  MOCK_CONST_METHOD0(Group, int());

 private:
  // Only for constructing the super class. Not used for testing.
  base::AtomicSequenceNumber sequence_counter_;
};

class MockRepresentation : public Representation {
 public:
  // |representation_id| is the numeric ID for the <Representation>.
  explicit MockRepresentation(uint32_t representation_id);
  virtual ~MockRepresentation() OVERRIDE;

  MOCK_METHOD1(AddContentProtectionElement,
               void(const ContentProtectionElement& element));
  MOCK_METHOD2(UpdateContentProtectionPssh,
               void(const std::string& drm_uuid, const std::string& pssh));
  MOCK_METHOD3(AddNewSegment,
               void(uint64_t start_time, uint64_t duration, uint64_t size));
  MOCK_METHOD1(SetSampleDuration, void(uint32_t sample_duration));
};

}  // namespace edash_packager

#endif  // MPD_BASE_MOCK_MPD_BUILDER_H_
