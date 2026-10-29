#pragma once

// Internal to the core: hardened XML parsing and namespace resolution.
// OOXML prefixes are conventional, not fixed, so every element and attribute
// is identified by its namespace URI and local name.

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "core/DocCore.h"
#include "core/Failure.h"
#include "pugixml.hpp"

namespace leandoc::core::detail {

// Namespaces the core reads. Transitional and Strict OOXML URIs map to the
// same value.
enum class Ns : unsigned char {
    None,   // no namespace (unprefixed attributes, or no default namespace)
    Other,  // a namespace the core does not need
    W,      // wordprocessingml main
    R,      // officeDocument relationships (r:id, r:embed, r:link, ...)
    Mc,     // markup compatibility
    Wps,    // Word 2010 shapes
    Wpg,    // Word 2010 groups
    Wpc,    // Word 2010 canvas
    Wp,     // drawingml wordprocessingDrawing
    A,      // drawingml main
    Pic,    // drawingml picture
    C,      // drawingml chart
    Cx,     // Office 2016 chartex
    Dgm,    // drawingml diagram (SmartArt)
    M,      // Office math
    V,      // VML
    O,      // VML office extensions
    W14,    // Word 2010 wordml extensions
    Asvg,   // Office 2016 SVG extension
    Lc,     // drawingml locked canvas
    Pr,     // package relationships
    Ct,     // package content types
    Cp,     // core properties
    Dc,     // Dublin Core elements
};

Ns NamespaceFromUri(std::string_view uri);

struct QName {
    Ns ns = Ns::None;
    const char* local = "";

    bool Is(Ns expected, const char* name) const {
        return ns == expected && std::strcmp(local, name) == 0;
    }
};

// A stack of xmlns bindings for a depth-first walk. Push() an element on the
// way down, Pop(mark) on the way up.
class NsScope final {
public:
    std::size_t Mark() const { return bindings_.size(); }
    void Push(pugi::xml_node element);
    void Pop(std::size_t mark) { bindings_.resize(mark); }

    QName Element(pugi::xml_node element) const;
    QName Attribute(pugi::xml_attribute attribute) const;

private:
    struct Binding {
        const char* prefix;
        std::size_t length;
        Ns ns;
    };
    Ns Lookup(const char* prefix, std::size_t length) const;
    std::vector<Binding> bindings_;
};

// One parsed XML part. The source bytes are parsed in place, so they live
// beside the document.
struct XmlPart {
    std::string source;
    pugi::xml_document document;
};

// Refuses DOCTYPE and ENTITY declarations before parsing, parses with
// whitespace kept, and bounds the element depth.
bool ParseXmlPart(XmlPart& part, const Limits& limits, Failure& failure);

// Serialises without indentation, as UTF-8, keeping the XML declaration.
std::string SaveXmlPart(pugi::xml_document& document);

// The value of the first attribute in `ns` with local name `local`.
pugi::xml_attribute FindAttribute(
    const NsScope& scope,
    pugi::xml_node element,
    Ns ns,
    const char* local);

// OOXML on/off values: absent val means on; "0", "false", "off" mean off.
bool IsOn(const char* value);

void AppendXmlEscaped(std::string& output, std::string_view value);

}  // namespace leandoc::core::detail
