// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MPD_BASE_SIMPLE_MPD_NOTIFIER_H_
#define MPD_BASE_SIMPLE_MPD_NOTIFIER_H_

#include <map>
#include <string>
#include <vector>

#include "packager/base/memory/scoped_ptr.h"
#include "packager/base/synchronization/lock.h"
#include "packager/mpd/base/mpd_notifier.h"

namespace edash_packager {

class AdaptationSet;
class MpdBuilder;
class Representation;

struct MpdOptions;

/// A simple MpdNotifier implementation which receives muxer listener event and
/// generates an Mpd file.
class SimpleMpdNotifier : public MpdNotifier {
 public:
  SimpleMpdNotifier(DashProfile dash_profile,
                    const MpdOptions& mpd_options,
                    const std::vector<std::string>& base_urls,
                    const std::string& output_path);
  virtual ~SimpleMpdNotifier();

  /// @name MpdNotifier implemetation overrides.
  /// @{
  virtual bool Init() OVERRIDE;
  virtual bool NotifyNewContainer(const MediaInfo& media_info,
                                  uint32_t* id) OVERRIDE;
  virtual bool NotifySampleDuration(uint32_t container_id,
                                    uint32_t sample_duration) OVERRIDE;
  virtual bool NotifyNewSegment(uint32_t id,
                                uint64_t start_time,
                                uint64_t duration,
                                uint64_t size) OVERRIDE;
  virtual bool AddContentProtectionElement(
      uint32_t id,
      const ContentProtectionElement& content_protection_element) OVERRIDE;
  /// @}

 private:
  enum ContentType {
    kUnknown,
    kVideo,
    kAudio,
    kText
  };
  ContentType GetContentType(const MediaInfo& media_info);
  bool WriteMpdToFile();

  std::string output_path_;

  scoped_ptr<MpdBuilder> mpd_builder_;

  base::Lock lock_;

  // [type][lang] = AdaptationSet
  typedef std::map<ContentType, std::map<std::string, AdaptationSet*> >
      AdaptationSetMap;
  AdaptationSetMap adaptation_set_map_;

  typedef std::map<uint32_t, Representation*> RepresentationMap;
  RepresentationMap representation_map_;

  DISALLOW_COPY_AND_ASSIGN(SimpleMpdNotifier);
};

}  // namespace edash_packager

#endif  // MPD_BASE_SIMPLE_MPD_NOTIFIER_H_
