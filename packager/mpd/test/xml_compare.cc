#include "packager/mpd/test/xml_compare.h"

#include <libxml/tree.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "packager/base/logging.h"
#include "packager/mpd/base/xml/scoped_xml_ptr.h"

namespace edash_packager {

namespace {
xml::ScopedXmlPtr<xmlDoc>::type GetDocFromString(const std::string& xml_str) {
  xml::ScopedXmlPtr<xmlDoc>::type schema_as_doc(
      xmlReadMemory(xml_str.data(), xml_str.size(), NULL, NULL, 0));

  return schema_as_doc.Pass();
}

// Make a map from attributes of the node.
std::map<std::string, std::string> GetMapOfAttributes(xmlNodePtr node) {
  DVLOG(2) << "Getting attributes for node "
           << reinterpret_cast<const char*>(node->name);
  std::map<std::string, std::string> attribute_map;
  for (xmlAttr* attribute = node->properties;
       attribute && attribute->name && attribute->children;
       attribute = attribute->next) {
    const char* name = reinterpret_cast<const char*>(attribute->name);
    xml::ScopedXmlPtr<xmlChar>::type value(
        xmlNodeListGetString(node->doc, attribute->children, 1));

    attribute_map[name] = reinterpret_cast<const char*>(value.get());
    DVLOG(3) << "In node " << reinterpret_cast<const char*>(node->name)
             << " found attribute " << name << " with value "
             << reinterpret_cast<const char*>(value.get());
  }

  return attribute_map;
}

bool MapCompareFunc(std::pair<std::string, std::string> a,
                    std::pair<std::string, std::string> b) {
  if (a.first != b.first) {
    DLOG(INFO) << "Attribute mismatch " << a.first << " and " << b.first;
    return false;
  }

  if (a.second != b.second) {
    DLOG(INFO) << "Value mismatch for " << a.first << std::endl << "Values are "
               << a.second << " and " << b.second;
    return false;
  }
  return true;
}

bool MapsEqual(const std::map<std::string, std::string>& map1,
               const std::map<std::string, std::string>& map2) {
  return map1.size() == map2.size() &&
         std::equal(map1.begin(), map1.end(), map2.begin(), MapCompareFunc);
}

// Return true if the nodes have the same attributes.
bool CompareAttributes(xmlNodePtr node1, xmlNodePtr node2) {
  return MapsEqual(GetMapOfAttributes(node1), GetMapOfAttributes(node2));
}

// Return true if the name of the nodes match.
bool CompareNames(xmlNodePtr node1, xmlNodePtr node2) {
  DVLOG(2) << "Comparing " << reinterpret_cast<const char*>(node1->name)
           << " and " << reinterpret_cast<const char*>(node2->name);
  return xmlStrcmp(node1->name, node2->name) == 0;
}

// Recursively check the elements.
// Note that the terminating condition of the recursion is when the children do
// not match (inside the loop).
bool CompareNodes(xmlNodePtr node1, xmlNodePtr node2) {
  DCHECK(node1 && node2);

  if (!CompareNames(node1, node2)) {
    LOG(ERROR) << "Names of the nodes do not match: "
               << reinterpret_cast<const char*>(node1->name) << " "
               << reinterpret_cast<const char*>(node2->name);
    return false;
  }

  if (!CompareAttributes(node1, node2)) {
    LOG(ERROR) << "Attributes of element "
               << reinterpret_cast<const char*>(node1->name)
               << " do not match.";
    return false;
  }

  xmlNodePtr node1_child = xmlFirstElementChild(node1);
  xmlNodePtr node2_child = xmlFirstElementChild(node2);
  do {
    if (!node1_child || !node2_child)
      return node1_child == node2_child;

    DCHECK(node1_child && node2_child);
    if (!CompareNodes(node1_child, node2_child))
      return false;

    node1_child = xmlNextElementSibling(node1_child);
    node2_child = xmlNextElementSibling(node2_child);
  } while (node1_child || node2_child);

  // Both nodes have equivalent descendents.
  return true;
}
}  // namespace

bool XmlEqual(const std::string& xml1, const std::string& xml2) {
  xml::ScopedXmlPtr<xmlDoc>::type xml1_doc(GetDocFromString(xml1));
  xml::ScopedXmlPtr<xmlDoc>::type xml2_doc(GetDocFromString(xml2));
  return XmlEqual(xml1_doc.get(), xml2_doc.get());
}

bool XmlEqual(const std::string& xml1, xmlDocPtr xml2) {
  xml::ScopedXmlPtr<xmlDoc>::type xml1_doc(GetDocFromString(xml1));
  return XmlEqual(xml1_doc.get(), xml2);
}

bool XmlEqual(xmlDocPtr xml1, xmlDocPtr xml2) {
  if (!xml1|| !xml2) {
    LOG(ERROR) << "xml1 and/or xml2 are not valid XML.";
    return false;
  }

  xmlNodePtr xml1_root_element = xmlDocGetRootElement(xml1);
  xmlNodePtr xml2_root_element = xmlDocGetRootElement(xml2);
  if (!xml1_root_element || !xml2_root_element)
    return xml1_root_element == xml2_root_element;

  return CompareNodes(xml1_root_element, xml2_root_element);
}

}  // namespace edash_packager
