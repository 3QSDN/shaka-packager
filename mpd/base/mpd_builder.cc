// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "mpd/base/mpd_builder.h"

#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include <cmath>
#include <list>
#include <string>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "media/file/file.h"
#include "mpd/base/content_protection_element.h"
#include "mpd/base/mpd_utils.h"
#include "mpd/base/xml/xml_node.h"

namespace edash_packager {

using xml::XmlNode;
using xml::RepresentationXmlNode;
using xml::AdaptationSetXmlNode;

namespace {

std::string GetMimeType(
    const std::string& prefix,
    MediaInfo::ContainerType container_type) {
  switch (container_type) {
    case MediaInfo::CONTAINER_MP4:
      return prefix + "/mp4";
    case MediaInfo::CONTAINER_MPEG2_TS:
      // NOTE: DASH MPD spec uses lowercase but RFC3555 says uppercase.
      return prefix + "/MP2T";
    case MediaInfo::CONTAINER_WEBM:
      return prefix + "/webm";
    default:
      break;
  }

  // Unsupported container types should be rejected/handled by the caller.
  NOTREACHED() << "Unrecognized container type: " << container_type;
  return std::string();
}

void AddMpdNameSpaceInfo(XmlNode* mpd) {
  DCHECK(mpd);

  static const char kXmlNamespace[] = "urn:mpeg:DASH:schema:MPD:2011";
  mpd->SetStringAttribute("xmlns", kXmlNamespace);
  static const char kXmlNamespaceXsi[] = "http://www.w3.org/2001/XMLSchema-instance";
  mpd->SetStringAttribute("xmlns:xsi", kXmlNamespaceXsi);
  static const char kXmlNamespaceXlink[] = "http://www.w3.org/1999/xlink";
  mpd->SetStringAttribute("xmlns:xlink", kXmlNamespaceXlink);
  static const char kDashSchemaMpd2011[] =
      "urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd";
  mpd->SetStringAttribute("xsi:schemaLocation", kDashSchemaMpd2011);
}

bool IsPeriodNode(xmlNodePtr node) {
  DCHECK(node);
  int kEqual = 0;
  return xmlStrcmp(node->name, reinterpret_cast<const xmlChar*>("Period")) ==
         kEqual;
}

// Find the first <Period> element. This does not recurse down the tree,
// only checks direct children. Returns the pointer to Period element on
// success, otherwise returns false.
// As noted here, we must traverse.
// http://www.xmlsoft.org/tutorial/ar01s04.html
xmlNodePtr FindPeriodNode(XmlNode* xml_node) {
  for (xmlNodePtr node = xml_node->GetRawPtr()->xmlChildrenNode;
       node != NULL;
       node = node->next) {
    if (IsPeriodNode(node))
      return node;
  }

  return NULL;
}

bool Positive(double d) {
  return d > 0.0;
}

// Return current time in XML DateTime format.
std::string XmlDateTimeNowWithOffset(int32 offset_seconds) {
  base::Time time = base::Time::Now();
  time += base::TimeDelta::FromSeconds(offset_seconds);
  base::Time::Exploded time_exploded;
  time.UTCExplode(&time_exploded);

  return base::StringPrintf("%4d-%02d-%02dT%02d:%02d:%02d",
                            time_exploded.year,
                            time_exploded.month,
                            time_exploded.day_of_month,
                            time_exploded.hour,
                            time_exploded.minute,
                            time_exploded.second);
}

void SetIfPositive(const char* attr_name, double value, XmlNode* mpd) {
  if (Positive(value)) {
    mpd->SetStringAttribute(attr_name, SecondsToXmlDuration(value));
  }
}

uint32 GetTimeScale(const MediaInfo& media_info) {
  if (media_info.has_reference_time_scale()) {
    return media_info.reference_time_scale();
  }

  if (media_info.video_info_size() > 0) {
    return media_info.video_info(0).time_scale();
  }

  if (media_info.audio_info_size() > 0) {
    return media_info.audio_info(0).time_scale();
  }

  LOG(WARNING) << "No timescale specified, using 1 as timescale.";
  return 1;
}

uint64 LastSegmentStartTime(const SegmentInfo& segment_info) {
  return segment_info.start_time + segment_info.duration * segment_info.repeat;
}

// This is equal to |segment_info| end time
uint64 LastSegmentEndTime(const SegmentInfo& segment_info) {
  return segment_info.start_time +
         segment_info.duration * (segment_info.repeat + 1);
}

uint64 LatestSegmentStartTime(const std::list<SegmentInfo>& segments) {
  DCHECK(!segments.empty());
  const SegmentInfo& latest_segment = segments.back();
  return LastSegmentStartTime(latest_segment);
}

// Given |timeshift_limit|, finds out the number of segments that are no longer
// valid and should be removed from |segment_info|.
int SearchTimedOutRepeatIndex(uint64 timeshift_limit,
                              const SegmentInfo& segment_info) {
  DCHECK_LE(timeshift_limit, LastSegmentEndTime(segment_info));
  if (timeshift_limit < segment_info.start_time)
    return 0;

  return (timeshift_limit - segment_info.start_time) / segment_info.duration;
}

// Overload this function to support different types of |output|.
// Note that this could be done by call MpdBuilder::ToString() and use the
// result to write to a file, it requires an extra copy.
bool WriteXmlCharArrayToOutput(xmlChar* doc,
                               int doc_size,
                               std::string* output) {
  DCHECK(doc);
  DCHECK(output);
  output->assign(doc, doc + doc_size);
  return true;
}

bool WriteXmlCharArrayToOutput(xmlChar* doc,
                               int doc_size,
                               media::File* output) {
  DCHECK(doc);
  DCHECK(output);
  if (output->Write(doc, doc_size) < doc_size)
    return false;

  return output->Flush();
}

}  // namespace

MpdOptions::MpdOptions()
    : availability_time_offset(0),
      minimum_update_period(0),
      // TODO(tinskip): Set min_buffer_time in unit tests rather than here.
      min_buffer_time(2.0),
      time_shift_buffer_depth(0),
      suggested_presentation_delay(0) {}

MpdOptions::~MpdOptions() {}

MpdBuilder::MpdBuilder(MpdType type, const MpdOptions& mpd_options)
    : type_(type),
      mpd_options_(mpd_options),
      adaptation_sets_deleter_(&adaptation_sets_) {}

MpdBuilder::~MpdBuilder() {}

void MpdBuilder::AddBaseUrl(const std::string& base_url) {
  base::AutoLock scoped_lock(lock_);
  base_urls_.push_back(base_url);
}

AdaptationSet* MpdBuilder::AddAdaptationSet() {
  base::AutoLock scoped_lock(lock_);
  scoped_ptr<AdaptationSet> adaptation_set(new AdaptationSet(
      adaptation_set_counter_.GetNext(), mpd_options_, &representation_counter_));

  DCHECK(adaptation_set);
  adaptation_sets_.push_back(adaptation_set.get());
  return adaptation_set.release();
}

bool MpdBuilder::WriteMpdToFile(media::File* output_file) {
  base::AutoLock scoped_lock(lock_);
  DCHECK(output_file);
  return WriteMpdToOutput(output_file);
}

bool MpdBuilder::ToString(std::string* output) {
  base::AutoLock scoped_lock(lock_);
  DCHECK(output);
  return WriteMpdToOutput(output);
}

template <typename OutputType>
bool MpdBuilder::WriteMpdToOutput(OutputType* output) {
  xmlInitParser();
  xml::ScopedXmlPtr<xmlDoc>::type doc(GenerateMpd());
  if (!doc.get())
    return false;

  static const int kNiceFormat = 1;
  int doc_str_size = 0;
  xmlChar* doc_str = NULL;
  xmlDocDumpFormatMemoryEnc(
      doc.get(), &doc_str, &doc_str_size, "UTF-8", kNiceFormat);

  bool result = WriteXmlCharArrayToOutput(doc_str, doc_str_size, output);
  xmlFree(doc_str);

  // Cleanup, free the doc then cleanup parser.
  doc.reset();
  xmlCleanupParser();
  return result;
}

xmlDocPtr MpdBuilder::GenerateMpd() {
  // Setup nodes.
  static const char kXmlVersion[] = "1.0";
  xml::ScopedXmlPtr<xmlDoc>::type doc(xmlNewDoc(BAD_CAST kXmlVersion));
  XmlNode mpd("MPD");

  // Iterate thru AdaptationSets and add them to one big Period element.
  XmlNode period("Period");
  std::list<AdaptationSet*>::iterator adaptation_sets_it =
      adaptation_sets_.begin();
  for (; adaptation_sets_it != adaptation_sets_.end(); ++adaptation_sets_it) {
    xml::ScopedXmlPtr<xmlNode>::type child((*adaptation_sets_it)->GetXml());
    if (!child.get() || !period.AddChild(child.Pass()))
      return NULL;
  }

  // Add baseurls to MPD.
  std::list<std::string>::const_iterator base_urls_it = base_urls_.begin();
  for (; base_urls_it != base_urls_.end(); ++base_urls_it) {
    XmlNode base_url("BaseURL");
    base_url.SetContent(*base_urls_it);

    if (!mpd.AddChild(base_url.PassScopedPtr()))
      return NULL;
  }

  if (type_ == kDynamic) {
    // This is the only Period and it is a regular period.
    period.SetStringAttribute("start", "PT0S");
  }

  if (!mpd.AddChild(period.PassScopedPtr()))
    return NULL;

  AddMpdNameSpaceInfo(&mpd);
  AddCommonMpdInfo(&mpd);
  switch (type_) {
    case kStatic:
      AddStaticMpdInfo(&mpd);
      break;
    case kDynamic:
      AddDynamicMpdInfo(&mpd);
      break;
    default:
      NOTREACHED() << "Unknown MPD type: " << type_;
      break;
  }

  DCHECK(doc);
  xmlDocSetRootElement(doc.get(), mpd.Release());
  return doc.release();
}

void MpdBuilder::AddCommonMpdInfo(XmlNode* mpd_node) {
  if (Positive(mpd_options_.min_buffer_time)) {
    mpd_node->SetStringAttribute(
        "minBufferTime",
        SecondsToXmlDuration(mpd_options_.min_buffer_time));
  } else {
    LOG(ERROR) << "minBufferTime value not specified.";
    // TODO(tinskip): Propagate error.
  }
}

void MpdBuilder::AddStaticMpdInfo(XmlNode* mpd_node) {
  DCHECK(mpd_node);
  DCHECK_EQ(MpdBuilder::kStatic, type_);

  static const char kStaticMpdType[] = "static";
  static const char kStaticMpdProfile[] =
      "urn:mpeg:dash:profile:isoff-on-demand:2011";
  mpd_node->SetStringAttribute("type", kStaticMpdType);
  mpd_node->SetStringAttribute("profiles", kStaticMpdProfile);
  mpd_node->SetStringAttribute(
      "mediaPresentationDuration",
      SecondsToXmlDuration(GetStaticMpdDuration(mpd_node)));
}

void MpdBuilder::AddDynamicMpdInfo(XmlNode* mpd_node) {
  DCHECK(mpd_node);
  DCHECK_EQ(MpdBuilder::kDynamic, type_);

  static const char kDynamicMpdType[] = "dynamic";
  static const char kDynamicMpdProfile[] =
      "urn:mpeg:dash:profile:isoff-live:2011";
  mpd_node->SetStringAttribute("type", kDynamicMpdType);
  mpd_node->SetStringAttribute("profiles", kDynamicMpdProfile);

  // 'availabilityStartTime' is required for dynamic profile. Calculate if
  // not already calculated.
  if (availability_start_time_.empty()) {
    double earliest_presentation_time;
    if (GetEarliestTimestamp(&earliest_presentation_time)) {
      availability_start_time_ =
          XmlDateTimeNowWithOffset(mpd_options_.availability_time_offset
                                   - std::ceil(earliest_presentation_time));
    } else {
      LOG(ERROR) << "Could not determine the earliest segment presentation "
                    "time for availabilityStartTime calculation.";
      // TODO(tinskip). Propagate an error.
    }
  }
  if (!availability_start_time_.empty())
    mpd_node->SetStringAttribute("availabilityStartTime", availability_start_time_);

  if (Positive(mpd_options_.minimum_update_period)) {
    mpd_node->SetStringAttribute(
        "minimumUpdatePeriod",
        SecondsToXmlDuration(mpd_options_.minimum_update_period));
  } else {
    LOG(WARNING) << "The profile is dynamic but no minimumUpdatePeriod "
                  "specified.";
  }

  SetIfPositive(
      "timeShiftBufferDepth", mpd_options_.time_shift_buffer_depth, mpd_node);
  SetIfPositive("suggestedPresentationDelay",
                mpd_options_.suggested_presentation_delay,
                mpd_node);
}

float MpdBuilder::GetStaticMpdDuration(XmlNode* mpd_node) {
  DCHECK(mpd_node);
  DCHECK_EQ(MpdBuilder::kStatic, type_);

  xmlNodePtr period_node = FindPeriodNode(mpd_node);
  DCHECK(period_node) << "Period element must be a child of mpd_node.";
  DCHECK(IsPeriodNode(period_node));

  // Attribute mediaPresentationDuration must be present for 'static' MPD. So
  // setting "PT0S" is required even if none of the representaions have duration
  // attribute.
  float max_duration = 0.0f;
  for (xmlNodePtr adaptation_set = xmlFirstElementChild(period_node);
       adaptation_set;
       adaptation_set = xmlNextElementSibling(adaptation_set)) {
    for (xmlNodePtr representation = xmlFirstElementChild(adaptation_set);
         representation;
         representation = xmlNextElementSibling(representation)) {
      float duration = 0.0f;
      if (GetDurationAttribute(representation, &duration)) {
        max_duration = max_duration > duration ? max_duration : duration;

        // 'duration' attribute is there only to help generate MPD, not
        // necessary for MPD, remove the attribute.
        xmlUnsetProp(representation, BAD_CAST "duration");
      }
    }
  }

  return max_duration;
}

bool MpdBuilder::GetEarliestTimestamp(double* timestamp_seconds) {
  DCHECK(timestamp_seconds);

  double earliest_timestamp(-1);
  for (std::list<AdaptationSet*>::const_iterator iter =
           adaptation_sets_.begin();
       iter != adaptation_sets_.end();
       ++iter) {
    double timestamp;
    if ((*iter)->GetEarliestTimestamp(&timestamp) &&
        ((earliest_timestamp < 0) || (timestamp < earliest_timestamp))) {
      earliest_timestamp = timestamp;
    }
  }
  if (earliest_timestamp < 0)
    return false;

  *timestamp_seconds = earliest_timestamp;
  return true;
}

AdaptationSet::AdaptationSet(uint32 adaptation_set_id,
                             const MpdOptions& mpd_options,
                             base::AtomicSequenceNumber* counter)
    : representations_deleter_(&representations_),
      representation_counter_(counter),
      id_(adaptation_set_id),
      mpd_options_(mpd_options) {
  DCHECK(counter);
}

AdaptationSet::~AdaptationSet() {}

Representation* AdaptationSet::AddRepresentation(const MediaInfo& media_info) {
  base::AutoLock scoped_lock(lock_);
  scoped_ptr<Representation> representation(new Representation(
      media_info, mpd_options_, representation_counter_->GetNext()));

  if (!representation->Init())
    return NULL;

  representations_.push_back(representation.get());
  return representation.release();
}

void AdaptationSet::AddContentProtectionElement(
    const ContentProtectionElement& content_protection_element) {
  base::AutoLock scoped_lock(lock_);
  content_protection_elements_.push_back(content_protection_element);
  RemoveDuplicateAttributes(&content_protection_elements_.back());
}

// Creates a copy of <AdaptationSet> xml element, iterate thru all the
// <Representation> (child) elements and add them to the copy.
xml::ScopedXmlPtr<xmlNode>::type AdaptationSet::GetXml() {
  base::AutoLock scoped_lock(lock_);
  AdaptationSetXmlNode adaptation_set;

  if (!adaptation_set.AddContentProtectionElements(
           content_protection_elements_)) {
    return xml::ScopedXmlPtr<xmlNode>::type();
  }

  std::list<Representation*>::iterator representation_it =
      representations_.begin();

  for (; representation_it != representations_.end(); ++representation_it) {
    xml::ScopedXmlPtr<xmlNode>::type child((*representation_it)->GetXml());
    if (!child.get() || !adaptation_set.AddChild(child.Pass()))
      return xml::ScopedXmlPtr<xmlNode>::type();
  }

  adaptation_set.SetId(id_);
  return adaptation_set.PassScopedPtr();
}

bool AdaptationSet::GetEarliestTimestamp(double* timestamp_seconds) {
  DCHECK(timestamp_seconds);

  base::AutoLock scoped_lock(lock_);
  double earliest_timestamp(-1);
  for (std::list<Representation*>::const_iterator iter =
           representations_.begin();
       iter != representations_.end();
       ++iter) {
    double timestamp;
    if ((*iter)->GetEarliestTimestamp(&timestamp) &&
        ((earliest_timestamp < 0) || (timestamp < earliest_timestamp))) {
      earliest_timestamp = timestamp;
    }
  }
  if (earliest_timestamp < 0)
    return false;

  *timestamp_seconds = earliest_timestamp;
  return true;
}


Representation::Representation(const MediaInfo& media_info,
                               const MpdOptions& mpd_options,
                               uint32 id)
    : media_info_(media_info),
      id_(id),
      bandwidth_estimator_(BandwidthEstimator::kUseAllBlocks),
      mpd_options_(mpd_options),
      start_number_(1) {}

Representation::~Representation() {}

bool Representation::Init() {
  codecs_ = GetCodecs(media_info_);
  if (codecs_.empty()) {
    LOG(ERROR) << "Missing codec info in MediaInfo.";
    return false;
  }

  const bool has_video_info = media_info_.video_info_size() > 0;
  const bool has_audio_info = media_info_.audio_info_size() > 0;

  if (!has_video_info && !has_audio_info) {
    // This is an error. Segment information can be in AdaptationSet, Period, or
    // MPD but the interface does not provide a way to set them.
    // See 5.3.9.1 ISO 23009-1:2012 for segment info.
    LOG(ERROR) << "Representation needs video or audio.";
    return false;
  }

  if (media_info_.container_type() == MediaInfo::CONTAINER_UNKNOWN) {
    LOG(ERROR) << "'container_type' in MediaInfo cannot be CONTAINER_UNKNOWN.";
    return false;
  }

  // Check video and then audio. Usually when there is audio + video, we take
  // video/<type>.
  if (has_video_info) {
    mime_type_ = GetVideoMimeType();
  } else if (has_audio_info) {
    mime_type_ = GetAudioMimeType();
  }

  return true;
}

void Representation::AddContentProtectionElement(
    const ContentProtectionElement& content_protection_element) {
  base::AutoLock scoped_lock(lock_);
  content_protection_elements_.push_back(content_protection_element);
  RemoveDuplicateAttributes(&content_protection_elements_.back());
}

void Representation::AddNewSegment(uint64 start_time,
                                   uint64 duration,
                                   uint64 size) {
  if (start_time == 0 && duration == 0) {
    LOG(WARNING) << "Got segment with start_time and duration == 0. Ignoring.";
    return;
  }

  base::AutoLock scoped_lock(lock_);
  if (IsContiguous(start_time, duration, size)) {
    ++segment_infos_.back().repeat;
  } else {
    SegmentInfo s = {start_time, duration, /* Not repeat. */ 0};
    segment_infos_.push_back(s);
  }

  bandwidth_estimator_.AddBlock(
      size, static_cast<double>(duration) / media_info_.reference_time_scale());

  SlideWindow();
  DCHECK_GE(segment_infos_.size(), 1u);
}

// Uses info in |media_info_| and |content_protection_elements_| to create a
// "Representation" node.
// MPD schema has strict ordering. The following must be done in order.
// AddVideoInfo() (possibly adds FramePacking elements), AddAudioInfo() (Adds
// AudioChannelConfig elements), AddContentProtectionElements*(), and
// AddVODOnlyInfo() (Adds segment info).
xml::ScopedXmlPtr<xmlNode>::type Representation::GetXml() {
  base::AutoLock scoped_lock(lock_);

  if (!HasRequiredMediaInfoFields()) {
    LOG(ERROR) << "MediaInfo missing required fields.";
    return xml::ScopedXmlPtr<xmlNode>::type();
  }

  const uint64 bandwidth = media_info_.has_bandwidth()
                               ? media_info_.bandwidth()
                               : bandwidth_estimator_.Estimate();

  DCHECK(!(HasVODOnlyFields(media_info_) && HasLiveOnlyFields(media_info_)));

  RepresentationXmlNode representation;
  // Mandatory fields for Representation.
  representation.SetId(id_);
  representation.SetIntegerAttribute("bandwidth", bandwidth);
  representation.SetStringAttribute("codecs", codecs_);
  representation.SetStringAttribute("mimeType", mime_type_);

  const bool has_video_info = media_info_.video_info_size() > 0;
  const bool has_audio_info = media_info_.audio_info_size() > 0;

  if (has_video_info &&
      !representation.AddVideoInfo(media_info_.video_info())) {
    LOG(ERROR) << "Failed to add video info to Representation XML.";
    return xml::ScopedXmlPtr<xmlNode>::type();
  }

  if (has_audio_info &&
      !representation.AddAudioInfo(media_info_.audio_info())) {
    LOG(ERROR) << "Failed to add audio info to Representation XML.";
    return xml::ScopedXmlPtr<xmlNode>::type();
  }

  if (!representation.AddContentProtectionElements(
           content_protection_elements_)) {
    return xml::ScopedXmlPtr<xmlNode>::type();
  }
  if (!representation.AddContentProtectionElementsFromMediaInfo(media_info_))
    return xml::ScopedXmlPtr<xmlNode>::type();

  if (HasVODOnlyFields(media_info_) &&
      !representation.AddVODOnlyInfo(media_info_)) {
    LOG(ERROR) << "Failed to add VOD segment info.";
    return xml::ScopedXmlPtr<xmlNode>::type();
  }

  if (HasLiveOnlyFields(media_info_) &&
      !representation.AddLiveOnlyInfo(
          media_info_, segment_infos_, start_number_)) {
    LOG(ERROR) << "Failed to add Live info.";
    return xml::ScopedXmlPtr<xmlNode>::type();
  }
  // TODO(rkuroiwa): It is likely that all representations have the exact same
  // SegmentTemplate. Optimize and propagate the tag up to AdaptationSet level.

  return representation.PassScopedPtr();
}

bool Representation::HasRequiredMediaInfoFields() {
  if (HasVODOnlyFields(media_info_) && HasLiveOnlyFields(media_info_)) {
    LOG(ERROR) << "MediaInfo cannot have both VOD and Live fields.";
    return false;
  }

  if (!media_info_.has_container_type()) {
    LOG(ERROR) << "MediaInfo missing required field: container_type.";
    return false;
  }

  if (HasVODOnlyFields(media_info_) && !media_info_.has_bandwidth()) {
    LOG(ERROR) << "Missing 'bandwidth' field. MediaInfo requires bandwidth for "
                  "static profile for generating a valid MPD.";
    return false;
  }

  VLOG_IF(3, HasLiveOnlyFields(media_info_) && !media_info_.has_bandwidth())
      << "MediaInfo missing field 'bandwidth'. Using estimated from "
         "segment size.";

  return true;
}

// In Debug builds, some of the irregular cases crash. It is probably a
// programming error but in production, it might not be best to stop the
// pipeline, especially for live.
bool Representation::IsContiguous(uint64 start_time,
                                  uint64 duration,
                                  uint64 size) const {
  if (segment_infos_.empty() || segment_infos_.back().duration != duration)
    return false;

  // Contiguous segment.
  const SegmentInfo& previous = segment_infos_.back();
  const uint64 previous_segment_end_time =
      previous.start_time +
      previous.duration * (previous.repeat + 1);
  if (previous_segment_end_time == start_time)
    return true;

  // A gap since previous.
  if (previous_segment_end_time < start_time)
    return false;

  // No out of order segments.
  const uint64 previous_segment_start_time =
      previous.start_time +
      previous.duration * previous.repeat;
  if (previous_segment_start_time >= start_time) {
    LOG(ERROR) << "Segments should not be out of order segment. Adding segment "
                  "with start_time == " << start_time
               << " but the previous segment starts at " << previous.start_time
               << ".";
    DCHECK(false);
    return false;
  }

  // No overlapping segments.
  const uint64 kRoundingErrorGrace = 5;
  if (start_time < previous_segment_end_time - kRoundingErrorGrace) {
    LOG(WARNING)
        << "Segments shold not be overlapping. The new segment starts at "
        << start_time << " but the previous segment ends at "
        << previous_segment_end_time << ".";
    DCHECK(false);
    return false;
  }

  // Within rounding error grace but technically not contiguous interms of MPD.
  return false;
}

void Representation::SlideWindow() {
  DCHECK(!segment_infos_.empty());
  if (mpd_options_.time_shift_buffer_depth <= 0.0)
    return;

  const uint32 time_scale = GetTimeScale(media_info_);
  DCHECK_GT(time_scale, 0u);

  uint64 time_shift_buffer_depth =
      static_cast<uint64>(mpd_options_.time_shift_buffer_depth * time_scale);

  // The start time of the latest segment is considered the current_play_time,
  // and this should guarantee that the latest segment will stay in the list.
  const uint64 current_play_time = LatestSegmentStartTime(segment_infos_);
  if (current_play_time <= time_shift_buffer_depth)
    return;

  const uint64 timeshift_limit = current_play_time - time_shift_buffer_depth;

  // First remove all the SegmentInfos that are completely out of range, by
  // looking at the very last segment's end time.
  std::list<SegmentInfo>::iterator first = segment_infos_.begin();
  std::list<SegmentInfo>::iterator last = first;
  size_t num_segments_removed = 0;
  for (; last != segment_infos_.end(); ++last) {
    const uint64 last_segment_end_time = LastSegmentEndTime(*last);
    if (timeshift_limit < last_segment_end_time)
      break;
    num_segments_removed += last->repeat + 1;
  }
  segment_infos_.erase(first, last);
  start_number_ += num_segments_removed;

  // Now some segment in the first SegmentInfo should be left in the list.
  SegmentInfo* first_segment_info = &segment_infos_.front();
  DCHECK_LE(timeshift_limit, LastSegmentEndTime(*first_segment_info));

  // Identify which segments should still be in the SegmentInfo.
  const int repeat_index =
      SearchTimedOutRepeatIndex(timeshift_limit, *first_segment_info);
  CHECK_GE(repeat_index, 0);
  if (repeat_index == 0)
    return;

  first_segment_info->start_time = first_segment_info->start_time +
                                   first_segment_info->duration * repeat_index;

  first_segment_info->repeat = first_segment_info->repeat - repeat_index;
  start_number_ += repeat_index;
}

std::string Representation::GetVideoMimeType() const {
  return GetMimeType("video", media_info_.container_type());
}

std::string Representation::GetAudioMimeType() const {
  return GetMimeType("audio", media_info_.container_type());
}

bool Representation::GetEarliestTimestamp(double* timestamp_seconds) {
  DCHECK(timestamp_seconds);

  base::AutoLock scoped_lock(lock_);
  if (segment_infos_.empty())
    return false;

  *timestamp_seconds =
      static_cast<double>(segment_infos_.begin()->start_time) /
      GetTimeScale(media_info_);
  return true;
}

}  // namespace edash_packager
