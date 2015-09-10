// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MPD_BASE_DASH_IOP_MPD_NOTIFIER_H_
#define MPD_BASE_DASH_IOP_MPD_NOTIFIER_H_

#include "packager/mpd/base/mpd_notifier.h"

#include <list>
#include <map>
#include <string>
#include <vector>

#include "packager/mpd/base/mpd_builder.h"
#include "packager/mpd/base/mpd_notifier_util.h"
#include "packager/mpd/base/mpd_options.h"

namespace edash_packager {

/// This class is an MpdNotifier which will try its best to generate a
/// DASH IF IOPv3 compliant MPD.
/// e.g.
/// All <ContentProtection> elements must be right under
/// <AdaptationSet> and cannot be under <Representation>.
/// All video Adaptation Sets have Role set to "main".
class DashIopMpdNotifier : public MpdNotifier {
 public:
  DashIopMpdNotifier(DashProfile dash_profile,
                     const MpdOptions& mpd_options,
                     const std::vector<std::string>& base_urls,
                     const std::string& output_path);
  virtual ~DashIopMpdNotifier() OVERRIDE;

  /// None of the methods write out the MPD file until Flush() is called.
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
  virtual bool Flush() OVERRIDE;
  /// @}

 private:
  friend class DashIopMpdNotifierTest;

  // Maps representation ID to Representation.
  typedef std::map<uint32_t, Representation*> RepresentationMap;

  // Maps AdaptationSet ID to ProtectedContent.
  typedef std::map<uint32_t, MediaInfo::ProtectedContent> ProtectedContentMap;

  // Checks the protected_content field of media_info and returns a non-null
  // AdaptationSet* for a new Representation.
  // This does not necessarily return a new AdaptationSet. If
  // media_info.protected_content completely matches with an existing
  // AdaptationSet, then it will return the pointer.
  AdaptationSet* GetAdaptationSetForMediaInfo(const MediaInfo& media_info,
                                              ContentType type,
                                              const std::string& language);

  // Sets a group id for |adaptation_set| if applicable.
  // If a group ID is already assigned, then this returns immediately.
  // |type| and |language| are the type and language of |adaptation_set|.
  void SetGroupId(ContentType type,
                  const std::string& language,
                  AdaptationSet* adaptation_set);

  // Helper function to get a new AdaptationSet; registers the values
  // to the fields (maps) of the instance.
  // If the media is encrypted, registers data to protected_content_map_.
  AdaptationSet* NewAdaptationSet(const MediaInfo& media_info,
                                  const std::string& language,
                                  std::list<AdaptationSet*>* adaptation_sets);

  // Testing only method. Returns a pointer to MpdBuilder.
  MpdBuilder* MpdBuilderForTesting() const {
    return mpd_builder_.get();
  }

  // Testing only method. Sets mpd_builder_.
  void SetMpdBuilderForTesting(scoped_ptr<MpdBuilder> mpd_builder) {
    mpd_builder_ = mpd_builder.Pass();
  }

  // [type][lang] = list<AdaptationSet>
  // Note: lang can be empty, e.g. for video.
  std::map<ContentType, std::map<std::string, std::list<AdaptationSet*> > >
      adaptation_set_list_map_;
  RepresentationMap representation_map_;

  // Used to check whether a Representation should be added to an AdaptationSet.
  ProtectedContentMap protected_content_map_;

  // MPD output path.
  std::string output_path_;
  scoped_ptr<MpdBuilder> mpd_builder_;
  base::Lock lock_;

  // Next group ID to use for AdapationSets that can be grouped.
  int next_group_id_;
};

}  // namespace edash_packager

#endif  // MPD_BASE_DASH_IOP_MPD_NOTIFIER_H_
