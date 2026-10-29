#include "core/DocCore.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/Cfb.h"
#include "core/Failure.h"
#include "core/Word.h"
#include "core/Xml.h"
#include "core/Zip.h"

namespace leandoc::core {
namespace {

using detail::Failure;
using detail::Ns;
using detail::NsScope;
using detail::XmlPart;
using detail::ZipEntry;
using detail::ZipReader;

constexpr char kDocxMain[] =
    "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml";
constexpr char kDotxMain[] =
    "application/vnd.openxmlformats-officedocument.wordprocessingml.template.main+xml";
constexpr char kDocmMain[] = "application/vnd.ms-word.document.macroenabled.main+xml";
constexpr char kDotmMain[] =
    "application/vnd.ms-word.template.macroenabledtemplate.main+xml";
constexpr char kOdtMime[] = "application/vnd.oasis.opendocument.text";
constexpr char kRelationshipsType[] =
    "application/vnd.openxmlformats-package.relationships+xml";
constexpr char kCorePropertiesType[] =
    "application/vnd.openxmlformats-package.core-properties+xml";
constexpr char kOfficeDocumentRelationship[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

FileKind KindFromMainType(std::string_view contentType) {
    const std::string lowered = detail::LowercaseAscii(contentType);
    if (lowered == kDocxMain) return FileKind::Docx;
    if (lowered == kDotxMain) return FileKind::Dotx;
    if (lowered == kDocmMain) return FileKind::Docm;
    if (lowered == kDotmMain) return FileKind::Dotm;
    return FileKind::NotADocument;
}

bool IsXmlContentType(std::string_view contentType) {
    const std::string lowered = detail::LowercaseAscii(contentType);
    return EndsWith(lowered, "+xml") || lowered == "application/xml" ||
           lowered == "text/xml";
}

bool IsAllowedImageType(std::string_view contentType) {
    const std::string lowered = detail::LowercaseAscii(contentType);
    static const char* const kAllowed[] = {
        "image/png", "image/jpeg", "image/jpg", "image/pjpeg", "image/gif",
        "image/bmp", "image/x-bmp", "image/x-ms-bmp", "image/tiff",
        "image/x-emf", "image/emf", "image/x-wmf", "image/wmf"};
    for (const char* allowed : kAllowed) {
        if (lowered == allowed) {
            return true;
        }
    }
    return false;
}

std::string Extension(std::string_view partName) {
    const std::size_t slash = partName.rfind('/');
    const std::size_t dot = partName.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    return detail::LowercaseAscii(partName.substr(dot + 1));
}

// [Content_Types].xml. Keys are lowercase part names without the leading
// slash, and lowercase extensions.
struct ContentTypes {
    std::unordered_map<std::string, std::string> defaults;
    std::unordered_map<std::string, std::string> overrides;
    std::string mainOverridePart;
    FileKind mainKind = FileKind::NotADocument;

    std::string TypeOf(std::string_view partName) const {
        const auto found = overrides.find(detail::LowercaseAscii(partName));
        if (found != overrides.end()) {
            return found->second;
        }
        const auto byExtension = defaults.find(Extension(partName));
        return byExtension == defaults.end() ? std::string() : byExtension->second;
    }

    bool HasType(std::string_view contentType) const {
        const std::string wanted = detail::LowercaseAscii(contentType);
        for (const auto& entry : defaults) {
            if (detail::LowercaseAscii(entry.second) == wanted) return true;
        }
        for (const auto& entry : overrides) {
            if (detail::LowercaseAscii(entry.second) == wanted) return true;
        }
        return false;
    }

    std::uint32_t CountType(std::string_view contentType) const {
        const std::string wanted = detail::LowercaseAscii(contentType);
        std::uint32_t count = 0;
        for (const auto& entry : overrides) {
            if (detail::LowercaseAscii(entry.second) == wanted) ++count;
        }
        return count;
    }
};

std::string StripLeadingSlash(std::string_view name) {
    while (!name.empty() && name.front() == '/') {
        name.remove_prefix(1);
    }
    return std::string(name);
}

bool ReadXml(
    ZipReader& zip,
    const ZipEntry& entry,
    const Limits& limits,
    XmlPart& part,
    Failure& failure) {
    return zip.Read(entry, part.source, failure) &&
           detail::ParseXmlPart(part, limits, failure);
}

bool ParseContentTypes(
    ZipReader& zip,
    const Limits& limits,
    ContentTypes& types,
    Failure& failure) {
    const ZipEntry* entry = zip.Find("[Content_Types].xml");
    if (entry == nullptr) {
        return true;
    }
    auto part = std::make_unique<XmlPart>();
    if (!ReadXml(zip, *entry, limits, *part, failure)) {
        return false;
    }
    NsScope scope;
    pugi::xml_node root = part->document.document_element();
    scope.Push(root);
    for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::size_t mark = scope.Mark();
        scope.Push(child);
        const detail::QName name = scope.Element(child);
        const std::string contentType = child.attribute("ContentType").value();
        if (name.Is(Ns::Ct, "Default")) {
            types.defaults.emplace(
                detail::LowercaseAscii(child.attribute("Extension").value()), contentType);
        } else if (name.Is(Ns::Ct, "Override")) {
            const std::string partName = StripLeadingSlash(child.attribute("PartName").value());
            types.overrides.emplace(detail::LowercaseAscii(partName), contentType);
            const FileKind kind = KindFromMainType(contentType);
            if (kind != FileKind::NotADocument && types.mainKind == FileKind::NotADocument) {
                types.mainKind = kind;
                types.mainOverridePart = partName;
            }
        }
        scope.Pop(mark);
    }
    return true;
}

// Classifies an opened ZIP: ODT by its mimetype entry, OOXML Word by the main
// document content type.
FileKind ClassifyZip(
    ZipReader& zip,
    const Limits& limits,
    ContentTypes& types,
    Failure& failure) {
    if (const ZipEntry* mimetype = zip.Find("mimetype")) {
        std::string value;
        if (mimetype->uncompressedSize <= 256 && zip.Read(*mimetype, value, failure)) {
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                                      value.back() == ' ')) {
                value.pop_back();
            }
            if (value == kOdtMime) {
                return FileKind::Odt;
            }
        }
        if (failure.Failed()) {
            return FileKind::NotADocument;
        }
    }
    if (!ParseContentTypes(zip, limits, types, failure)) {
        return FileKind::NotADocument;
    }
    return types.mainKind;
}

std::string PercentDecode(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size() &&
            std::isxdigit(static_cast<unsigned char>(value[index + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[index + 2]))) {
            const auto hex = [](char digit) {
                return digit <= '9' ? digit - '0' : (digit | 0x20) - 'a' + 10;
            };
            output += static_cast<char>(hex(value[index + 1]) * 16 + hex(value[index + 2]));
            index += 2;
        } else {
            output += value[index];
        }
    }
    return output;
}

// Resolves a relationship target against its source part, per OPC. Returns
// an empty string for a target that leaves the package.
std::string ResolvePartName(std::string_view source, std::string_view target) {
    const std::size_t cut = target.find_first_of("?#");
    if (cut != std::string_view::npos) {
        target = target.substr(0, cut);
    }
    std::string path;
    if (!target.empty() && target.front() == '/') {
        path = std::string(target.substr(1));
    } else {
        const std::size_t slash = source.rfind('/');
        path = slash == std::string_view::npos ? std::string()
                                               : std::string(source.substr(0, slash + 1));
        path.append(target);
    }
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        const std::string segment = path.substr(start, end - start);
        if (segment == "..") {
            if (segments.empty()) {
                return {};
            }
            segments.pop_back();
        } else if (!segment.empty() && segment != ".") {
            segments.push_back(segment);
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    std::string resolved;
    for (const std::string& segment : segments) {
        if (!resolved.empty()) {
            resolved += '/';
        }
        resolved += segment;
    }
    return resolved;
}

std::string RelsPathFor(std::string_view partName) {
    const std::size_t slash = partName.rfind('/');
    if (slash == std::string_view::npos) {
        return "_rels/" + std::string(partName) + ".rels";
    }
    return std::string(partName.substr(0, slash + 1)) + "_rels/" +
           std::string(partName.substr(slash + 1)) + ".rels";
}

std::string TypeTail(std::string_view type) {
    const std::size_t slash = type.rfind('/');
    return detail::LowercaseAscii(
        slash == std::string_view::npos ? type : type.substr(slash + 1));
}

struct Relationship {
    std::string id;
    std::string type;
    std::string tail;
    std::string target;
    bool external = false;
    std::string partName;  // resolved and present in the archive; else empty
};

enum class Role { Main, Story, Styles, Settings, FontTable, Other };

bool IsStructuralTail(const std::string& tail, Role& role) {
    if (tail == "header" || tail == "footer" || tail == "footnotes" || tail == "endnotes") {
        role = Role::Story;
        return true;
    }
    if (tail == "styles") {
        role = Role::Styles;
        return true;
    }
    if (tail == "settings") {
        role = Role::Settings;
        return true;
    }
    if (tail == "fonttable") {
        role = Role::FontTable;
        return true;
    }
    if (tail == "styleswith effects" || tail == "styleswitheffects" ||
        tail == "numbering" || tail == "theme" || tail == "websettings") {
        role = Role::Other;
        return true;
    }
    return false;
}

class PackageBuilder final {
public:
    PackageBuilder(
        ZipReader& zip,
        const ContentTypes& types,
        const Limits& limits,
        OpenResult& result,
        Failure& failure)
        : zip_(zip), types_(types), limits_(limits), result_(result), failure_(failure) {}

    bool Run() {
        if (!FindMainPart()) {
            return false;
        }
        std::vector<Relationship> mainRelationships;
        if (!LoadRelationships(mainPart_, mainRelationships)) {
            return false;
        }

        // Styles first: the outline needs them once the main part is done.
        for (int pass = 0; pass < 2; ++pass) {
            for (const Relationship& relationship : mainRelationships) {
                Role role = Role::Other;
                if (relationship.external || relationship.partName.empty() ||
                    !IsStructuralTail(relationship.tail, role) ||
                    (pass == 0) != (role == Role::Styles)) {
                    continue;
                }
                if (!IsXmlContentType(types_.TypeOf(relationship.partName))) {
                    continue;
                }
                if (!ProcessPart(relationship.partName, role)) {
                    return false;
                }
            }
        }
        if (!ProcessPart(mainPart_, Role::Main)) {
            return false;
        }
        if (!CountComments(mainRelationships) || !ReadCoreProperties()) {
            return false;
        }
        CountPackageFeatures();
        if (!CopyImages()) {
            return false;
        }
        WritePackage();
        return true;
    }

private:
    struct Output {
        std::string name;
        std::string contentType;
        std::string data;
    };

    bool FindMainPart() {
        std::vector<Relationship> root;
        if (!LoadRelationships("", root)) {
            return false;
        }
        for (const Relationship& relationship : root) {
            if (relationship.external || relationship.partName.empty()) {
                continue;
            }
            if (relationship.tail == "officedocument" && mainPart_.empty()) {
                if (KindFromMainType(types_.TypeOf(relationship.partName)) !=
                    FileKind::NotADocument) {
                    mainPart_ = relationship.partName;
                    mainRelationship_ = relationship;
                }
            } else if (relationship.tail == "core-properties" && corePart_.empty()) {
                corePart_ = relationship.partName;
                coreRelationship_ = relationship;
            }
        }
        if (mainPart_.empty() && !types_.mainOverridePart.empty() &&
            zip_.Find(types_.mainOverridePart) != nullptr) {
            mainPart_ = zip_.Find(types_.mainOverridePart)->name;
            mainRelationship_.id = "rId1";
            mainRelationship_.type = kOfficeDocumentRelationship;
            mainRelationship_.target = mainPart_;
        }
        if (mainPart_.empty()) {
            failure_.Set(
                "missing-main-part",
                "This file does not contain a Word document part.");
            return false;
        }
        result_.kind = KindFromMainType(types_.TypeOf(mainPart_));
        return true;
    }

    // Reads the relationships of `partName` ("" for the package). A part
    // without a relationships part has none.
    bool LoadRelationships(const std::string& partName, std::vector<Relationship>& output) {
        output.clear();
        const std::string relsName =
            partName.empty() ? std::string("_rels/.rels") : RelsPathFor(partName);
        const ZipEntry* entry = zip_.Find(relsName);
        if (entry == nullptr) {
            return true;
        }
        auto part = std::make_unique<XmlPart>();
        if (!ReadXml(zip_, *entry, limits_, *part, failure_)) {
            return false;
        }
        NsScope scope;
        pugi::xml_node root = part->document.document_element();
        scope.Push(root);
        std::unordered_set<std::string> seen;
        for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope.Mark();
            scope.Push(child);
            const detail::QName name = scope.Element(child);
            scope.Pop(mark);
            if (std::strcmp(name.local, "Relationship") != 0 ||
                (name.ns != Ns::Pr && name.ns != Ns::None)) {
                continue;
            }
            Relationship relationship;
            relationship.id = child.attribute("Id").value();
            relationship.type = child.attribute("Type").value();
            relationship.tail = TypeTail(relationship.type);
            relationship.target = child.attribute("Target").value();
            relationship.external =
                detail::LowercaseAscii(child.attribute("TargetMode").value()) == "external";
            if (relationship.id.empty() || !seen.insert(relationship.id).second) {
                continue;
            }
            if (!relationship.external) {
                std::string resolved =
                    ResolvePartName(partName, PercentDecode(relationship.target));
                const ZipEntry* target = resolved.empty() ? nullptr : zip_.Find(resolved);
                if (target == nullptr) {
                    resolved = ResolvePartName(partName, relationship.target);
                    target = resolved.empty() ? nullptr : zip_.Find(resolved);
                }
                // Package bookkeeping parts are never relationship targets.
                if (target != nullptr && !target->directory &&
                    target->key != "[content_types].xml" &&
                    !EndsWith(target->key, ".rels")) {
                    relationship.partName = target->name;
                }
            }
            output.push_back(std::move(relationship));
        }
        return true;
    }

    bool ProcessPart(const std::string& partName, Role role) {
        const std::string key = detail::LowercaseAscii(partName);
        if (!processed_.insert(key).second) {
            return true;
        }
        const ZipEntry* entry = zip_.Find(partName);
        if (entry == nullptr) {
            return true;
        }
        auto part = std::make_unique<XmlPart>();
        std::vector<Relationship> relationships;
        if (!ReadXml(zip_, *entry, limits_, *part, failure_) ||
            !LoadRelationships(partName, relationships)) {
            return false;
        }

        detail::ExternalIds externalIds;
        std::unordered_set<std::string> keptIds;
        std::unordered_set<std::string> structuralIds;
        for (const Relationship& relationship : relationships) {
            if (relationship.external) {
                externalIds.insert(relationship.id);
                if (relationship.tail == "attachedtemplate") {
                    if (role == Role::Settings) {
                        remoteTemplate_ = true;
                    }
                } else if (relationship.tail != "hyperlink" && relationship.tail != "image") {
                    tally_.Add(Feature::ExternalContent);
                }
            }
            if (relationship.tail == "image") {
                if (!relationship.external && !relationship.partName.empty() &&
                    IsAllowedImageType(types_.TypeOf(relationship.partName))) {
                    keptIds.insert(relationship.id);
                }
            } else if (relationship.tail == "hyperlink") {
                if (relationship.external && detail::IsAllowedLinkTarget(relationship.target)) {
                    keptIds.insert(relationship.id);
                }
            } else if (role == Role::Main) {
                Role childRole = Role::Other;
                if (!relationship.external && !relationship.partName.empty() &&
                    IsStructuralTail(relationship.tail, childRole) &&
                    processed_.count(detail::LowercaseAscii(relationship.partName)) > 0 &&
                    emitted_.count(detail::LowercaseAscii(relationship.partName)) > 0) {
                    keptIds.insert(relationship.id);
                    structuralIds.insert(relationship.id);
                }
            }
        }

        if (role == Role::Main || role == Role::Story) {
            detail::AnalyzeStory(part->document, externalIds, tally_);
        } else if (role == Role::FontTable) {
            for (const char* local : {"embedRegular", "embedBold", "embedItalic",
                                      "embedBoldItalic"}) {
                tally_.Add(
                    Feature::EmbeddedFonts,
                    detail::CountElements(part->document, Ns::W, local));
            }
        } else if (role == Role::Settings) {
            const std::string attached = detail::AttachedTemplateId(part->document);
            if (!attached.empty() && externalIds.count(attached) > 0) {
                remoteTemplate_ = true;
            }
        }

        std::unordered_set<std::string> usedIds;
        detail::SanitizePart(part->document, keptIds, usedIds);

        if (role == Role::Main) {
            const detail::StyleTable styles = detail::ReadStyles(styles_ ? &styles_->document : nullptr);
            detail::OutlineResult outline = detail::BuildOutline(part->document, styles);
            result_.outline = std::move(outline.entries);
            result_.properties.rightToLeft = outline.rightToLeft;
        }

        std::vector<const Relationship*> finalRelationships;
        for (const Relationship& relationship : relationships) {
            if (structuralIds.count(relationship.id) > 0 ||
                (keptIds.count(relationship.id) > 0 && usedIds.count(relationship.id) > 0)) {
                finalRelationships.push_back(&relationship);
                if (relationship.tail == "image") {
                    images_.push_back(relationship.partName);
                }
            }
        }

        Output output;
        output.name = entry->name;
        output.contentType = types_.TypeOf(entry->name);
        if (role == Role::Main) {
            if (result_.kind == FileKind::Docm) output.contentType = kDocxMain;
            if (result_.kind == FileKind::Dotm) output.contentType = kDotxMain;
        }
        output.data = detail::SaveXmlPart(part->document);
        Output rels;
        if (!finalRelationships.empty()) {
            rels.name = RelsPathFor(entry->name);
            rels.data = BuildRelationships(finalRelationships);
        }
        emitted_.insert(key);
        if (role == Role::Main) {
            mainOutput_ = std::move(output);
            mainRelsOutput_ = std::move(rels);
        } else {
            outputs_.push_back(std::move(output));
            if (!rels.name.empty()) {
                outputs_.push_back(std::move(rels));
            }
        }
        if (role == Role::Styles && !styles_) {
            styles_ = std::move(part);
        }
        return true;
    }

    static std::string BuildRelationships(const std::vector<const Relationship*>& items) {
        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
            "relationships\">";
        for (const Relationship* item : items) {
            xml += "<Relationship Id=\"";
            detail::AppendXmlEscaped(xml, item->id);
            xml += "\" Type=\"";
            detail::AppendXmlEscaped(xml, item->type);
            xml += "\" Target=\"";
            detail::AppendXmlEscaped(xml, item->target);
            xml += item->external ? "\" TargetMode=\"External\"/>" : "\"/>";
        }
        xml += "</Relationships>";
        return xml;
    }

    bool CountComments(const std::vector<Relationship>& mainRelationships) {
        for (const Relationship& relationship : mainRelationships) {
            if (relationship.tail != "comments" || relationship.external ||
                relationship.partName.empty()) {
                continue;
            }
            const ZipEntry* entry = zip_.Find(relationship.partName);
            auto part = std::make_unique<XmlPart>();
            if (!ReadXml(zip_, *entry, limits_, *part, failure_)) {
                return false;
            }
            tally_.Add(Feature::Comments, detail::CountElements(part->document, Ns::W, "comment"));
            return true;
        }
        tally_.Add(Feature::Comments, tally_.commentReferences);
        return true;
    }

    bool ReadCoreProperties() {
        if (corePart_.empty() || !IsXmlContentType(types_.TypeOf(corePart_))) {
            return true;
        }
        const ZipEntry* entry = zip_.Find(corePart_);
        auto part = std::make_unique<XmlPart>();
        if (!ReadXml(zip_, *entry, limits_, *part, failure_)) {
            return false;
        }
        NsScope scope;
        pugi::xml_node root = part->document.document_element();
        scope.Push(root);
        for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope.Mark();
            scope.Push(child);
            const detail::QName name = scope.Element(child);
            if (name.Is(Ns::Dc, "title") && result_.properties.title.empty()) {
                result_.properties.title = detail::CleanText(child.text().get(), 1024);
            } else if (name.Is(Ns::Dc, "creator") && result_.properties.creator.empty()) {
                result_.properties.creator = detail::CleanText(child.text().get(), 1024);
            }
            scope.Pop(mark);
        }
        std::unordered_set<std::string> none;
        std::unordered_set<std::string> used;
        detail::SanitizePart(part->document, none, used);
        Output output;
        output.name = entry->name;
        output.contentType = types_.TypeOf(entry->name);
        if (output.contentType.empty()) {
            output.contentType = kCorePropertiesType;
        }
        output.data = detail::SaveXmlPart(part->document);
        coreOutput_ = std::move(output);
        return true;
    }

    void CountPackageFeatures() {
        bool macros = result_.kind == FileKind::Docm || result_.kind == FileKind::Dotm ||
                      types_.HasType("application/vnd.ms-office.vbaProject");
        for (const ZipEntry& entry : zip_.Entries()) {
            macros = macros || EndsWith(entry.key, "vbaproject.bin");
        }
        if (macros) {
            tally_.Add(Feature::Macros);
        }
        if (remoteTemplate_) {
            tally_.Add(Feature::RemoteTemplate);
        }
        if (tally_.Get(Feature::ActiveX) == 0) {
            tally_.Add(Feature::ActiveX, types_.CountType("application/vnd.ms-office.activeX+xml"));
        }
        for (std::size_t index = 0; index < detail::kFeatureCount; ++index) {
            if (tally_.counts[index] > 0) {
                result_.features.push_back(
                    {static_cast<Feature>(index), tally_.counts[index]});
            }
        }
    }

    bool CopyImages() {
        std::unordered_set<std::string> copied;
        for (const std::string& name : images_) {
            const std::string key = detail::LowercaseAscii(name);
            if (!copied.insert(key).second || emitted_.count(key) > 0) {
                continue;
            }
            const ZipEntry* entry = zip_.Find(name);
            Output output;
            output.name = entry->name;
            output.contentType = types_.TypeOf(entry->name);
            if (!zip_.Read(*entry, output.data, failure_)) {
                return false;
            }
            imageOutputs_.push_back(std::move(output));
        }
        return true;
    }

    void WritePackage() {
        // Images use one Default per extension when every image with that
        // extension has the same type; anything else gets an Override.
        std::unordered_map<std::string, std::string> imageDefaults;
        std::unordered_set<std::string> conflicting;
        for (const Output& image : imageOutputs_) {
            const std::string extension = Extension(image.name);
            const auto inserted = imageDefaults.emplace(extension, image.contentType);
            if (!inserted.second && inserted.first->second != image.contentType) {
                conflicting.insert(extension);
            }
        }

        std::string types =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"";
        types += kRelationshipsType;
        types += "\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>";
        for (const auto& entry : imageDefaults) {
            if (entry.first.empty() || entry.first == "xml" || entry.first == "rels" ||
                conflicting.count(entry.first) > 0) {
                continue;
            }
            types += "<Default Extension=\"";
            detail::AppendXmlEscaped(types, entry.first);
            types += "\" ContentType=\"";
            detail::AppendXmlEscaped(types, entry.second);
            types += "\"/>";
        }
        const auto addOverride = [&types](const Output& output) {
            if (output.name.empty() || EndsWith(detail::LowercaseAscii(output.name), ".rels")) {
                return;
            }
            types += "<Override PartName=\"/";
            detail::AppendXmlEscaped(types, output.name);
            types += "\" ContentType=\"";
            detail::AppendXmlEscaped(
                types, output.contentType.empty() ? "application/xml" : output.contentType);
            types += "\"/>";
        };
        addOverride(mainOutput_);
        addOverride(coreOutput_);
        for (const Output& output : outputs_) {
            addOverride(output);
        }
        for (const Output& image : imageOutputs_) {
            const std::string extension = Extension(image.name);
            if (extension.empty() || extension == "xml" || extension == "rels" ||
                conflicting.count(extension) > 0) {
                addOverride(image);
            }
        }
        types += "</Types>";

        std::vector<const Relationship*> rootRelationships{&mainRelationship_};
        if (!coreOutput_.name.empty()) {
            rootRelationships.push_back(&coreRelationship_);
        }

        detail::StoredZipWriter writer;
        writer.Add("[Content_Types].xml", types);
        writer.Add("_rels/.rels", BuildRelationships(rootRelationships));
        writer.Add(mainOutput_.name, mainOutput_.data);
        if (!mainRelsOutput_.name.empty()) {
            writer.Add(mainRelsOutput_.name, mainRelsOutput_.data);
        }
        if (!coreOutput_.name.empty()) {
            writer.Add(coreOutput_.name, coreOutput_.data);
        }
        for (const Output& output : outputs_) {
            writer.Add(output.name, output.data);
        }
        for (const Output& image : imageOutputs_) {
            writer.Add(image.name, image.data);
        }
        result_.sanitizedPackage = writer.Finish();
    }

    ZipReader& zip_;
    const ContentTypes& types_;
    const Limits& limits_;
    OpenResult& result_;
    Failure& failure_;

    std::string mainPart_;
    Relationship mainRelationship_;
    std::string corePart_;
    Relationship coreRelationship_;
    detail::FeatureTally tally_;
    bool remoteTemplate_ = false;
    std::unique_ptr<XmlPart> styles_;
    std::unordered_set<std::string> processed_;
    std::unordered_set<std::string> emitted_;
    std::vector<std::string> images_;
    Output mainOutput_;
    Output mainRelsOutput_;
    Output coreOutput_;
    std::vector<Output> outputs_;
    std::vector<Output> imageOutputs_;
};

void AppendQuoted(std::string& output, std::string_view value) {
    output += '"';
    AppendJsonString(output, value);
    output += '"';
}

bool IsZipSignature(std::string_view bytes) {
    return StartsWith(bytes, std::string_view("PK\x03\x04", 4)) ||
           StartsWith(bytes, std::string_view("PK\x05\x06", 4));
}

bool IsRtfSignature(std::string_view bytes) {
    return StartsWith(bytes, "{\\rtf");
}

void FailNotADocument(Failure& failure) {
    failure.Set("not-a-zip", "This file is not a Word document.");
}

std::string MegabytesText(std::uint64_t bytes) {
    return std::to_string(bytes / (1024ull * 1024ull));
}

OpenResult OpenUnchecked(std::string_view bytes, const Limits& limits) {
    OpenResult result;
    Failure failure;
    result.sourceBytes = bytes.size();
    if (bytes.size() > limits.maximumInputBytes) {
        failure.Set(
            "too-large",
            "This file is larger than LeanDoc's " +
                MegabytesText(limits.maximumInputBytes) + " MB limit.");
    } else if (!IsZipSignature(bytes)) {
        if (detail::HasCompoundFileSignature(bytes)) {
            result.kind = detail::ClassifyCompoundFile(bytes);
        } else if (IsRtfSignature(bytes)) {
            result.kind = FileKind::Rtf;
        }
        if (result.kind == FileKind::NotADocument) {
            FailNotADocument(failure);
        }
    } else {
        ZipReader zip(limits);
        ContentTypes types;
        if (zip.Open(bytes, failure)) {
            const FileKind kind = ClassifyZip(zip, limits, types, failure);
            if (!failure.Failed()) {
                result.kind = kind;
                if (kind == FileKind::NotADocument) {
                    failure.Set(
                        "missing-main-part",
                        "This file does not contain a Word document part.");
                } else if (IsRenderableKind(kind) && zip.ValidateEntries(failure)) {
                    PackageBuilder builder(zip, types, limits, result, failure);
                    builder.Run();
                }
            }
        }
    }

    if (failure.Failed()) {
        result.ok = false;
        result.errorCode = std::move(failure.code);
        result.errorMessage = std::move(failure.message);
        result.sanitizedPackage.clear();
        result.features.clear();
        result.outline.clear();
        result.properties = {};
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace

bool IsRenderableKind(FileKind kind) {
    return kind == FileKind::Docx || kind == FileKind::Dotx || kind == FileKind::Docm ||
           kind == FileKind::Dotm;
}

const char* FileKindName(FileKind kind) {
    switch (kind) {
        case FileKind::Docx: return "docx";
        case FileKind::Dotx: return "dotx";
        case FileKind::Docm: return "docm";
        case FileKind::Dotm: return "dotm";
        case FileKind::LegacyDoc: return "legacy-doc";
        case FileKind::EncryptedOoxml: return "encrypted-ooxml";
        case FileKind::Rtf: return "rtf";
        case FileKind::Odt: return "odt";
        case FileKind::NotADocument: return "not-a-document";
    }
    return "not-a-document";
}

const char* FeatureCode(Feature feature) {
    switch (feature) {
        case Feature::TextBox: return "text-box";
        case Feature::Equation: return "equation";
        case Feature::Chart: return "chart";
        case Feature::SmartArt: return "smartart";
        case Feature::Shape: return "shape";
        case Feature::OleObject: return "ole-object";
        case Feature::ActiveX: return "activex";
        case Feature::AltChunk: return "alt-chunk";
        case Feature::TrackedChanges: return "tracked-changes";
        case Feature::Comments: return "comments";
        case Feature::Macros: return "macros";
        case Feature::RemoteTemplate: return "remote-template";
        case Feature::RemoteImage: return "remote-image";
        case Feature::ExternalContent: return "external-content";
        case Feature::DdeField: return "dde-field";
        case Feature::EmbeddedFonts: return "embedded-fonts";
    }
    return "";
}

// The first eight features are content the reader does not show. The core
// counts them only where docx-preview 0.4.1, the reader's layout engine, does
// not draw them, so "not displayed" in the fidelity bar is true for every
// item it counts. Checked against docx-preview 0.4.1's source and by
// rendering probe documents in Chromium (2026-10-20):
//
// - TextBox: docx-preview draws VML text boxes (v:textbox) as SVG
//   foreignObject with their paragraphs, but only inside a w:pict that is a
//   child of a run and only through v:shape, v:rect, v:oval, and v:line (a
//   v:group or v:roundrect drops its content). It draws nothing from
//   DrawingML (a:graphicData yields pictures only). It supports no
//   mc:Choice, so inside a run it shows mc:Fallback, whose first element
//   must be the w:pict. A DrawingML text box therefore displays when Word
//   wrote a VML fallback, and counts here only when it has none. Text that
//   overflows the box size Word recorded is clipped.
// - Equation: docx-preview renders OMML as MathML for f, func, rad, sSup,
//   sSub, sPre, d, nary, eqArr, limLow, m, box, bar, and groupChr, and only
//   for m:oMath or m:oMathPara directly in a paragraph (or in an inline
//   content control). An equation counts when it uses any other element
//   (m:acc, m:sSubSup, m:limUpp, m:borderBox, m:phant lose their content), sits
//   elsewhere (for example in a hyperlink), or lacks a property or argument
//   element whose absence makes docx-preview throw and abandon the whole
//   document (m:d without m:dPr, m:nary with one limit, and so on).
// - Shape: docx-preview draws v:rect, v:oval, and v:line, also from a VML
//   fallback. v:shape becomes an empty SVG group, and DrawingML shapes,
//   groups, and locked canvases are not drawn. Only the drawn VML forms
//   are left uncounted.
// - Chart, SmartArt: a:graphicData yields pictures only, so neither is drawn.
// - OleObject, ActiveX, AltChunk: removed from the sanitized package.
//   docx-preview would draw neither w:object nor w:control, and it would
//   load an altChunk into an iframe, which LeanDoc never allows.
//
// The tests in tests/core/CoreTests.cpp (TestDisplayedFeatures) and the
// fixtures features/not-displayed.docx and features/drawn-content.docx pin
// these rules. Recheck them whenever the pinned docx-preview changes.
bool FeatureIsNotDisplayed(Feature feature) {
    return static_cast<int>(feature) <= static_cast<int>(Feature::AltChunk);
}

FeatureLabel LabelFor(Feature feature) {
    switch (feature) {
        case Feature::TextBox: return {"text box", "text boxes"};
        case Feature::Equation: return {"equation", "equations"};
        case Feature::Chart: return {"chart", "charts"};
        case Feature::SmartArt: return {"SmartArt graphic", "SmartArt graphics"};
        case Feature::Shape: return {"shape", "shapes"};
        case Feature::OleObject: return {"embedded object", "embedded objects"};
        case Feature::ActiveX: return {"ActiveX control", "ActiveX controls"};
        case Feature::AltChunk: return {"imported content block", "imported content blocks"};
        case Feature::TrackedChanges: return {"tracked change", "tracked changes"};
        case Feature::Comments: return {"comment", "comments"};
        case Feature::Macros: return {"macro project", "macro projects"};
        case Feature::RemoteTemplate: return {"remote template", "remote templates"};
        case Feature::RemoteImage: return {"remote image", "remote images"};
        case Feature::ExternalContent: return {"external content link", "external content links"};
        case Feature::DdeField: return {"DDE field", "DDE fields"};
        case Feature::EmbeddedFonts: return {"embedded font", "embedded fonts"};
    }
    return {"", ""};
}

FileKind DetectKind(std::string_view bytes) {
    if (!IsZipSignature(bytes)) {
        if (detail::HasCompoundFileSignature(bytes)) {
            return detail::ClassifyCompoundFile(bytes);
        }
        return IsRtfSignature(bytes) ? FileKind::Rtf : FileKind::NotADocument;
    }
    try {
        const Limits limits;
        ZipReader zip(limits);
        ContentTypes types;
        Failure failure;
        if (!zip.Open(bytes, failure)) {
            return FileKind::NotADocument;
        }
        const FileKind kind = ClassifyZip(zip, limits, types, failure);
        return failure.Failed() ? FileKind::NotADocument : kind;
    } catch (const std::bad_alloc&) {
        return FileKind::NotADocument;
    }
}

OpenResult OpenDocumentBytes(std::string_view bytes, const Limits& limits) {
    try {
        return OpenUnchecked(bytes, limits);
    } catch (const std::bad_alloc&) {
        OpenResult result;
        result.sourceBytes = bytes.size();
        result.errorCode = "read-failed";
        result.errorMessage = "LeanDoc ran out of memory while reading this file.";
        return result;
    }
}

OpenResult OpenDocumentFile(const std::filesystem::path& path, const Limits& limits) {
    OpenResult result;
    const auto fail = [&result](const char* message) {
        result.ok = false;
        result.errorCode = "read-failed";
        result.errorMessage = message;
        return result;
    };

    std::error_code code;
    const auto status = std::filesystem::status(path, code);
    if (code || !std::filesystem::exists(status)) {
        return fail("LeanDoc could not find this file.");
    }
    if (std::filesystem::is_directory(status)) {
        return fail("That path is a folder, not a document.");
    }
    if (!std::filesystem::is_regular_file(status)) {
        return fail("LeanDoc can only read regular files.");
    }
    const auto size = std::filesystem::file_size(path, code);
    if (code) {
        return fail("LeanDoc could not measure this file.");
    }
    if (static_cast<std::uint64_t>(size) > limits.maximumInputBytes) {
        result.sourceBytes = static_cast<std::uint64_t>(size);
        result.errorCode = "too-large";
        result.errorMessage = "This file is larger than LeanDoc's " +
                              MegabytesText(limits.maximumInputBytes) + " MB limit.";
        return result;
    }

    std::string bytes;
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return fail("LeanDoc could not read this file.");
        }
        // Read at most one byte past the limit, in case the file grew.
        bytes.resize(static_cast<std::size_t>(size));
        if (!bytes.empty()) {
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            const auto bytesRead = input.gcount();
            if (bytesRead < 0 || (input.bad() && bytesRead == 0)) {
                return fail("LeanDoc could not finish reading this file.");
            }
            bytes.resize(static_cast<std::size_t>(bytesRead));
        }
    } catch (const std::bad_alloc&) {
        return fail("LeanDoc ran out of memory while reading this file.");
    }
    return OpenDocumentBytes(bytes, limits);
}

bool IsSupportedDocumentPath(const std::filesystem::path& path) {
    const std::string extension = detail::LowercaseAscii(path.extension().string());
    return extension == ".docx" || extension == ".dotx" || extension == ".docm" ||
           extension == ".dotm" || extension == ".doc" || extension == ".rtf" ||
           extension == ".odt";
}

std::string ReportToJson(const OpenResult& result) {
    std::string json;
    json.reserve(256 + result.outline.size() * 64);
    json += "{\"ok\":";
    json += result.ok ? "true" : "false";
    json += ",\"errorCode\":";
    AppendQuoted(json, result.errorCode);
    json += ",\"errorMessage\":";
    AppendQuoted(json, result.errorMessage);
    json += ",\"kind\":";
    AppendQuoted(json, FileKindName(result.kind));
    json += ",\"sourceBytes\":";
    json += std::to_string(result.sourceBytes);
    json += ",\"features\":[";
    bool first = true;
    for (const FeatureCount& feature : result.features) {
        if (!first) json += ',';
        first = false;
        const FeatureLabel label = LabelFor(feature.feature);
        json += "{\"code\":";
        AppendQuoted(json, FeatureCode(feature.feature));
        json += ",\"count\":";
        json += std::to_string(feature.count);
        json += ",\"notDisplayed\":";
        json += FeatureIsNotDisplayed(feature.feature) ? "true" : "false";
        json += ",\"singular\":";
        AppendQuoted(json, label.singular);
        json += ",\"plural\":";
        AppendQuoted(json, label.plural);
        json += '}';
    }
    json += "],\"outline\":[";
    first = true;
    for (const OutlineEntry& entry : result.outline) {
        if (!first) json += ',';
        first = false;
        json += "{\"level\":";
        json += std::to_string(entry.level);
        json += ",\"text\":";
        AppendQuoted(json, entry.text);
        json += ",\"paragraph\":";
        json += std::to_string(entry.paragraphIndex);
        json += '}';
    }
    json += "],\"properties\":{\"title\":";
    AppendQuoted(json, result.properties.title);
    json += ",\"creator\":";
    AppendQuoted(json, result.properties.creator);
    json += ",\"rightToLeft\":";
    json += result.properties.rightToLeft ? "true" : "false";
    json += "}}";
    return json;
}

bool IsPathWithin(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    std::error_code rootError;
    std::error_code candidateError;
    const auto canonicalRoot = std::filesystem::canonical(root, rootError);
    const auto canonicalCandidate = std::filesystem::canonical(candidate, candidateError);
    if (rootError || candidateError) {
        return false;
    }

    auto rootPart = canonicalRoot.begin();
    auto candidatePart = canonicalCandidate.begin();
    for (; rootPart != canonicalRoot.end(); ++rootPart, ++candidatePart) {
        if (candidatePart == canonicalCandidate.end() || *rootPart != *candidatePart) {
            return false;
        }
    }
    return true;
}

void AppendJsonString(std::string& output, std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    // Characters that need no escaping are copied a run at a time.
    std::size_t runStart = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto character = static_cast<unsigned char>(value[index]);
        char control[] = "\\u0000";
        const char* replacement = nullptr;
        std::size_t consumed = 1;
        switch (character) {
            case '"': replacement = "\\\""; break;
            case '\\': replacement = "\\\\"; break;
            case '\b': replacement = "\\b"; break;
            case '\f': replacement = "\\f"; break;
            case '\n': replacement = "\\n"; break;
            case '\r': replacement = "\\r"; break;
            case '\t': replacement = "\\t"; break;
            default:
                if (character < 0x20) {
                    control[4] = kHex[(character >> 4) & 0xF];
                    control[5] = kHex[character & 0xF];
                    replacement = control;
                } else if (
                    character == 0xE2 && index + 2 < value.size() &&
                    static_cast<unsigned char>(value[index + 1]) == 0x80 &&
                    (static_cast<unsigned char>(value[index + 2]) == 0xA8 ||
                     static_cast<unsigned char>(value[index + 2]) == 0xA9)) {
                    replacement = static_cast<unsigned char>(value[index + 2]) == 0xA8
                                      ? "\\u2028"
                                      : "\\u2029";
                    consumed = 3;
                }
                break;
        }
        if (replacement == nullptr) {
            continue;
        }
        output.append(value.data() + runStart, index - runStart);
        output += replacement;
        index += consumed - 1;
        runStart = index + 1;
    }
    output.append(value.data() + runStart, value.size() - runStart);
}

std::string EscapeJsonString(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 16);
    AppendJsonString(output, value);
    return output;
}

}  // namespace leandoc::core
