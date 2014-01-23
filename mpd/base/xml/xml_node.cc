#include "mpd/base/xml/xml_node.h"

#include <set>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "mpd/base/media_info.pb.h"

using dash_packager::xml::XmlNode;

using dash_packager::MediaInfo;
typedef MediaInfo::ContentProtectionXml ContentProtectionXml;
typedef ContentProtectionXml::AttributeNameValuePair AttributeNameValuePair;

namespace {

std::string RangeToString(const dash_packager::Range& range) {
  return base::Uint64ToString(range.begin()) + "-" +
         base::Uint64ToString(range.end());
}

bool SetAttributes(const google::protobuf::RepeatedPtrField<
                       AttributeNameValuePair>& attributes,
                   XmlNode* xml_node) {
  DCHECK(xml_node);
  for (int i = 0; i < attributes.size(); ++i) {
    const AttributeNameValuePair& attribute = attributes.Get(i);
    const std::string& name = attribute.name();
    const std::string& value = attribute.value();

    if (name.empty()) {
      LOG(ERROR) << "For element "
                 << reinterpret_cast<const char*>(xml_node->GetRawPtr()->name)
                 << ", no name specified for attribute with value: " << value;
      return false;
    }

    xml_node->SetStringAttribute(name.c_str(), value);
  }

  return true;
}

// This function is recursive. Note that elements.size() == 0 is a terminating
// condition.
bool AddSubelements(const google::protobuf::RepeatedPtrField<
                        ContentProtectionXml::Element>& elements,
                    XmlNode* xml_node) {
  DCHECK(xml_node);
  for (int i = 0; i < elements.size(); ++i) {
    const ContentProtectionXml::Element& subelement = elements.Get(i);
    const std::string& subelement_name = subelement.name();
    if (subelement_name.empty()) {
      LOG(ERROR) << "Subelement name was not specified for node "
                 << reinterpret_cast<const char*>(xml_node->GetRawPtr()->name);
      return false;
    }

    XmlNode subelement_xml_node(subelement_name.c_str());
    if (!SetAttributes(subelement.attributes(), &subelement_xml_node)) {
      LOG(ERROR) << "Failed to set attributes for " << subelement_name;
      return false;
    }

    if (!AddSubelements(subelement.subelements(), &subelement_xml_node)) {
      LOG(ERROR) << "Failed to add subelements to " << subelement_name;
      return false;
    }

    if (!xml_node->AddChild(subelement_xml_node.PassScopedPtr())) {
      LOG(ERROR) << "Failed to add subelement " << subelement_name << " to "
                 << reinterpret_cast<const char*>(xml_node->GetRawPtr()->name);
      return false;
    }
  }

  return true;
}

// Returns true if 'schemeIdUri' is set in |content_protection_xml| and sets
// |scheme_id_uri_output|. This function checks
// ContentProtectionXml::scheme_id_uri before searching thru attributes.
bool GetSchemeIdAttribute(const ContentProtectionXml& content_protection_xml,
                          std::string* scheme_id_uri_output) {
  // Common case where 'schemeIdUri' is set directly.
  if (content_protection_xml.has_scheme_id_uri()) {
    scheme_id_uri_output->assign(content_protection_xml.scheme_id_uri());
    return true;
  }

  // 'schemeIdUri' is one of the attributes.
  for (int i = 0; i < content_protection_xml.attributes().size(); ++i) {
    const AttributeNameValuePair& attribute =
        content_protection_xml.attributes(i);
    const std::string& name = attribute.name();
    const std::string& value = attribute.value();
    if (name == "schemeIdUri") {
      if (value.empty())
        LOG(WARNING) << "schemeIdUri is specified with an empty string.";

      // 'schemeIdUri' is a mandatory field but MPD doesn't care what the actual
      // value is, proceed.
      scheme_id_uri_output->assign(value);
      return true;
    }
  }

  return false;
}

// Translates ContentProtectionXml to XmlNode.
// content_protection_xml.scheme_id_uri and content_protection_xml.value takes
// precedence over attributes in content_protection_xml.attributes.
bool TranslateToContentProtectionXmlNode(
    const ContentProtectionXml& content_protection_xml,
    XmlNode* xml_node_content_protection) {
  std::string scheme_id_uri;
  if (!GetSchemeIdAttribute(content_protection_xml, &scheme_id_uri)) {
    LOG(ERROR) << "ContentProtection element requires schemeIdUri.";
    return false;
  }

  if (!SetAttributes(content_protection_xml.attributes(),
                     xml_node_content_protection)) {
    LOG(ERROR) << "Failed to set attributes for ContentProtection.";
    return false;
  }

  if (!AddSubelements(content_protection_xml.subelements(),
                      xml_node_content_protection)) {
    LOG(ERROR) << "Failed to add sublements to ContentProtection.";
    return false;
  }

  // Add 'schemeIdUri' and 'value' attributes after SetAttributes() to avoid
  // being overridden by content_protection_xml.attributes().
  xml_node_content_protection->SetStringAttribute("schemeIdUri", scheme_id_uri);

  if (content_protection_xml.has_value()) {
    // Note that |value| is an optional field.
    xml_node_content_protection->SetStringAttribute(
        "value", content_protection_xml.value());
  }

  return true;
}

}  // namespace

namespace dash_packager {
namespace xml {

XmlNode::XmlNode(const char* name) : node_(xmlNewNode(NULL, BAD_CAST name)) {
  DCHECK(name);
  DCHECK(node_);
}

XmlNode::~XmlNode() {}

bool XmlNode::AddChild(ScopedXmlPtr<xmlNode>::type child) {
  DCHECK(node_);
  DCHECK(child);
  if (!xmlAddChild(node_.get(), child.get()))
    return false;

  // Reaching here means the ownership of |child| transfered to |node_|.
  // Release the pointer so that it doesn't get destructed in this scope.
  child.release();
  return true;
}

void XmlNode::SetStringAttribute(const char* attribute_name,
                                 const std::string& attribute) {
  DCHECK(node_);
  DCHECK(attribute_name);
  xmlNewProp(node_.get(), BAD_CAST attribute_name, BAD_CAST attribute.c_str());
}

void XmlNode::SetIntegerAttribute(const char* attribute_name, uint64 number) {
  DCHECK(node_);
  DCHECK(attribute_name);
  xmlNewProp(node_.get(),
             BAD_CAST attribute_name,
             BAD_CAST (base::Uint64ToString(number).c_str()));
}

void XmlNode::SetFloatingPointAttribute(const char* attribute_name,
                                        double number) {
  DCHECK(node_);
  DCHECK(attribute_name);
  xmlNewProp(node_.get(),
             BAD_CAST attribute_name,
             BAD_CAST (base::DoubleToString(number).c_str()));
}

void XmlNode::SetId(uint32 id) {
  SetIntegerAttribute("id", id);
}

void XmlNode::SetContent(const std::string& content) {
  DCHECK(node_);
  xmlNodeSetContent(node_.get(), BAD_CAST content.c_str());
}

ScopedXmlPtr<xmlNode>::type XmlNode::PassScopedPtr() {
  DLOG(INFO) << "Passing node_.";
  DCHECK(node_);
  return node_.Pass();
}

xmlNodePtr XmlNode::Release() {
  DLOG(INFO) << "Releasing node_.";
  DCHECK(node_);
  return node_.release();
}

xmlNodePtr XmlNode::GetRawPtr() {
  return node_.get();
}

RepresentationBaseXmlNode::RepresentationBaseXmlNode(const char* name)
    : XmlNode(name) {}
RepresentationBaseXmlNode::~RepresentationBaseXmlNode() {}

bool RepresentationBaseXmlNode::AddContentProtectionElements(
    const std::list<ContentProtectionElement>& content_protection_elements) {
  std::list<ContentProtectionElement>::const_iterator content_protection_it =
      content_protection_elements.begin();
  for (; content_protection_it != content_protection_elements.end();
       ++content_protection_it) {
    if (!AddContentProtectionElement(*content_protection_it))
      return false;
  }

  return true;
}

bool RepresentationBaseXmlNode::AddContentProtectionElementsFromMediaInfo(
    const MediaInfo& media_info) {
  const bool has_content_protections =
      media_info.content_protections().size() > 0;

  if (!has_content_protections)
    return true;

  for (int i = 0; i < media_info.content_protections().size(); ++i) {
    const ContentProtectionXml& content_protection_xml =
        media_info.content_protections(i);
    XmlNode content_protection_node("ContentProtection");
    if (!TranslateToContentProtectionXmlNode(content_protection_xml,
                                             &content_protection_node)) {
      LOG(ERROR) << "Failed to make ContentProtection element from MediaInfo.";
      return false;
    }

    if (!AddChild(content_protection_node.PassScopedPtr())) {
      LOG(ERROR) << "Failed to add ContentProtection to Representation.";
      return false;
    }
  }

  return true;
}

bool RepresentationBaseXmlNode::AddContentProtectionElement(
    const ContentProtectionElement& content_protection_element) {
  XmlNode content_protection_node("ContentProtection");

  content_protection_node.SetStringAttribute("value",
                                             content_protection_element.value);
  content_protection_node.SetStringAttribute(
      "schemeIdUri", content_protection_element.scheme_id_uri);

  typedef std::map<std::string, std::string> AttributesMapType;
  const AttributesMapType& additional_attributes =
      content_protection_element.additional_attributes;

  AttributesMapType::const_iterator attributes_it =
      additional_attributes.begin();
  for (; attributes_it != additional_attributes.end(); ++attributes_it) {
    content_protection_node.SetStringAttribute(attributes_it->first.c_str(),
                                               attributes_it->second);
  }

  content_protection_node.SetContent(content_protection_element.subelements);
  return AddChild(content_protection_node.PassScopedPtr());
}

AdaptationSetXmlNode::AdaptationSetXmlNode()
    : RepresentationBaseXmlNode("AdaptationSet") {}
AdaptationSetXmlNode::~AdaptationSetXmlNode() {}

RepresentationXmlNode::RepresentationXmlNode()
    : RepresentationBaseXmlNode("Representation") {}
RepresentationXmlNode::~RepresentationXmlNode() {}

bool RepresentationXmlNode::AddVideoInfo(
    const RepeatedVideoInfo& repeated_video_info) {
  uint32 width = 0;
  uint32 height = 0;

  // Make sure that all the widths and heights match.
  for (int i = 0; i < repeated_video_info.size(); ++i) {
    const MediaInfo_VideoInfo& video_info = repeated_video_info.Get(i);
    if (video_info.width() <= 0 || video_info.height() <= 0)
      return false;

    if (width == 0) {
      width = video_info.width();
    } else if (width != video_info.width()) {
      return false;
    }

    if (height == 0) {
      height = video_info.height();
    } else  if (height != video_info.height()) {
      return false;
    }
  }

  if (width != 0)
    SetIntegerAttribute("width", width);

  if (height != 0)
    SetIntegerAttribute("height", height);

  return true;
}

bool RepresentationXmlNode::AddAudioInfo(
    const RepeatedAudioInfo& repeated_audio_info) {
  if (!AddAudioChannelInfo(repeated_audio_info))
    return false;

  AddAudioSamplingRateInfo(repeated_audio_info);

  // TODO(rkuroiwa): Find out where language goes.
  return true;
}

bool RepresentationXmlNode::AddVODOnlyInfo(const MediaInfo& media_info) {
  const bool need_segment_base = media_info.has_index_range() ||
                                 media_info.has_init_range() ||
                                 media_info.has_reference_time_scale();

  if (need_segment_base) {
    XmlNode segment_base("SegmentBase");
    if (media_info.has_index_range()) {
      segment_base.SetStringAttribute("indexRange",
                                      RangeToString(media_info.index_range()));
    }

    if (media_info.has_reference_time_scale()) {
      segment_base.SetIntegerAttribute("timescale",
                                       media_info.reference_time_scale());
    }

    if (media_info.has_init_range()) {
      XmlNode initialization("Initialization");
      initialization.SetStringAttribute("range",
                                        RangeToString(media_info.init_range()));

      if (!segment_base.AddChild(initialization.PassScopedPtr()))
        return false;
    }

    if (!AddChild(segment_base.PassScopedPtr()))
      return false;
  }

  if (media_info.has_media_file_name()) {
    XmlNode base_url("BaseURL");
    base_url.SetContent(media_info.media_file_name());

    if (!AddChild(base_url.PassScopedPtr()))
      return false;
  }

  if (media_info.has_media_duration_seconds()) {
    // Adding 'duration' attribute, so that this information can be used when
    // generating one MPD file. This should be removed from the final MPD.
    SetFloatingPointAttribute("duration", media_info.media_duration_seconds());
  }

  return true;
}

// Find all the unique number-of-channels in |repeated_audio_info|, and make
// AudioChannelConfiguration for each number-of-channels.
bool RepresentationXmlNode::AddAudioChannelInfo(
    const RepeatedAudioInfo& repeated_audio_info) {
  std::set<uint32> num_channels;
  for (int i = 0; i < repeated_audio_info.size(); ++i) {
    if (repeated_audio_info.Get(i).has_num_channels())
      num_channels.insert(repeated_audio_info.Get(i).num_channels());
  }

  std::set<uint32>::const_iterator num_channels_it = num_channels.begin();
  for (; num_channels_it != num_channels.end(); ++num_channels_it) {
    XmlNode audio_channel_config("AudioChannelConfiguration");

    const char kAudioChannelConfigScheme[] =
        "urn:mpeg:dash:23003:3:audio_channel_configuration:2011";
    audio_channel_config.SetStringAttribute("schemeIdUri",
                                            kAudioChannelConfigScheme);
    audio_channel_config.SetIntegerAttribute("value", *num_channels_it);

    if (!AddChild(audio_channel_config.PassScopedPtr()))
      return false;
  }

  return true;
}

// MPD expects one number for sampling frequency, or if it is a range it should
// be space separated.
void RepresentationXmlNode::AddAudioSamplingRateInfo(
    const RepeatedAudioInfo& repeated_audio_info) {
  bool has_sampling_frequency = false;
  uint32 min_sampling_frequency = UINT32_MAX;
  uint32 max_sampling_frequency = 0;

  for (int i = 0; i < repeated_audio_info.size(); ++i) {
    const MediaInfo_AudioInfo &audio_info = repeated_audio_info.Get(i);
    if (audio_info.has_sampling_frequency()) {
      has_sampling_frequency = true;
      const uint32 sampling_frequency = audio_info.sampling_frequency();
      if (sampling_frequency < min_sampling_frequency)
        min_sampling_frequency = sampling_frequency;

      if (sampling_frequency > max_sampling_frequency)
        max_sampling_frequency = sampling_frequency;
    }
  }

  if (has_sampling_frequency) {
    if (min_sampling_frequency == max_sampling_frequency) {
      SetIntegerAttribute("audioSamplingRate", min_sampling_frequency);
    } else {
      std::string sample_rate_string =
          base::UintToString(min_sampling_frequency) + " " +
          base::UintToString(max_sampling_frequency);
      SetStringAttribute("audioSamplingRate", sample_rate_string);
    }
  }
}

}  // namespace xml
}  // namespace dash_packager
