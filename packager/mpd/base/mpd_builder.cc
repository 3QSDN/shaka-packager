// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/mpd/base/mpd_builder.h"

#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include <cmath>
#include <iterator>
#include <list>
#include <string>

#include "packager/base/files/file_path.h"
#include "packager/base/logging.h"
#include "packager/base/memory/scoped_ptr.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/base/synchronization/lock.h"
#include "packager/base/time/time.h"
#include "packager/media/file/file.h"
#include "packager/mpd/base/content_protection_element.h"
#include "packager/mpd/base/language_utils.h"
#include "packager/mpd/base/mpd_utils.h"
#include "packager/mpd/base/xml/xml_node.h"

namespace edash_packager {

using base::FilePath;
using xml::XmlNode;
using xml::RepresentationXmlNode;
using xml::AdaptationSetXmlNode;

namespace {

const int kAdaptationSetGroupNotSet = -1;

std::string GetMimeType(const std::string& prefix,
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
  static const char kXmlNamespaceXsi[] =
      "http://www.w3.org/2001/XMLSchema-instance";
  static const char kXmlNamespaceXlink[] = "http://www.w3.org/1999/xlink";
  static const char kDashSchemaMpd2011[] =
      "urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd";
  static const char kCencNamespace[] = "urn:mpeg:cenc:2013";

  mpd->SetStringAttribute("xmlns", kXmlNamespace);
  mpd->SetStringAttribute("xmlns:xsi", kXmlNamespaceXsi);
  mpd->SetStringAttribute("xmlns:xlink", kXmlNamespaceXlink);
  mpd->SetStringAttribute("xsi:schemaLocation", kDashSchemaMpd2011);
  mpd->SetStringAttribute("xmlns:cenc", kCencNamespace);
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
  for (xmlNodePtr node = xml_node->GetRawPtr()->xmlChildrenNode; node != NULL;
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
std::string XmlDateTimeNowWithOffset(int32_t offset_seconds) {
  base::Time time = base::Time::Now();
  time += base::TimeDelta::FromSeconds(offset_seconds);
  base::Time::Exploded time_exploded;
  time.UTCExplode(&time_exploded);

  return base::StringPrintf("%4d-%02d-%02dT%02d:%02d:%02d", time_exploded.year,
                            time_exploded.month, time_exploded.day_of_month,
                            time_exploded.hour, time_exploded.minute,
                            time_exploded.second);
}

void SetIfPositive(const char* attr_name, double value, XmlNode* mpd) {
  if (Positive(value)) {
    mpd->SetStringAttribute(attr_name, SecondsToXmlDuration(value));
  }
}

uint32_t GetTimeScale(const MediaInfo& media_info) {
  if (media_info.has_reference_time_scale()) {
    return media_info.reference_time_scale();
  }

  if (media_info.has_video_info()) {
    return media_info.video_info().time_scale();
  }

  if (media_info.has_audio_info()) {
    return media_info.audio_info().time_scale();
  }

  LOG(WARNING) << "No timescale specified, using 1 as timescale.";
  return 1;
}

uint64_t LastSegmentStartTime(const SegmentInfo& segment_info) {
  return segment_info.start_time + segment_info.duration * segment_info.repeat;
}

// This is equal to |segment_info| end time
uint64_t LastSegmentEndTime(const SegmentInfo& segment_info) {
  return segment_info.start_time +
         segment_info.duration * (segment_info.repeat + 1);
}

uint64_t LatestSegmentStartTime(const std::list<SegmentInfo>& segments) {
  DCHECK(!segments.empty());
  const SegmentInfo& latest_segment = segments.back();
  return LastSegmentStartTime(latest_segment);
}

// Given |timeshift_limit|, finds out the number of segments that are no longer
// valid and should be removed from |segment_info|.
int SearchTimedOutRepeatIndex(uint64_t timeshift_limit,
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

std::string MakePathRelative(const std::string& path,
                             const std::string& mpd_dir) {
  return (path.find(mpd_dir) == 0) ? path.substr(mpd_dir.size()) : path;
}

// Check whether the video info has width and height.
// DASH IOP also requires several other fields for video representations, namely
// width, height, framerate, and sar.
bool HasRequiredVideoFields(const MediaInfo_VideoInfo& video_info) {
  if (!video_info.has_height() || !video_info.has_width()) {
    LOG(ERROR)
        << "Width and height are required fields for generating a valid MPD.";
    return false;
  }
  // These fields are not required for a valid MPD, but required for DASH IOP
  // compliant MPD. MpdBuilder can keep generating MPDs without these fields.
  LOG_IF(WARNING, !video_info.has_time_scale())
      << "Video info does not contain timescale required for "
         "calculating framerate. @frameRate is required for DASH IOP.";
  LOG_IF(WARNING, !video_info.has_frame_duration())
      << "Video info does not contain frame duration required "
         "for calculating framerate. @frameRate is required for DASH IOP.";
  LOG_IF(WARNING, !video_info.has_pixel_width())
      << "Video info does not contain pixel_width to calculate the sample "
         "aspect ratio required for DASH IOP.";
  LOG_IF(WARNING, !video_info.has_pixel_height())
      << "Video info does not contain pixel_height to calculate the sample "
         "aspect ratio required for DASH IOP.";
  return true;
}

// Euclidean algorithm.
// gcd(a,0) = a
// gcd(a,b) = gcd(b, a % b)
uint32_t GreatestCommonDivisor(uint32_t a, uint32_t b) {
  while (b != 0) {
    const uint32_t new_b = a % b;
    a = b;
    b = new_b;
  }
  return a;
}

// Returns the picture aspect ratio string e.g. "16:9", "4:3".
std::string GetPictureAspectRatio(uint32_t width, uint32_t height,
                                  uint32_t pixel_width, uint32_t pixel_height) {
  const uint32_t scaled_width = pixel_width * width;
  const uint32_t scaled_height = pixel_height * height;
  const uint32_t gcd = GreatestCommonDivisor(scaled_width, scaled_height);
  DCHECK_NE(gcd, 0u) << "GCD of width*pix_width (" << scaled_width
                     << ") and height*pix_height (" << scaled_height
                     << ") is 0.";

  const uint32_t par_x = scaled_width / gcd;
  const uint32_t par_y = scaled_height / gcd;
  return base::IntToString(par_x) + ":" + base::IntToString(par_y);
}

// Adds an entry to picture_aspect_ratio if the size of picture_aspect_ratio is
// less than 2 and video_info has both pixel width and pixel height.
void AddPictureAspectRatio(
  const MediaInfo::VideoInfo& video_info,
  std::set<std::string>* picture_aspect_ratio) {
  // If there are more than one entries in picture_aspect_ratio, the @par
  // attribute cannot be set, so skip.
  if (picture_aspect_ratio->size() > 1)
    return;

  if (video_info.width() == 0 || video_info.height() == 0 ||
      video_info.pixel_width() == 0 || video_info.pixel_height() == 0) {
    // If there is even one Representation without a @sar attribute, @par cannot
    // be calculated.
    // Just populate the set with at least 2 bogus strings so that further call
    // to this function will bail out immediately.
    picture_aspect_ratio->insert("bogus");
    picture_aspect_ratio->insert("entries");
    return;
  }

  const std::string par = GetPictureAspectRatio(
      video_info.width(), video_info.height(),
      video_info.pixel_width(), video_info.pixel_height());
  DVLOG(1) << "Setting par as: " << par
           << " for video with width: " << video_info.width()
           << " height: " << video_info.height()
           << " pixel_width: " << video_info.pixel_width() << " pixel_height; "
           << video_info.pixel_height();
  picture_aspect_ratio->insert(par);
}

std::string RoleToText(AdaptationSet::Role role) {
  // Using switch so that the compiler can detect whether there is a case that's
  // not being handled.
  switch (role) {
    case AdaptationSet::kRoleCaption:
      return "caption";
    case AdaptationSet::kRoleSubtitle:
      return "subtitle";
    case AdaptationSet::kRoleMain:
      return "main";
    case AdaptationSet::kRoleAlternate:
      return "alternate";
    case AdaptationSet::kRoleSupplementary:
      return "supplementary";
    case AdaptationSet::kRoleCommentary:
      return "commentary";
    case AdaptationSet::kRoleDub:
      return "dub";
    default:
      NOTREACHED();
      return "";
  }

  NOTREACHED();
  return "";
}

// Spooky static initialization/cleanup of libxml.
class LibXmlInitializer {
 public:
  LibXmlInitializer() : initialized_(false) {
    base::AutoLock lock(lock_);
    if (!initialized_) {
      xmlInitParser();
      initialized_ = true;
    }
  }

  ~LibXmlInitializer() {
    base::AutoLock lock(lock_);
    if (initialized_) {
      xmlCleanupParser();
      initialized_ = false;
    }
  }

 private:
  base::Lock lock_;
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(LibXmlInitializer);
};

class RepresentationStateChangeListenerImpl
    : public RepresentationStateChangeListener {
 public:
  // |adaptation_set| is not owned by this class.
  RepresentationStateChangeListenerImpl(uint32_t representation_id,
                                        AdaptationSet* adaptation_set)
      : representation_id_(representation_id), adaptation_set_(adaptation_set) {
    DCHECK(adaptation_set_);
  }
  virtual ~RepresentationStateChangeListenerImpl() OVERRIDE {}

  // RepresentationStateChangeListener implementation.
  virtual void OnNewSegmentForRepresentation(uint64_t start_time,
                                             uint64_t duration) OVERRIDE {
    adaptation_set_->OnNewSegmentForRepresentation(representation_id_,
                                                   start_time, duration);
  }

  virtual void OnSetFrameRateForRepresentation(uint32_t frame_duration,
                                               uint32_t timescale) OVERRIDE {
    adaptation_set_->OnSetFrameRateForRepresentation(representation_id_,
                                                     frame_duration, timescale);
  }

 private:
  const uint32_t representation_id_;
  AdaptationSet* const adaptation_set_;

  DISALLOW_COPY_AND_ASSIGN(RepresentationStateChangeListenerImpl);
};

}  // namespace

MpdBuilder::MpdBuilder(MpdType type, const MpdOptions& mpd_options)
    : type_(type),
      mpd_options_(mpd_options),
      adaptation_sets_deleter_(&adaptation_sets_) {}

MpdBuilder::~MpdBuilder() {}

void MpdBuilder::AddBaseUrl(const std::string& base_url) {
  base::AutoLock scoped_lock(lock_);
  base_urls_.push_back(base_url);
}

AdaptationSet* MpdBuilder::AddAdaptationSet(const std::string& lang) {
  base::AutoLock scoped_lock(lock_);
  scoped_ptr<AdaptationSet> adaptation_set(
      new AdaptationSet(adaptation_set_counter_.GetNext(), lang, mpd_options_,
                        type_,
                        &representation_counter_));

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
  static LibXmlInitializer lib_xml_initializer;

  xml::ScopedXmlPtr<xmlDoc>::type doc(GenerateMpd());
  if (!doc.get())
    return false;

  static const int kNiceFormat = 1;
  int doc_str_size = 0;
  xmlChar* doc_str = NULL;
  xmlDocDumpFormatMemoryEnc(doc.get(), &doc_str, &doc_str_size, "UTF-8",
                            kNiceFormat);

  bool result = WriteXmlCharArrayToOutput(doc_str, doc_str_size, output);
  xmlFree(doc_str);

  // Cleanup, free the doc.
  doc.reset();
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
        "minBufferTime", SecondsToXmlDuration(mpd_options_.min_buffer_time));
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
          XmlDateTimeNowWithOffset(mpd_options_.availability_time_offset -
                                   std::ceil(earliest_presentation_time));
    } else {
      LOG(ERROR) << "Could not determine the earliest segment presentation "
                    "time for availabilityStartTime calculation.";
      // TODO(tinskip). Propagate an error.
    }
  }
  if (!availability_start_time_.empty())
    mpd_node->SetStringAttribute("availabilityStartTime",
                                 availability_start_time_);

  if (Positive(mpd_options_.minimum_update_period)) {
    mpd_node->SetStringAttribute(
        "minimumUpdatePeriod",
        SecondsToXmlDuration(mpd_options_.minimum_update_period));
  } else {
    LOG(WARNING) << "The profile is dynamic but no minimumUpdatePeriod "
                    "specified.";
  }

  SetIfPositive("timeShiftBufferDepth", mpd_options_.time_shift_buffer_depth,
                mpd_node);
  SetIfPositive("suggestedPresentationDelay",
                mpd_options_.suggested_presentation_delay, mpd_node);
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
       adaptation_set; adaptation_set = xmlNextElementSibling(adaptation_set)) {
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
       iter != adaptation_sets_.end(); ++iter) {
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

void MpdBuilder::MakePathsRelativeToMpd(const std::string& mpd_path,
                                        MediaInfo* media_info) {
  DCHECK(media_info);
  const std::string kFileProtocol("file://");
  std::string mpd_file_path = (mpd_path.find(kFileProtocol) == 0)
                                  ? mpd_path.substr(kFileProtocol.size())
                                  : mpd_path;

  if (!mpd_file_path.empty()) {
    std::string mpd_dir(
        FilePath(mpd_file_path).DirName().AsEndingWithSeparator().value());
    if (!mpd_dir.empty()) {
      if (media_info->has_media_file_name()) {
        media_info->set_media_file_name(
            MakePathRelative(media_info->media_file_name(), mpd_dir));
      }
      if (media_info->has_init_segment_name()) {
        media_info->set_init_segment_name(
            MakePathRelative(media_info->init_segment_name(), mpd_dir));
      }
      if (media_info->has_segment_template()) {
        media_info->set_segment_template(
            MakePathRelative(media_info->segment_template(), mpd_dir));
      }
    }
  }
}

AdaptationSet::AdaptationSet(uint32_t adaptation_set_id,
                             const std::string& lang,
                             const MpdOptions& mpd_options,
                             MpdBuilder::MpdType mpd_type,
                             base::AtomicSequenceNumber* counter)
    : representations_deleter_(&representations_),
      representation_counter_(counter),
      id_(adaptation_set_id),
      lang_(lang),
      mpd_options_(mpd_options),
      mpd_type_(mpd_type),
      group_(kAdaptationSetGroupNotSet),
      segments_aligned_(kSegmentAlignmentUnknown),
      force_set_segment_alignment_(false) {
  DCHECK(counter);
}

AdaptationSet::~AdaptationSet() {}

Representation* AdaptationSet::AddRepresentation(const MediaInfo& media_info) {
  base::AutoLock scoped_lock(lock_);
  const uint32_t representation_id = representation_counter_->GetNext();
  // Note that AdaptationSet outlive Representation, so this object
  // will die before AdaptationSet.
  scoped_ptr<RepresentationStateChangeListener> listener(
      new RepresentationStateChangeListenerImpl(representation_id, this));
  scoped_ptr<Representation> representation(new Representation(
      media_info, mpd_options_, representation_id, listener.Pass()));

  if (!representation->Init())
    return NULL;

  // For videos, record the width, height, and the frame rate to calculate the
  // max {width,height,framerate} required for DASH IOP.
  if (media_info.has_video_info()) {
    const MediaInfo::VideoInfo& video_info = media_info.video_info();
    DCHECK(video_info.has_width());
    DCHECK(video_info.has_height());
    video_widths_.insert(video_info.width());
    video_heights_.insert(video_info.height());

    if (video_info.has_time_scale() && video_info.has_frame_duration())
      RecordFrameRate(video_info.frame_duration(), video_info.time_scale());

    AddPictureAspectRatio(video_info, &picture_aspect_ratio_);
  }

  if (media_info.has_video_info()) {
    content_type_ = "video";
  } else if (media_info.has_audio_info()) {
    content_type_ = "audio";
  }

  representations_.push_back(representation.get());
  return representation.release();
}

void AdaptationSet::AddContentProtectionElement(
    const ContentProtectionElement& content_protection_element) {
  base::AutoLock scoped_lock(lock_);
  content_protection_elements_.push_back(content_protection_element);
  RemoveDuplicateAttributes(&content_protection_elements_.back());
}

void AdaptationSet::AddRole(Role role) {
  roles_.insert(role);
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
    if (!child || !adaptation_set.AddChild(child.Pass()))
      return xml::ScopedXmlPtr<xmlNode>::type();
  }

  adaptation_set.SetId(id_);
  adaptation_set.SetStringAttribute("contentType", content_type_);
  if (!lang_.empty() && lang_ != "und") {
    adaptation_set.SetStringAttribute("lang", LanguageToShortestForm(lang_));
  }

  // Note that std::{set,map} are ordered, so the last element is the max value.
  if (video_widths_.size() == 1) {
    adaptation_set.SetIntegerAttribute("width", *video_widths_.begin());
  } else if (video_widths_.size() > 1) {
    adaptation_set.SetIntegerAttribute("maxWidth", *video_widths_.rbegin());
  }
  if (video_heights_.size() == 1) {
    adaptation_set.SetIntegerAttribute("height", *video_heights_.begin());
  } else if (video_heights_.size() > 1) {
    adaptation_set.SetIntegerAttribute("maxHeight", *video_heights_.rbegin());
  }

  if (video_frame_rates_.size() == 1) {
    adaptation_set.SetStringAttribute("frameRate",
                                      video_frame_rates_.begin()->second);
  } else if (video_frame_rates_.size() > 1) {
    adaptation_set.SetStringAttribute("maxFrameRate",
                                      video_frame_rates_.rbegin()->second);
  }

  // Note: must be checked before checking segments_aligned_ (below).
  if (mpd_type_ == MpdBuilder::kStatic) {
    CheckVodSegmentAlignment();
  }

  if (segments_aligned_ == kSegmentAlignmentTrue) {
    adaptation_set.SetStringAttribute(mpd_type_ == MpdBuilder::kStatic
                                          ? "subSegmentAlignment"
                                          : "segmentAlignment",
                                      "true");
  }

  if (picture_aspect_ratio_.size() == 1)
    adaptation_set.SetStringAttribute("par", *picture_aspect_ratio_.begin());

  if (group_ >= 0)
    adaptation_set.SetIntegerAttribute("group", group_);

  for (std::set<Role>::const_iterator role_it = roles_.begin();
       role_it != roles_.end(); ++role_it) {
    adaptation_set.AddRoleElement("urn:mpeg:dash:role:2011",
                                  RoleToText(*role_it));
  }
  return adaptation_set.PassScopedPtr();
}

void AdaptationSet::ForceSetSegmentAlignment(bool segment_alignment) {
  segments_aligned_ =
      segment_alignment ? kSegmentAlignmentTrue : kSegmentAlignmentFalse;
  force_set_segment_alignment_ = true;
}

void AdaptationSet::SetGroup(int group_number) {
  group_ = group_number;
}

int AdaptationSet::Group() const {
  return group_;
}

// Check segmentAlignment for Live here. Storing all start_time and duration
// will out-of-memory because there's no way of knowing when it will end.
// VOD subSegmentAlignment check is *not* done here because it is possible
// that some Representations might not have been added yet (e.g. a thread is
// assigned per muxer so one might run faster than others).
// To be clear, for Live, all Representations should be added before a
// segment is added.
void AdaptationSet::OnNewSegmentForRepresentation(uint32_t representation_id,
                                                  uint64_t start_time,
                                                  uint64_t duration) {
  base::AutoLock scoped_lock(lock_);

  if (mpd_type_ == MpdBuilder::kDynamic) {
    CheckLiveSegmentAlignment(representation_id, start_time, duration);
  } else {
    representation_segment_start_times_[representation_id].push_back(
        start_time);
  }
}

void AdaptationSet::OnSetFrameRateForRepresentation(
    uint32_t /* representation_id */,
    uint32_t frame_duration,
    uint32_t timescale) {
  base::AutoLock scoped_lock(lock_);
  RecordFrameRate(frame_duration, timescale);
}

bool AdaptationSet::GetEarliestTimestamp(double* timestamp_seconds) {
  DCHECK(timestamp_seconds);

  base::AutoLock scoped_lock(lock_);
  double earliest_timestamp(-1);
  for (std::list<Representation*>::const_iterator iter =
           representations_.begin();
       iter != representations_.end(); ++iter) {
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

// This implementation assumes that each representations' segments' are
// contiguous.
// Also assumes that all Representations are added before this is called.
// This checks whether the first elements of the lists in
// representation_segment_start_times_ are aligned.
// For example, suppose this method was just called with args rep_id=2
// start_time=1.
// 1 -> [1, 100, 200]
// 2 -> [1]
// The timestamps of the first elements match, so this flags
// segments_aligned_=true.
// Also since the first segment start times match, the first element of all the
// lists are removed, so the map of lists becomes:
// 1 -> [100, 200]
// 2 -> []
// Note that there could be false positives.
// e.g. just got rep_id=3 start_time=1 duration=300, and the duration of the
// whole AdaptationSet is 300.
// 1 -> [1, 100, 200]
// 2 -> [1, 90, 100]
// 3 -> [1]
// They are not aligned but this will be marked as aligned.
// But since this is unlikely to happen in the packager (and to save
// computation), this isn't handled at the moment.
void AdaptationSet::CheckLiveSegmentAlignment(uint32_t representation_id,
                                              uint64_t start_time,
                                              uint64_t /* duration */) {
  if (segments_aligned_ == kSegmentAlignmentFalse ||
      force_set_segment_alignment_) {
    return;
  }

  std::list<uint64_t>& representation_start_times =
      representation_segment_start_times_[representation_id];
  representation_start_times.push_back(start_time);
  // There's no way to detemine whether the segments are aligned if some
  // representations do not have any segments.
  if (representation_segment_start_times_.size() != representations_.size())
    return;

  DCHECK(!representation_start_times.empty());
  const uint64_t expected_start_time = representation_start_times.front();
  for (RepresentationTimeline::const_iterator it =
           representation_segment_start_times_.begin();
       it != representation_segment_start_times_.end(); ++it) {
    // If there are no entries in a list, then there is no way for the
    // segment alignment status to change.
    // Note that it can be empty because entries get deleted below.
    if (it->second.empty())
      return;

    if (expected_start_time != it->second.front()) {
      // Flag as false and clear the start times data, no need to keep it
      // around.
      segments_aligned_ = kSegmentAlignmentFalse;
      representation_segment_start_times_.clear();
      return;
    }
  }
  segments_aligned_ = kSegmentAlignmentTrue;

  for (RepresentationTimeline::iterator it =
           representation_segment_start_times_.begin();
       it != representation_segment_start_times_.end(); ++it) {
    it->second.pop_front();
  }
}

// Make sure all segements start times match for all Representations.
// This assumes that the segments are contiguous.
void AdaptationSet::CheckVodSegmentAlignment() {
  if (segments_aligned_ == kSegmentAlignmentFalse ||
      force_set_segment_alignment_) {
    return;
  }
  if (representation_segment_start_times_.empty())
    return;
  if (representation_segment_start_times_.size() == 1) {
    segments_aligned_ = kSegmentAlignmentTrue;
    return;
  }

  // This is not the most efficient implementation to compare the values
  // because expected_time_line is compared against all other time lines, but
  // probably the most readable.
  const std::list<uint64_t>& expected_time_line =
      representation_segment_start_times_.begin()->second;

  bool all_segment_time_line_same_length = true;
  // Note that the first entry is skipped because it is expected_time_line.
  RepresentationTimeline::const_iterator it =
      representation_segment_start_times_.begin();
  for (++it; it != representation_segment_start_times_.end(); ++it) {
    const std::list<uint64_t>& other_time_line = it->second;
    if (expected_time_line.size() != other_time_line.size()) {
      all_segment_time_line_same_length = false;
    }

    const std::list<uint64_t>* longer_list = &other_time_line;
    const std::list<uint64_t>* shorter_list = &expected_time_line;
    if (expected_time_line.size() > other_time_line.size()) {
      shorter_list = &other_time_line;
      longer_list = &expected_time_line;
    }

    if (!std::equal(shorter_list->begin(), shorter_list->end(),
                    longer_list->begin())) {
      // Some segments are definitely unaligned.
      segments_aligned_ = kSegmentAlignmentFalse;
      representation_segment_start_times_.clear();
      return;
    }
  }

  // TODO(rkuroiwa): The right way to do this is to also check the durations.
  // For example:
  // (a)  3 4 5
  // (b)  3 4 5 6
  // could be true or false depending on the length of the third segment of (a).
  // i.e. if length of the third segment is 2, then this is not aligned.
  if (!all_segment_time_line_same_length) {
    segments_aligned_ = kSegmentAlignmentUnknown;
    return;
  }

  segments_aligned_ = kSegmentAlignmentTrue;
}

// Since all AdaptationSet cares about is the maxFrameRate, representation_id
// is not passed to this method.
void AdaptationSet::RecordFrameRate(uint32_t frame_duration,
                                    uint32_t timescale) {
  if (frame_duration == 0) {
    LOG(ERROR) << "Frame duration is 0 and cannot be set.";
    return;
  }
  video_frame_rates_[static_cast<double>(timescale) / frame_duration] =
      base::IntToString(timescale) + "/" + base::IntToString(frame_duration);
}

Representation::Representation(
    const MediaInfo& media_info,
    const MpdOptions& mpd_options,
    uint32_t id,
    scoped_ptr<RepresentationStateChangeListener> state_change_listener)
    : media_info_(media_info),
      id_(id),
      bandwidth_estimator_(BandwidthEstimator::kUseAllBlocks),
      mpd_options_(mpd_options),
      start_number_(1),
      state_change_listener_(state_change_listener.Pass()) {}

Representation::~Representation() {}

bool Representation::Init() {
  codecs_ = GetCodecs(media_info_);
  if (codecs_.empty()) {
    LOG(ERROR) << "Missing codec info in MediaInfo.";
    return false;
  }

  const bool has_video_info = media_info_.has_video_info();
  const bool has_audio_info = media_info_.has_audio_info();

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

  // For mimetypes, this checks the video and then audio. Usually when there is
  // audio + video, we take video/<type>.
  if (has_video_info) {
    mime_type_ = GetVideoMimeType();
    if (!HasRequiredVideoFields(media_info_.video_info())) {
      LOG(ERROR) << "Missing required fields to create a video Representation.";
      return false;
    }
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

void Representation::AddNewSegment(uint64_t start_time,
                                   uint64_t duration,
                                   uint64_t size) {
  if (start_time == 0 && duration == 0) {
    LOG(WARNING) << "Got segment with start_time and duration == 0. Ignoring.";
    return;
  }

  base::AutoLock scoped_lock(lock_);
  if (state_change_listener_)
    state_change_listener_->OnNewSegmentForRepresentation(start_time, duration);
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

void Representation::SetSampleDuration(uint32_t sample_duration) {
  base::AutoLock scoped_lock(lock_);

  if (media_info_.has_video_info()) {
    media_info_.mutable_video_info()->set_frame_duration(sample_duration);
    if (state_change_listener_) {
      state_change_listener_->OnSetFrameRateForRepresentation(
          sample_duration, media_info_.video_info().time_scale());
    }
  }
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

  const uint64_t bandwidth = media_info_.has_bandwidth()
                                 ? media_info_.bandwidth()
                                 : bandwidth_estimator_.Estimate();

  DCHECK(!(HasVODOnlyFields(media_info_) && HasLiveOnlyFields(media_info_)));

  RepresentationXmlNode representation;
  // Mandatory fields for Representation.
  representation.SetId(id_);
  representation.SetIntegerAttribute("bandwidth", bandwidth);
  representation.SetStringAttribute("codecs", codecs_);
  representation.SetStringAttribute("mimeType", mime_type_);

  const bool has_video_info = media_info_.has_video_info();
  const bool has_audio_info = media_info_.has_audio_info();

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

  if (HasVODOnlyFields(media_info_) &&
      !representation.AddVODOnlyInfo(media_info_)) {
    LOG(ERROR) << "Failed to add VOD segment info.";
    return xml::ScopedXmlPtr<xmlNode>::type();
  }

  if (HasLiveOnlyFields(media_info_) &&
      !representation.AddLiveOnlyInfo(media_info_, segment_infos_,
                                      start_number_)) {
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

bool Representation::IsContiguous(uint64_t start_time,
                                  uint64_t duration,
                                  uint64_t size) const {
  if (segment_infos_.empty())
    return false;

  // Contiguous segment.
  const SegmentInfo& previous = segment_infos_.back();
  const uint64_t previous_segment_end_time =
      previous.start_time + previous.duration * (previous.repeat + 1);
  if (previous_segment_end_time == start_time &&
      segment_infos_.back().duration == duration) {
    return true;
  }

  // No out of order segments.
  const uint64_t previous_segment_start_time =
      previous.start_time + previous.duration * previous.repeat;
  if (previous_segment_start_time >= start_time) {
    LOG(ERROR) << "Segments should not be out of order segment. Adding segment "
                  "with start_time == "
               << start_time << " but the previous segment starts at "
               << previous.start_time << ".";
    return false;
  }

  // A gap since previous.
  const uint64_t kRoundingErrorGrace = 5;
  if (previous_segment_end_time + kRoundingErrorGrace < start_time) {
    LOG(WARNING) << "Found a gap of size "
                 << (start_time - previous_segment_end_time)
                 << " > kRoundingErrorGrace (" << kRoundingErrorGrace
                 << "). The new segment starts at " << start_time
                 << " but the previous segment ends at "
                 << previous_segment_end_time << ".";
    return false;
  }

  // No overlapping segments.
  if (start_time < previous_segment_end_time - kRoundingErrorGrace) {
    LOG(WARNING)
        << "Segments should not be overlapping. The new segment starts at "
        << start_time << " but the previous segment ends at "
        << previous_segment_end_time << ".";
    return false;
  }

  // Within rounding error grace but technically not contiguous in terms of MPD.
  return false;
}

void Representation::SlideWindow() {
  DCHECK(!segment_infos_.empty());
  if (mpd_options_.time_shift_buffer_depth <= 0.0)
    return;

  const uint32_t time_scale = GetTimeScale(media_info_);
  DCHECK_GT(time_scale, 0u);

  uint64_t time_shift_buffer_depth =
      static_cast<uint64_t>(mpd_options_.time_shift_buffer_depth * time_scale);

  // The start time of the latest segment is considered the current_play_time,
  // and this should guarantee that the latest segment will stay in the list.
  const uint64_t current_play_time = LatestSegmentStartTime(segment_infos_);
  if (current_play_time <= time_shift_buffer_depth)
    return;

  const uint64_t timeshift_limit = current_play_time - time_shift_buffer_depth;

  // First remove all the SegmentInfos that are completely out of range, by
  // looking at the very last segment's end time.
  std::list<SegmentInfo>::iterator first = segment_infos_.begin();
  std::list<SegmentInfo>::iterator last = first;
  size_t num_segments_removed = 0;
  for (; last != segment_infos_.end(); ++last) {
    const uint64_t last_segment_end_time = LastSegmentEndTime(*last);
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

  *timestamp_seconds = static_cast<double>(segment_infos_.begin()->start_time) /
                       GetTimeScale(media_info_);
  return true;
}

}  // namespace edash_packager
