#include "core/Xml.h"

#include <unordered_set>
#include <utility>

namespace leandoc::core::detail {
namespace {

struct UriEntry {
    const char* uri;
    Ns ns;
};

constexpr UriEntry kNamespaces[] = {
    {"http://schemas.openxmlformats.org/wordprocessingml/2006/main", Ns::W},
    {"http://purl.oclc.org/ooxml/wordprocessingml/main", Ns::W},
    {"http://schemas.openxmlformats.org/officeDocument/2006/relationships", Ns::R},
    {"http://purl.oclc.org/ooxml/officeDocument/relationships", Ns::R},
    {"http://schemas.openxmlformats.org/markup-compatibility/2006", Ns::Mc},
    {"http://schemas.microsoft.com/office/word/2010/wordprocessingShape", Ns::Wps},
    {"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup", Ns::Wpg},
    {"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas", Ns::Wpc},
    {"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing", Ns::Wp},
    {"http://purl.oclc.org/ooxml/drawingml/wordprocessingDrawing", Ns::Wp},
    {"http://schemas.openxmlformats.org/drawingml/2006/main", Ns::A},
    {"http://purl.oclc.org/ooxml/drawingml/main", Ns::A},
    {"http://schemas.openxmlformats.org/drawingml/2006/picture", Ns::Pic},
    {"http://purl.oclc.org/ooxml/drawingml/picture", Ns::Pic},
    {"http://schemas.openxmlformats.org/drawingml/2006/chart", Ns::C},
    {"http://purl.oclc.org/ooxml/drawingml/chart", Ns::C},
    {"http://schemas.microsoft.com/office/drawing/2014/chartex", Ns::Cx},
    {"http://schemas.openxmlformats.org/drawingml/2006/diagram", Ns::Dgm},
    {"http://purl.oclc.org/ooxml/drawingml/diagram", Ns::Dgm},
    {"http://schemas.openxmlformats.org/officeDocument/2006/math", Ns::M},
    {"http://purl.oclc.org/ooxml/officeDocument/math", Ns::M},
    {"urn:schemas-microsoft-com:vml", Ns::V},
    {"urn:schemas-microsoft-com:office:office", Ns::O},
    {"http://schemas.microsoft.com/office/word/2010/wordml", Ns::W14},
    {"http://schemas.microsoft.com/office/drawing/2016/SVG/main", Ns::Asvg},
    {"http://schemas.openxmlformats.org/drawingml/2006/lockedCanvas", Ns::Lc},
    {"http://purl.oclc.org/ooxml/drawingml/lockedCanvas", Ns::Lc},
    {"http://schemas.openxmlformats.org/package/2006/relationships", Ns::Pr},
    {"http://schemas.openxmlformats.org/package/2006/content-types", Ns::Ct},
    {"http://schemas.openxmlformats.org/package/2006/metadata/core-properties", Ns::Cp},
    {"http://purl.org/dc/elements/1.1/", Ns::Dc},
};

class StringWriter final : public pugi::xml_writer {
public:
    explicit StringWriter(std::string& output) : output_(output) {}
    void write(const void* data, std::size_t size) override {
        output_.append(static_cast<const char*>(data), size);
    }

private:
    std::string& output_;
};

bool StartsWithIgnoringCase(const char* text, std::size_t available, const char* word) {
    const std::size_t length = std::strlen(word);
    if (available < length) {
        return false;
    }
    for (std::size_t index = 0; index < length; ++index) {
        char character = text[index];
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        if (character != word[index]) {
            return false;
        }
    }
    return true;
}

// Byte scan for "<!DOCTYPE" and "<!ENTITY" in any ASCII-compatible encoding.
// UTF-16 parts are caught after parsing by the doctype node check.
bool ContainsDtdMarkup(std::string_view source) {
    std::size_t position = 0;
    while ((position = source.find("<!", position)) != std::string_view::npos) {
        const char* rest = source.data() + position + 2;
        const std::size_t available = source.size() - position - 2;
        if (StartsWithIgnoringCase(rest, available, "DOCTYPE") ||
            StartsWithIgnoringCase(rest, available, "ENTITY")) {
            return true;
        }
        position += 2;
    }
    return false;
}

void FailDoctype(Failure& failure) {
    failure.Set(
        "xml-doctype",
        "This document contains a DTD, which LeanDoc refuses to read.");
}

// Duplicate attributes make XML ill-formed; pugixml accepts them, browsers
// and LibreOffice do not.
bool HasDuplicateAttributes(pugi::xml_node element) {
    std::size_t count = 0;
    for (pugi::xml_attribute attribute = element.first_attribute(); attribute;
         attribute = attribute.next_attribute()) {
        ++count;
    }
    if (count < 2) {
        return false;
    }
    if (count <= 32) {
        for (pugi::xml_attribute left = element.first_attribute(); left;
             left = left.next_attribute()) {
            for (pugi::xml_attribute right = left.next_attribute(); right;
                 right = right.next_attribute()) {
                if (std::strcmp(left.name(), right.name()) == 0) {
                    return true;
                }
            }
        }
        return false;
    }
    std::unordered_set<std::string_view> names;
    names.reserve(count);
    for (pugi::xml_attribute attribute = element.first_attribute(); attribute;
         attribute = attribute.next_attribute()) {
        if (!names.insert(attribute.name()).second) {
            return true;
        }
    }
    return false;
}

enum class Structure { Fine, TooDeep, DuplicateAttribute };

// Counts element nesting only; text inside the deepest element is free.
// Iterative so that a deep document cannot exhaust the stack here.
Structure CheckStructure(const pugi::xml_document& document, std::uint32_t maximum) {
    pugi::xml_node node = document.first_child();
    std::uint32_t depth = node ? 1 : 0;
    while (node) {
        if (node.type() == pugi::node_element) {
            if (depth > maximum) {
                return Structure::TooDeep;
            }
            if (HasDuplicateAttributes(node)) {
                return Structure::DuplicateAttribute;
            }
        }
        if (node.first_child()) {
            node = node.first_child();
            ++depth;
            continue;
        }
        while (node && !node.next_sibling()) {
            node = node.parent();
            --depth;
            if (node == document) {
                return Structure::Fine;
            }
        }
        if (node) {
            node = node.next_sibling();
        }
    }
    return Structure::Fine;
}

}  // namespace

Ns NamespaceFromUri(std::string_view uri) {
    for (const UriEntry& entry : kNamespaces) {
        if (uri == entry.uri) {
            return entry.ns;
        }
    }
    return Ns::Other;
}

void NsScope::Push(pugi::xml_node element) {
    for (pugi::xml_attribute attribute = element.first_attribute(); attribute;
         attribute = attribute.next_attribute()) {
        const char* name = attribute.name();
        if (std::strncmp(name, "xmlns", 5) != 0) {
            continue;
        }
        if (name[5] == '\0') {
            bindings_.push_back({"", 0, NamespaceFromUri(attribute.value())});
        } else if (name[5] == ':') {
            const std::size_t length = std::strlen(name + 6);
            const Ns ns = attribute.value()[0] == '\0'
                              ? Ns::None
                              : NamespaceFromUri(attribute.value());
            bindings_.push_back({name + 6, length, ns});
        }
    }
}

Ns NsScope::Lookup(const char* prefix, std::size_t length) const {
    for (auto binding = bindings_.rbegin(); binding != bindings_.rend(); ++binding) {
        if (binding->length == length &&
            std::memcmp(binding->prefix, prefix, length) == 0) {
            return binding->ns;
        }
    }
    if (length == 3 && std::memcmp(prefix, "xml", 3) == 0) {
        return Ns::Other;
    }
    return length == 0 ? Ns::None : Ns::Other;
}

QName NsScope::Element(pugi::xml_node element) const {
    const char* name = element.name();
    const char* colon = std::strchr(name, ':');
    if (colon == nullptr) {
        return {Lookup("", 0), name};
    }
    return {Lookup(name, static_cast<std::size_t>(colon - name)), colon + 1};
}

QName NsScope::Attribute(pugi::xml_attribute attribute) const {
    const char* name = attribute.name();
    const char* colon = std::strchr(name, ':');
    if (colon == nullptr) {
        return {Ns::None, name};
    }
    return {Lookup(name, static_cast<std::size_t>(colon - name)), colon + 1};
}

bool ParseXmlPart(XmlPart& part, const Limits& limits, Failure& failure) {
    if (ContainsDtdMarkup(part.source)) {
        FailDoctype(failure);
        return false;
    }
    // parse_doctype keeps any DOCTYPE as a node so the check below sees one
    // that the byte scan could not (UTF-16 parts). pugixml never expands
    // custom entities or loads external resources.
    constexpr unsigned kOptions = pugi::parse_cdata | pugi::parse_escapes |
                                  pugi::parse_wconv_attribute | pugi::parse_eol |
                                  pugi::parse_ws_pcdata | pugi::parse_declaration |
                                  pugi::parse_doctype;
    const pugi::xml_parse_result parsed = part.document.load_buffer_inplace(
        part.source.data(), part.source.size(), kOptions, pugi::encoding_auto);
    if (!parsed) {
        if (parsed.status == pugi::status_bad_doctype) {
            FailDoctype(failure);
        } else {
            FailMalformedXml(failure);
        }
        return false;
    }
    for (pugi::xml_node child = part.document.first_child(); child;
         child = child.next_sibling()) {
        if (child.type() == pugi::node_doctype) {
            FailDoctype(failure);
            return false;
        }
    }
    if (!part.document.document_element()) {
        FailMalformedXml(failure);
        return false;
    }
    switch (CheckStructure(part.document, limits.maximumXmlDepth)) {
        case Structure::Fine:
            break;
        case Structure::TooDeep:
            failure.Set(
                "xml-too-deep",
                "This document's XML is nested more deeply than LeanDoc allows.");
            return false;
        case Structure::DuplicateAttribute:
            FailMalformedXml(failure);
            return false;
    }
    return true;
}

std::string SaveXmlPart(pugi::xml_document& document) {
    pugi::xml_node declaration = document.first_child();
    if (declaration.type() == pugi::node_declaration) {
        // The output is always UTF-8, whatever the source encoding was.
        if (pugi::xml_attribute encoding = declaration.attribute("encoding")) {
            encoding.set_value("UTF-8");
        }
    }
    std::string output;
    StringWriter writer(output);
    document.save(writer, "", pugi::format_raw, pugi::encoding_utf8);
    return output;
}

pugi::xml_attribute FindAttribute(
    const NsScope& scope,
    pugi::xml_node element,
    Ns ns,
    const char* local) {
    for (pugi::xml_attribute attribute = element.first_attribute(); attribute;
         attribute = attribute.next_attribute()) {
        if (scope.Attribute(attribute).Is(ns, local)) {
            return attribute;
        }
    }
    return {};
}

bool IsOn(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return true;
    }
    return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0 &&
           std::strcmp(value, "off") != 0;
}

void AppendXmlEscaped(std::string& output, std::string_view value) {
    for (const char character : value) {
        switch (character) {
            case '&':
                output += "&amp;";
                break;
            case '<':
                output += "&lt;";
                break;
            case '>':
                output += "&gt;";
                break;
            case '"':
                output += "&quot;";
                break;
            case '\'':
                output += "&apos;";
                break;
            default:
                output += character;
                break;
        }
    }
}

}  // namespace leandoc::core::detail
