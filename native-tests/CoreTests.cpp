// LeanDoc core tests. Plain checks, no framework: every failed check prints a
// line and the process exits 1. The first argument is tests/fixtures.

#include "core/DocCore.h"
#include "core/Links.h"
#include "core/Zip.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "TestZip.h"
#include "miniz.h"
#include "pugixml.hpp"

namespace {

namespace core = leandoc::core;
namespace fs = std::filesystem;
using leandoc::tests::BuildZip;
using leandoc::tests::DocumentXml;
using leandoc::tests::MinimalDocx;
using leandoc::tests::TestEntry;

int failures = 0;
int checks = 0;

void Expect(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void ExpectEqual(const std::string& actual, const std::string& expected, const std::string& message) {
    Expect(actual == expected,
           message + "\n  expected: " + expected + "\n  actual:   " + actual);
}

struct ExpectedFixture {
    const char* path;
    const char* kind;
    bool ok;
    const char* errorCode;
    const char* features;
    int rightToLeft;
    const char* outline;
    const char* text;
};

const ExpectedFixture kExpectedFixtures[] = {
#include "expected.inc"
};

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

// ---------------------------------------------------------------------------
// Reading a sanitized package back

std::map<std::string, std::string> Unzip(const std::string& bytes) {
    std::map<std::string, std::string> parts;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        return parts;
    }
    for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&zip); ++index) {
        mz_zip_archive_file_stat stat{};
        mz_zip_reader_file_stat(&zip, index, &stat);
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, index, &size, 0);
        parts[stat.m_filename] = data ? std::string(static_cast<const char*>(data), size) : "";
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return parts;
}

const char* LocalName(const pugi::xml_node& node) {
    const char* colon = std::strchr(node.name(), ':');
    return colon ? colon + 1 : node.name();
}

std::string Collapse(const std::string& text) {
    std::string output;
    bool space = false;
    for (const char character : text) {
        if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
            space = !output.empty();
            continue;
        }
        if (space) {
            output += ' ';
            space = false;
        }
        output += character;
    }
    return output;
}

void CollectRunText(const pugi::xml_node& node, std::string& text) {
    for (pugi::xml_node child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string local = LocalName(child);
        if (local == "t") {
            text += child.text().get();
        } else if (local == "tab" || local == "br" || local == "cr") {
            text += ' ';
        } else if (local != "txbxContent" && local != "Fallback" && local != "p" &&
                   local != "instrText" && local != "oMath" && local != "oMathPara") {
            CollectRunText(child, text);
        }
    }
}

void CollectParagraphs(const pugi::xml_node& node, std::vector<std::string>& paragraphs) {
    for (pugi::xml_node child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string local = LocalName(child);
        if (local == "p") {
            std::string text;
            CollectRunText(child, text);
            paragraphs.push_back(Collapse(text));
        } else if (local != "txbxContent" && local != "Fallback") {
            CollectParagraphs(child, paragraphs);
        }
    }
}

std::string BodyText(const std::string& documentXml) {
    pugi::xml_document document;
    document.load_buffer(documentXml.data(), documentXml.size(),
                         pugi::parse_default | pugi::parse_ws_pcdata);
    std::vector<std::string> paragraphs;
    for (pugi::xml_node child : document.document_element().children()) {
        if (std::strcmp(LocalName(child), "body") == 0) {
            CollectParagraphs(child, paragraphs);
        }
    }
    std::string joined;
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        if (index > 0) joined += '\n';
        joined += paragraphs[index];
    }
    return joined;
}

std::string MainPartName(const std::map<std::string, std::string>& parts) {
    const auto rels = parts.find("_rels/.rels");
    if (rels == parts.end()) {
        return {};
    }
    pugi::xml_document document;
    document.load_string(rels->second.c_str());
    for (pugi::xml_node relationship : document.document_element().children()) {
        const std::string type = relationship.attribute("Type").value();
        if (type.size() >= 14 && type.compare(type.size() - 14, 14, "officeDocument") == 0) {
            std::string target = relationship.attribute("Target").value();
            if (!target.empty() && target[0] == '/') target.erase(0, 1);
            return target;
        }
    }
    return {};
}

std::string PackageBodyText(const std::string& package) {
    const auto parts = Unzip(package);
    const auto main = parts.find(MainPartName(parts));
    return main == parts.end() ? std::string("<no main part>") : BodyText(main->second);
}

std::string FeaturesText(const core::OpenResult& result) {
    std::vector<std::string> items;
    for (const core::FeatureCount& feature : result.features) {
        items.push_back(std::string(core::FeatureCode(feature.feature)) + "=" +
                        std::to_string(feature.count));
    }
    std::sort(items.begin(), items.end());
    std::string joined;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index > 0) joined += ';';
        joined += items[index];
    }
    return joined;
}

std::string OutlineText(const core::OpenResult& result) {
    std::string joined;
    for (std::size_t index = 0; index < result.outline.size(); ++index) {
        const core::OutlineEntry& entry = result.outline[index];
        if (index > 0) joined += '\n';
        joined += std::to_string(entry.level) + "|" + std::to_string(entry.paragraphIndex) +
                  "|" + entry.text;
    }
    return joined;
}

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// Every part of a sanitized package is checked for anything SPEC 3.5 removes.
void ExpectNothingForbidden(const std::string& package, const std::string& label) {
    const auto parts = Unzip(package);
    Expect(!parts.empty(), label + ": sanitized package should unzip");
    for (const auto& [name, data] : parts) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const std::string where = label + " " + name;
        Expect(!Contains(lower, "vbaproject") && !Contains(lower, "activex") &&
                   !Contains(lower, "embeddings/") && !Contains(lower, ".htm") &&
                   !Contains(lower, ".svg") && !Contains(lower, "comments") &&
                   !Contains(lower, "fonts/") && !Contains(lower, "customxml") &&
                   !Contains(lower, "charts/") && !Contains(lower, "diagrams/") &&
                   !Contains(lower, ".bin"),
               where + ": part must not be in a sanitized package");
        const bool isXml = Contains(lower, ".xml") || Contains(lower, ".rels");
        if (!isXml) {
            continue;
        }
        if (Contains(lower, ".rels")) {
            pugi::xml_document rels;
            rels.load_buffer(data.data(), data.size());
            for (pugi::xml_node relationship : rels.document_element().children()) {
                const std::string mode = relationship.attribute("TargetMode").value();
                if (mode != "External") {
                    continue;
                }
                const std::string type = relationship.attribute("Type").value();
                const std::string target = relationship.attribute("Target").value();
                const bool hyperlink = type.size() >= 9 &&
                                       type.compare(type.size() - 9, 9, "hyperlink") == 0;
                const bool allowed = target.rfind("https:", 0) == 0 ||
                                     target.rfind("http:", 0) == 0 ||
                                     target.rfind("mailto:", 0) == 0;
                Expect(hyperlink && allowed, where + ": external relationship " + target);
            }
        }
        for (const char* banned :
             {"attachedTemplate", "r:link=", "<w:altChunk", "<w:object", "w:control",
              "vbaProject", "INCLUDEPICTURE", "INCLUDETEXT", "DDEAUTO", "instrText xml:space=\"preserve\"> DDE",
              "commentReference", "commentRangeStart", "<w:del ", "<w:ins ", "<w:moveFrom ",
              "<w:moveTo ", "rPrChange", "pPrChange", "embedRegular", "embedBold",
              "svgBlip", "OLEObject", "javascript:", "file:///", "<!DOCTYPE"}) {
            Expect(!Contains(data, banned), where + ": contains " + banned);
        }
    }
}

// ---------------------------------------------------------------------------
// Fixture expectations

void TestFixtures(const fs::path& fixtures) {
    for (const ExpectedFixture& expected : kExpectedFixtures) {
        const fs::path path = fixtures / expected.path;
        const std::string label = expected.path;
        const core::OpenResult result = core::OpenDocumentFile(path);
        ExpectEqual(core::FileKindName(result.kind), expected.kind, label + ": kind");
        Expect(result.ok == expected.ok, label + ": ok");
        ExpectEqual(result.errorCode, expected.errorCode, label + ": error code");
        ExpectEqual(FeaturesText(result), expected.features, label + ": features");
        Expect(result.ok == result.errorMessage.empty(),
               label + ": an error message exactly when not ok");
        Expect(result.errorMessage.find("\xE2\x80\x94") == std::string::npos,
               label + ": no em dash in the message");

        const std::string bytes = ReadFile(path);
        Expect(result.sourceBytes == bytes.size(), label + ": sourceBytes");
        ExpectEqual(core::FileKindName(core::DetectKind(bytes)), expected.kind,
                    label + ": DetectKind");
        const core::OpenResult fromBytes = core::OpenDocumentBytes(bytes);
        Expect(fromBytes.sanitizedPackage == result.sanitizedPackage &&
                   core::ReportToJson(fromBytes) == core::ReportToJson(result),
               label + ": file and bytes give the same result");

        if (expected.rightToLeft >= 0) {
            Expect(result.properties.rightToLeft == (expected.rightToLeft == 1),
                   label + ": rightToLeft");
        }
        if (expected.outline != nullptr) {
            ExpectEqual(OutlineText(result), expected.outline, label + ": outline");
        }

        const bool renderable = result.ok && core::IsRenderableKind(result.kind);
        Expect(renderable == !result.sanitizedPackage.empty(),
               label + ": a package exactly when ok and renderable");
        if (!renderable) {
            continue;
        }
        if (expected.text != nullptr) {
            ExpectEqual(PackageBodyText(result.sanitizedPackage), expected.text,
                        label + ": sanitized text");
        }
        ExpectNothingForbidden(result.sanitizedPackage, label);

        // The sanitized package reopens cleanly with the same text and outline.
        const core::OpenResult again = core::OpenDocumentBytes(result.sanitizedPackage);
        Expect(again.ok, label + ": sanitized package reopens");
        const core::FileKind plainKind =
            result.kind == core::FileKind::Docm   ? core::FileKind::Docx
            : result.kind == core::FileKind::Dotm ? core::FileKind::Dotx
                                                  : result.kind;
        Expect(again.kind == plainKind, label + ": sanitized kind is not macro-enabled");
        ExpectEqual(OutlineText(again), OutlineText(result), label + ": outline after reopen");
        ExpectEqual(PackageBodyText(again.sanitizedPackage),
                    PackageBodyText(result.sanitizedPackage), label + ": text after reopen");
        Expect(again.properties.title == result.properties.title,
               label + ": title after reopen");
        for (const core::FeatureCount& feature : again.features) {
            const bool allowed = feature.feature == core::Feature::TextBox ||
                                 feature.feature == core::Feature::Equation ||
                                 feature.feature == core::Feature::Shape;
            Expect(allowed, label + ": sanitized package still has " +
                                core::FeatureCode(feature.feature));
        }
    }
}

// ---------------------------------------------------------------------------
// Limits and ZIP structure

core::OpenResult OpenEntries(const std::vector<TestEntry>& entries, const core::Limits& limits = {}) {
    return core::OpenDocumentBytes(BuildZip(entries), limits);
}

void TestLimits() {
    const auto basic = MinimalDocx("<w:p><w:r><w:t>Hello</w:t></w:r></w:p>");
    const core::OpenResult ok = OpenEntries(basic);
    Expect(ok.ok && ok.kind == core::FileKind::Docx, "the minimal test package opens");
    ExpectEqual(PackageBodyText(ok.sanitizedPackage), "Hello", "minimal package text");

    core::Limits tiny;
    tiny.maximumInputBytes = 10;
    const core::OpenResult tooLarge = OpenEntries(basic, tiny);
    ExpectEqual(tooLarge.errorCode, "too-large", "input over the limit");
    Expect(tooLarge.errorMessage.find("larger than") != std::string::npos,
           "too-large message names the limit");

    auto many = basic;
    for (int index = 0; index < 4097; ++index) {
        many.push_back({"x/" + std::to_string(index), ""});
    }
    ExpectEqual(OpenEntries(many).errorCode, "too-many-entries", "4,101 entries");
    core::Limits fewEntries;
    fewEntries.maximumEntries = 4;
    ExpectEqual(OpenEntries(basic, fewEntries).errorCode, "", "exactly at the entry limit");
    fewEntries.maximumEntries = 3;
    ExpectEqual(OpenEntries(basic, fewEntries).errorCode, "too-many-entries",
                "one past the entry limit");

    // The stream inflates past what the entry declares.
    auto lying = basic;
    lying[2].declaredSize = 100;
    ExpectEqual(OpenEntries(lying).errorCode, "part-too-large", "inflates past declared size");
    auto shortStream = basic;
    shortStream[2].declaredSize = static_cast<long long>(shortStream[2].data.size() + 50);
    ExpectEqual(OpenEntries(shortStream).errorCode, "corrupt-zip", "inflates short of declared size");

    core::Limits smallParts;
    smallParts.maximumPartBytes = 200;
    ExpectEqual(OpenEntries(basic, smallParts).errorCode, "part-too-large", "part over the cap");

    core::Limits smallTotal;
    smallTotal.maximumTotalUncompressedBytes = 900;
    ExpectEqual(OpenEntries(basic, smallTotal).errorCode, "part-too-large", "total over the cap");

    auto ratio = MinimalDocx("<w:p/>" + std::string(2 * 1024 * 1024, ' '));
    ExpectEqual(OpenEntries(ratio).errorCode, "compression-ratio", "2 MiB of spaces");
    core::Limits looseRatio;
    looseRatio.maximumCompressionRatio = 100000;
    Expect(OpenEntries(ratio, looseRatio).ok, "the same part passes a loose ratio");

    auto badCrc = basic;
    badCrc[2].crc32 = 12345;
    ExpectEqual(OpenEntries(badCrc).errorCode, "corrupt-zip", "CRC mismatch");

    auto storedMismatch = basic;
    storedMismatch[2].declaredSize = static_cast<long long>(storedMismatch[2].data.size() + 1);
    storedMismatch[2].deflate = false;
    ExpectEqual(OpenEntries(storedMismatch).errorCode, "corrupt-zip", "stored size mismatch");

    auto encrypted = basic;
    encrypted.push_back({"word/x.xml", "<x/>", false, -1, -1, -1, 1});
    ExpectEqual(OpenEntries(encrypted).errorCode, "encrypted-entry", "encrypted entry");

    auto strongEncryption = basic;
    strongEncryption.push_back({"word/x.xml", "<x/>", false, -1, -1, -1, 0x41});
    ExpectEqual(OpenEntries(strongEncryption).errorCode, "encrypted-entry", "strong encryption");

    auto method = basic;
    method.push_back({"word/x.xml", "<x/>", false, -1, -1, 14});
    ExpectEqual(OpenEntries(method).errorCode, "corrupt-zip", "unsupported method");

    const std::string full = BuildZip(basic);
    ExpectEqual(core::OpenDocumentBytes(full.substr(0, full.size() / 2)).errorCode, "corrupt-zip",
                "truncated archive");
    ExpectEqual(core::OpenDocumentBytes("").errorCode, "not-a-zip", "empty input");
    ExpectEqual(core::OpenDocumentBytes("PK\x03\x04").errorCode, "corrupt-zip",
                "a bare local header signature");
}

void TestNames() {
    using leandoc::core::detail::IsSafeEntryName;
    struct Case {
        std::string name;
        bool safe;
    };
    const Case cases[] = {
        {"word/document.xml", true},
        {"[Content_Types].xml", true},
        {"word/media/image 1.png", true},
        {"a/./b.xml", true},
        {"a/.../b.xml", true},
        {"word/", true},
        {"", false},
        {"../evil.xml", false},
        {"word/../../evil.xml", false},
        {"word/..", false},
        {"/abs.xml", false},
        {"a\\b.xml", false},
        {"C:/x.xml", false},
        {"c:x.xml", false},
        {"word/D:/x.xml", false},
        {std::string("a\0b", 3), false},
        {std::string(512, 'n'), true},
        {std::string(513, 'n'), false},
    };
    for (const Case& item : cases) {
        Expect(IsSafeEntryName(item.name) == item.safe,
               "entry name \"" + core::EscapeJsonString(item.name) + "\"");
    }

    auto unsafe = MinimalDocx("<w:p/>");
    unsafe.push_back({"word/../x.xml", "<x/>"});
    ExpectEqual(OpenEntries(unsafe).errorCode, "unsafe-entry-name", "unsafe name in a package");

    auto exact = MinimalDocx("<w:p/>");
    exact.push_back({"word/document.xml", DocumentXml("<w:p/>")});
    ExpectEqual(OpenEntries(exact).errorCode, "duplicate-entry", "exact duplicate");

    auto caseOnly = MinimalDocx("<w:p/>");
    caseOnly.push_back({"Word/Styles.xml", "<x/>"});
    caseOnly.push_back({"word/styles.XML", "<x/>"});
    ExpectEqual(OpenEntries(caseOnly).errorCode, "duplicate-entry", "duplicate ignoring case");
}

// ---------------------------------------------------------------------------
// XML safety

void TestXmlSafety() {
    const std::string doctype =
        "<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY e \"boom\">]><x>&e;</x>";

    auto inStyles = MinimalDocx("<w:p/>",
                                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/"
                                "officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>",
                                "<Override PartName=\"/word/styles.xml\" ContentType=\"application/"
                                "vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>");
    inStyles.push_back({"word/styles.xml", doctype});
    ExpectEqual(OpenEntries(inStyles).errorCode, "xml-doctype", "DOCTYPE in styles.xml");

    auto inTypes = MinimalDocx("<w:p/>");
    inTypes[0].data = doctype;
    const core::OpenResult types = OpenEntries(inTypes);
    ExpectEqual(types.errorCode, "xml-doctype", "DOCTYPE in [Content_Types].xml");
    Expect(types.kind == core::FileKind::NotADocument, "unclassified when content types fail");

    auto inRels = MinimalDocx("<w:p/>");
    inRels[3].data = "<?xml version=\"1.0\"?><!doctype Relationships><Relationships/>";
    ExpectEqual(OpenEntries(inRels).errorCode, "xml-doctype", "lowercase doctype in rels");

    auto entityOnly = MinimalDocx("<w:p/>");
    entityOnly[2].data = DocumentXml("<w:p/>") + "<!ENTITY x \"y\">";
    ExpectEqual(OpenEntries(entityOnly).errorCode, "xml-doctype", "a stray ENTITY declaration");

    // UTF-16 hides the DOCTYPE from the byte scan; the parsed tree shows it.
    const std::string utf8 = "<?xml version=\"1.0\" encoding=\"UTF-16\"?><!DOCTYPE w:document>" +
                             DocumentXml("<w:p/>").substr(DocumentXml("").find("<w:document"));
    std::string utf16 = "\xFF\xFE";
    for (const char character : utf8) {
        utf16 += character;
        utf16 += '\0';
    }
    auto wide = MinimalDocx("<w:p/>");
    wide[2].data = utf16;
    ExpectEqual(OpenEntries(wide).errorCode, "xml-doctype", "DOCTYPE in a UTF-16 part");

    // A UTF-16 part without a DTD is converted and saved as UTF-8.
    const std::string plain = "<?xml version=\"1.0\" encoding=\"UTF-16\"?>" +
                              DocumentXml("<w:p><w:r><w:t>Wide</w:t></w:r></w:p>")
                                  .substr(DocumentXml("").find("<w:document"));
    std::string plain16 = "\xFF\xFE";
    for (const char character : plain) {
        plain16 += character;
        plain16 += '\0';
    }
    auto widePlain = MinimalDocx("<w:p/>");
    widePlain[2].data = plain16;
    const core::OpenResult wideResult = OpenEntries(widePlain);
    Expect(wideResult.ok, "a UTF-16 document opens");
    const auto wideParts = Unzip(wideResult.sanitizedPackage);
    const auto wideMain = wideParts.find("word/document.xml");
    Expect(wideMain != wideParts.end() && Contains(wideMain->second, "encoding=\"UTF-8\"") &&
               BodyText(wideMain->second) == "Wide",
           "a UTF-16 part is saved as UTF-8 with a matching declaration");

    auto malformed = MinimalDocx("<w:p>");
    ExpectEqual(OpenEntries(malformed).errorCode, "xml-malformed", "unclosed element");
    auto duplicate = MinimalDocx("<w:p><w:pPr><w:jc w:val=\"center\" w:val=\"left\"/></w:pPr></w:p>");
    ExpectEqual(OpenEntries(duplicate).errorCode, "xml-malformed", "duplicate attribute");
    std::string manyAttributes = "<w:p><w:pPr><w:jc";
    for (int index = 0; index < 40; ++index) {
        manyAttributes += " w:a" + std::to_string(index) + "=\"1\"";
    }
    Expect(OpenEntries(MinimalDocx(manyAttributes + "/></w:pPr></w:p>")).ok,
           "forty distinct attributes are fine");
    ExpectEqual(OpenEntries(MinimalDocx(manyAttributes + " w:a7=\"2\"/></w:pPr></w:p>")).errorCode,
                "xml-malformed", "a duplicate among forty attributes");

    // Depth: document > body > nested sdt pairs > p > r > t.
    const auto nested = [](int pairs) {
        std::string body;
        for (int index = 0; index < pairs; ++index) body += "<w:sdt><w:sdtContent>";
        body += "<w:p><w:r><w:t>Deep</w:t></w:r></w:p>";
        for (int index = 0; index < pairs; ++index) body += "</w:sdtContent></w:sdt>";
        return MinimalDocx(body);
    };
    core::Limits depth;
    depth.maximumXmlDepth = 15;  // 2 + 2 * 5 + 3 elements = 15
    Expect(OpenEntries(nested(5), depth).ok, "depth exactly at the limit");
    depth.maximumXmlDepth = 14;
    ExpectEqual(OpenEntries(nested(5), depth).errorCode, "xml-too-deep", "one level too deep");
    ExpectEqual(OpenEntries(nested(200)).errorCode, "xml-too-deep", "default depth limit");
}

// ---------------------------------------------------------------------------
// Report JSON

void TestJson() {
    ExpectEqual(core::EscapeJsonString("a\"b\\c\n\t\x01"), "a\\\"b\\\\c\\n\\t\\u0001",
                "control characters and quotes");
    ExpectEqual(core::EscapeJsonString("x\xE2\x80\xA8y\xE2\x80\xA9"), "x\\u2028y\\u2029",
                "line and paragraph separators");
    ExpectEqual(core::EscapeJsonString("\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A"),
                "\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A", "Arabic passes through");
    std::string appended = "x";
    core::AppendJsonString(appended, "\"");
    ExpectEqual(appended, "x\\\"", "AppendJsonString adds no quotes");

    core::OpenResult result;
    result.ok = true;
    result.kind = core::FileKind::Docx;
    result.sourceBytes = 1234;
    result.features.push_back({core::Feature::TextBox, 2});
    result.features.push_back({core::Feature::Macros, 1});
    result.outline.push_back({1, "Intro \"one\"", 0});
    result.properties.title = "T";
    result.properties.creator = "C";
    result.properties.rightToLeft = true;
    ExpectEqual(
        core::ReportToJson(result),
        "{\"ok\":true,\"errorCode\":\"\",\"errorMessage\":\"\",\"kind\":\"docx\","
        "\"sourceBytes\":1234,\"features\":[{\"code\":\"text-box\",\"count\":2,"
        "\"notDisplayed\":true,\"singular\":\"text box\",\"plural\":\"text boxes\"},"
        "{\"code\":\"macros\",\"count\":1,\"notDisplayed\":false,\"singular\":\"macro project\","
        "\"plural\":\"macro projects\"}],\"outline\":[{\"level\":1,\"text\":\"Intro \\\"one\\\"\","
        "\"paragraph\":0}],\"properties\":{\"title\":\"T\",\"creator\":\"C\","
        "\"rightToLeft\":true}}",
        "report JSON shape");

    const core::OpenResult failed = core::OpenDocumentBytes("not a document");
    ExpectEqual(core::ReportToJson(failed),
                "{\"ok\":false,\"errorCode\":\"not-a-zip\",\"errorMessage\":\"This file is not a "
                "Word document.\",\"kind\":\"not-a-document\",\"sourceBytes\":14,\"features\":[],"
                "\"outline\":[],\"properties\":{\"title\":\"\",\"creator\":\"\","
                "\"rightToLeft\":false}}",
                "error report JSON shape");

    for (int index = 0; index <= static_cast<int>(core::Feature::EmbeddedFonts); ++index) {
        const auto feature = static_cast<core::Feature>(index);
        Expect(std::strlen(core::FeatureCode(feature)) > 0 &&
                   std::strlen(core::LabelFor(feature).singular) > 0 &&
                   std::strlen(core::LabelFor(feature).plural) > 0,
               "every feature has a code and labels");
        Expect(core::FeatureIsNotDisplayed(feature) == (index <= static_cast<int>(core::Feature::AltChunk)),
               "not-displayed group");
    }
}

// ---------------------------------------------------------------------------
// Outline

const char kStylesRelationship[] =
    "<Relationship Id=\"rIdS\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/styles\" Target=\"styles.xml\"/>";
const char kStylesOverride[] =
    "<Override PartName=\"/word/styles.xml\" ContentType=\"application/"
    "vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>";

std::vector<TestEntry> WithStyles(const std::string& body, const std::string& styles) {
    auto entries = MinimalDocx(body, kStylesRelationship, kStylesOverride);
    entries.push_back(
        {"word/styles.xml",
         "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">" +
             styles + "</w:styles>"});
    return entries;
}

std::string Paragraph(const std::string& text, const std::string& style = "",
                      const std::string& properties = "") {
    std::string ppr = style.empty() ? "" : "<w:pStyle w:val=\"" + style + "\"/>";
    ppr += properties;
    return "<w:p>" + (ppr.empty() ? "" : "<w:pPr>" + ppr + "</w:pPr>") +
           "<w:r><w:t xml:space=\"preserve\">" + text + "</w:t></w:r></w:p>";
}

void TestOutline() {
    const std::string styles =
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"H1\"><w:name w:val=\"Big\"/><w:pPr><w:outlineLvl w:val=\"0\"/></w:pPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Child\"><w:basedOn w:val=\"H1\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"GrandChild\"><w:basedOn w:val=\"Child\"/><w:pPr><w:outlineLvl w:val=\"2\"/></w:pPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Named\"><w:name w:val=\"Heading 6\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"CycleA\"><w:basedOn w:val=\"CycleB\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"CycleB\"><w:basedOn w:val=\"CycleA\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Body9\"><w:basedOn w:val=\"H1\"/><w:pPr><w:outlineLvl w:val=\"9\"/></w:pPr></w:style>";
    std::string euro;
    for (int index = 0; index < 100; ++index) euro += "\xE2\x82\xAC";
    const std::string textBox =
        "<w:p><w:r><w:t>Has a box</w:t></w:r><w:r><w:drawing><wps:txbx><w:txbxContent>" +
        Paragraph("Inside the box", "H1") + "</w:txbxContent></wps:txbx></w:drawing></w:r></w:p>";
    const std::string body =
        Paragraph("Top", "H1") +                                        // 0
        Paragraph("Inherited", "Child") +                               // 1
        Paragraph("Own level", "GrandChild") +                          // 2
        Paragraph("By name", "Named") +                                 // 3
        Paragraph("Cycle", "CycleA") +                                  // 4
        Paragraph("Level nine", "Body9") +                              // 5
        textBox +                                                       // 6
        "<w:tbl><w:tr><w:tc>" + Paragraph("In a cell", "H1") + "</w:tc></w:tr></w:tbl>" +  // 7
        Paragraph("Direct", "", "<w:outlineLvl w:val=\"3\"/>") +        // 8
        Paragraph("", "H1") +                                           // 9 (empty, skipped)
        Paragraph(euro, "H1") +                                         // 10
        Paragraph("  spaced \t  out  ", "H1") +                         // 11
        Paragraph("Override to body", "H1", "<w:outlineLvl w:val=\"9\"/>");  // 12
    const core::OpenResult result = OpenEntries(WithStyles(body, styles));
    Expect(result.ok, "outline document opens");
    std::string capped;
    for (int index = 0; index < 66; ++index) capped += "\xE2\x82\xAC";
    ExpectEqual(OutlineText(result),
                "1|0|Top\n1|1|Inherited\n3|2|Own level\n6|3|By name\n1|7|In a cell\n"
                "4|8|Direct\n1|10|" + capped + "\n1|11|spaced out",
                "outline levels, indices, and text");
}

void TestRightToLeft() {
    const std::string bidiDefault =
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:pPr><w:bidi/></w:pPr></w:style>";
    Expect(OpenEntries(WithStyles(Paragraph("x"), bidiDefault)).properties.rightToLeft,
           "a bidi default paragraph style");
    const std::string docDefaults =
        "<w:docDefaults><w:pPrDefault><w:pPr><w:bidi/></w:pPr></w:pPrDefault></w:docDefaults>";
    Expect(OpenEntries(WithStyles(Paragraph("x"), docDefaults)).properties.rightToLeft,
           "bidi document defaults");
    Expect(OpenEntries(MinimalDocx("<w:p/>" + Paragraph("x", "", "<w:bidi/>"))).properties.rightToLeft,
           "the first non-empty paragraph is bidi");
    Expect(!OpenEntries(MinimalDocx(Paragraph("x") + Paragraph("y", "", "<w:bidi/>")))
                .properties.rightToLeft,
           "only the first non-empty paragraph counts");
    Expect(!OpenEntries(MinimalDocx(Paragraph("x", "", "<w:bidi w:val=\"0\"/>")))
                .properties.rightToLeft,
           "bidi off");
}

// ---------------------------------------------------------------------------
// Sanitizing details

std::string SanitizedBody(const std::string& body, const std::string& relationships = "") {
    const core::OpenResult result = OpenEntries(MinimalDocx(body, relationships));
    if (!result.ok) {
        return "<error " + result.errorCode + ">";
    }
    const auto parts = Unzip(result.sanitizedPackage);
    const auto main = parts.find("word/document.xml");
    return main == parts.end() ? std::string() : main->second;
}

std::string Field(const std::string& instruction, const std::string& result) {
    return "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText>" + instruction +
           "</w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
           "<w:r><w:t>" + result + "</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r>";
}

void TestSanitizing() {
    // Nested field inside a stripped instruction: everything up to the
    // outer separator goes, the cached result stays.
    const std::string nested =
        "<w:p><w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText>INCLUDEPICTURE \""
        "</w:instrText></w:r>" + Field("MERGEFIELD Path", "c:/x.png") +
        "<w:r><w:instrText>\" \\d</w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
        "<w:r><w:t>Result</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>";
    std::string output = SanitizedBody(nested);
    Expect(BodyText(output) == "Result" && !Contains(output, "fldChar") &&
               !Contains(output, "MERGEFIELD"),
           "a field nested in a stripped instruction is removed with it");

    output = SanitizedBody("<w:p><w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText>"
                           "DDEAUTO x y</w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"end\"/>"
                           "</w:r><w:r><w:t>After</w:t></w:r></w:p>");
    Expect(BodyText(output) == "After" && !Contains(output, "DDEAUTO") && !Contains(output, "fldChar"),
           "a DDE field without a result is removed");

    output = SanitizedBody("<w:p>" + Field("HYPERLINK \"javascript:alert(1)\"", "Bad") +
                           Field("HYPERLINK \"https://example.invalid\" \\o \"tip\"", "Good") +
                           Field("HYPERLINK \\l \"anchor\"", "Local") + "</w:p>");
    Expect(BodyText(output) == "BadGoodLocal" && !Contains(output, "javascript") &&
               Contains(output, "https://example.invalid") && Contains(output, "\\l"),
           "unsafe HYPERLINK fields keep only their result");

    output = SanitizedBody("<w:p>" + Field("PAGE", "7") + "</w:p>");
    Expect(Contains(output, "PAGE") && Contains(output, "fldChar"), "ordinary fields are kept");

    output = SanitizedBody("<w:p><w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText>"
                           "INCLUDETEXT x</w:instrText></w:r></w:p>");
    Expect(!Contains(output, "INCLUDETEXT") && !Contains(output, "fldChar"),
           "an unterminated external field loses its instruction");

    // A Choice with an OLE object falls back; the fallback content replaces
    // the AlternateContent element.
    output = SanitizedBody(
        "<w:p><w:r><mc:AlternateContent><mc:Choice Requires=\"wps\"><w:object><o:OLEObject/>"
        "</w:object></mc:Choice><mc:Fallback><w:t>Fallback text</w:t></mc:Fallback>"
        "</mc:AlternateContent></w:r></w:p>");
    Expect(BodyText(output) == "Fallback text" && !Contains(output, "AlternateContent") &&
               !Contains(output, "OLEObject"),
           "forbidden Choice falls back");
    output = SanitizedBody(
        "<w:p><w:r><w:t>Kept</w:t></w:r><w:r><mc:AlternateContent><mc:Choice Requires=\"x\"><w:object/>"
        "</mc:Choice><mc:Fallback><w:control/></mc:Fallback></mc:AlternateContent></w:r></w:p>");
    Expect(BodyText(output) == "Kept" && !Contains(output, "AlternateContent"),
           "both branches forbidden drops the element");
    output = SanitizedBody(
        "<w:p><w:r><mc:AlternateContent><mc:Choice Requires=\"wps\"><w:t>Choice</w:t></mc:Choice>"
        "<mc:Fallback><w:t>Fallback</w:t></mc:Fallback></mc:AlternateContent></w:r></w:p>");
    Expect(Contains(output, "mc:Choice") && Contains(output, "mc:Fallback"),
           "a clean AlternateContent is kept");

    // Tracked-change markers inside properties are removed, not unwrapped.
    output = SanitizedBody(
        "<w:p><w:pPr><w:rPr><w:ins w:id=\"1\" w:author=\"a\"/></w:rPr></w:pPr>"
        "<w:ins w:id=\"2\"><w:r><w:t>New</w:t></w:r></w:ins></w:p>");
    Expect(BodyText(output) == "New" && !Contains(output, "w:ins"), "insertion markers");

    // Namespace declarations on an unwrapped element move to its children.
    output = SanitizedBody(
        "<w:p><w:ins xmlns:x=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<x:r><x:t>Prefixed</x:t></x:r></w:ins></w:p>");
    Expect(BodyText(output) == "Prefixed" && Contains(output, "xmlns:x="),
           "unwrapping keeps namespace declarations");

    // Prefixes are not fixed: a different prefix for WordprocessingML works.
    const core::OpenResult renamed = core::OpenDocumentBytes(BuildZip([] {
        auto entries = MinimalDocx("");
        entries[2].data =
            "<q:document xmlns:q=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
            "<q:body><q:p><q:pPr><q:outlineLvl q:val=\"0\"/></q:pPr><q:r><q:t>Q</q:t></q:r></q:p>"
            "<q:p><q:del><q:r><q:delText>gone</q:delText></q:r></q:del></q:p></q:body></q:document>";
        return entries;
    }()));
    Expect(renamed.ok && OutlineText(renamed) == "1|0|Q" &&
               !Contains(PackageBodyText(renamed.sanitizedPackage), "gone"),
           "elements are matched by namespace URI, not prefix");
    const core::OpenResult wrongNamespace = core::OpenDocumentBytes(BuildZip([] {
        auto entries = MinimalDocx("");
        entries[2].data =
            "<w:document xmlns:w=\"urn:not-word\"><w:body><w:p><w:pPr><w:outlineLvl w:val=\"0\"/>"
            "</w:pPr><w:r><w:t>W</w:t></w:r></w:p></w:body></w:document>";
        return entries;
    }()));
    Expect(wrongNamespace.ok && wrongNamespace.outline.empty(),
           "the w prefix bound to another namespace is not WordprocessingML");

    // Hyperlinks: relationships decide.
    const std::string relationships =
        "<Relationship Id=\"rIdA\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/hyperlink\" Target=\"HTTPS://example.invalid\" TargetMode=\"External\"/>"
        "<Relationship Id=\"rIdB\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/hyperlink\" Target=\" javascript:x\" TargetMode=\"External\"/>"
        "<Relationship Id=\"rIdC\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/hyperlink\" Target=\"vbscript:x\" TargetMode=\"External\"/>";
    output = SanitizedBody(
        "<w:p><w:hyperlink r:id=\"rIdA\"><w:r><w:t>A</w:t></w:r></w:hyperlink>"
        "<w:hyperlink r:id=\"rIdB\"><w:r><w:t>B</w:t></w:r></w:hyperlink>"
        "<w:hyperlink r:id=\"rIdC\" w:anchor=\"top\"><w:r><w:t>C</w:t></w:r></w:hyperlink></w:p>",
        relationships);
    Expect(BodyText(output) == "ABC" && Contains(output, "r:id=\"rIdA\"") &&
               !Contains(output, "rIdB") && !Contains(output, "rIdC") &&
               Contains(output, "w:anchor=\"top\""),
           "hyperlink relationships: scheme check is case-insensitive and strict");

    // Content types: overrides list only parts present; macro type switched.
    auto macro = MinimalDocx("<w:p/>", "", "<Override PartName=\"/word/vbaProject.bin\" "
                                           "ContentType=\"application/vnd.ms-office.vbaProject\"/>");
    const std::string docxType =
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml";
    macro[0].data.replace(macro[0].data.find(docxType), docxType.size(),
                          "application/vnd.ms-word.document.macroEnabled.main+xml");
    macro.push_back({"word/vbaProject.bin", "binary"});
    const core::OpenResult macroResult = OpenEntries(macro);
    Expect(macroResult.ok && macroResult.kind == core::FileKind::Docm, "docm opens");
    const auto macroParts = Unzip(macroResult.sanitizedPackage);
    const std::string& types = macroParts.at("[Content_Types].xml");
    Expect(!Contains(types, "vbaProject") && !Contains(types, "macroEnabled") &&
               Contains(types, "wordprocessingml.document.main+xml"),
           "sanitized content types");
}

// Accepting a deleted paragraph mark joins the paragraph to the next block.
// A paragraph with nothing left was kept as an empty line: 126 of them in a
// survey with tracked changes, about 3 pages.
void TestDeletedParagraphMarks() {
    const std::string deletedMark =
        "<w:pPr><w:rPr><w:del w:id=\"1\" w:author=\"a\" w:date=\"2026-01-01T00:00:00Z\"/></w:rPr></w:pPr>";
    const std::string deletedText =
        "<w:del w:id=\"2\" w:author=\"a\"><w:r><w:delText>Gone</w:delText></w:r></w:del>";
    const std::string next = "<w:p><w:r><w:t>Next</w:t></w:r></w:p>";

    std::string output = SanitizedBody("<w:p>" + deletedMark + deletedText + "</w:p>" + next);
    ExpectEqual(BodyText(output), "Next", "an emptied paragraph whose mark is deleted goes");

    output = SanitizedBody("<w:p>" + deletedMark + "<w:r><w:rPr><w:b/></w:rPr></w:r></w:p>" +
                           "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Cell</w:t></w:r></w:p></w:tc></w:tr></w:tbl>");
    ExpectEqual(BodyText(output), "Cell", "an empty paragraph with a deleted mark before a table goes");

    output = SanitizedBody("<w:p>" + deletedMark + "<w:r><w:t>Stays</w:t></w:r>" + deletedText + "</w:p>" + next);
    ExpectEqual(BodyText(output), "Stays\nNext", "a paragraph with text left keeps its mark");

    output = SanitizedBody(next + "<w:p>" + deletedMark + deletedText + "</w:p>");
    ExpectEqual(BodyText(output), "Next\n", "the last paragraph of the body stays");

    output = SanitizedBody("<w:tbl><w:tr><w:tc><w:p>" + deletedMark + deletedText +
                           "</w:p></w:tc></w:tr></w:tbl>" + next);
    ExpectEqual(BodyText(output), "\nNext", "the last paragraph of a cell stays");

    output = SanitizedBody("<w:p><w:pPr><w:sectPr/><w:rPr><w:del w:id=\"1\" w:author=\"a\"/></w:rPr></w:pPr>" +
                           deletedText + "</w:p>" + next);
    ExpectEqual(BodyText(output), "\nNext", "a paragraph that ends a section stays");

    output = SanitizedBody("<w:p><w:pPr><w:rPr><w:ins w:id=\"3\" w:author=\"a\"/></w:rPr></w:pPr>" +
                           deletedText + "</w:p>" + next);
    ExpectEqual(BodyText(output), "\nNext", "an inserted paragraph mark is not a deleted one");

    // The outline counts the paragraphs the reader receives.
    const core::OpenResult outline = OpenEntries(MinimalDocx(
        "<w:p>" + deletedMark + deletedText + "</w:p>"
        "<w:p><w:pPr><w:outlineLvl w:val=\"0\"/></w:pPr><w:r><w:t>Heading</w:t></w:r></w:p>"));
    ExpectEqual(OutlineText(outline), "1|0|Heading", "outline indexes skip the joined paragraph");
}

// ---------------------------------------------------------------------------
// Classification and files

std::string CompoundFile(bool corruptChain) {
    std::string bytes(512 * 3, '\0');
    const unsigned char signature[] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    std::memcpy(bytes.data(), signature, 8);
    auto put16 = [&bytes](std::size_t offset, unsigned value) {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>(value >> 8);
    };
    auto put32 = [&bytes](std::size_t offset, std::uint32_t value) {
        for (int index = 0; index < 4; ++index) {
            bytes[offset + index] = static_cast<char>((value >> (8 * index)) & 0xFF);
        }
    };
    put16(30, 9);
    put32(44, 1);   // one FAT sector
    put32(48, 1);   // directory at sector 1
    put32(68, 0xFFFFFFFE);
    put32(76, 0);   // FAT at sector 0
    for (std::size_t index = 1; index < 109; ++index) put32(76 + index * 4, 0xFFFFFFFF);
    put32(512, 0xFFFFFFFD);
    put32(516, corruptChain ? 1 : 0xFFFFFFFE);  // a chain that loops on itself
    const std::string name = "Workbook";
    for (std::size_t index = 0; index < name.size(); ++index) {
        bytes[1024 + 128 + index * 2] = name[index];
    }
    put16(1024 + 128 + 64, static_cast<unsigned>(name.size() * 2 + 2));
    bytes[1024 + 128 + 66] = 2;
    return bytes;
}

void TestClassification(const fs::path& fixtures) {
    Expect(core::DetectKind("{\\rtf1 x}") == core::FileKind::Rtf, "RTF signature");
    Expect(core::DetectKind("plain") == core::FileKind::NotADocument, "plain text");
    Expect(core::DetectKind(CompoundFile(false)) == core::FileKind::NotADocument,
           "an OLE file without a Word stream");
    Expect(core::DetectKind(CompoundFile(true)) == core::FileKind::LegacyDoc,
           "a looping directory chain ends and falls back to legacy-doc");
    Expect(core::DetectKind(std::string("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8)) ==
               core::FileKind::LegacyDoc,
           "a bare OLE signature");

    Expect(core::IsRenderableKind(core::FileKind::Dotm) &&
               !core::IsRenderableKind(core::FileKind::Odt),
           "renderable kinds");
    Expect(core::IsSupportedDocumentPath("a/B.DOCX") && core::IsSupportedDocumentPath("x.odt") &&
               !core::IsSupportedDocumentPath("x.xlsx") && !core::IsSupportedDocumentPath("docx"),
           "supported extensions");

    const core::OpenResult folder = core::OpenDocumentFile(fixtures);
    Expect(!folder.ok && folder.errorCode == "read-failed", "a folder is not a document");
    const core::OpenResult missing = core::OpenDocumentFile(fixtures / "no-such-file.docx");
    Expect(!missing.ok && missing.errorCode == "read-failed", "a missing file");
    core::Limits small;
    small.maximumInputBytes = 100;
    const core::OpenResult big =
        core::OpenDocumentFile(fixtures / "features" / "styles.docx", small);
    Expect(big.errorCode == "too-large" && big.sourceBytes > 100, "file size is checked first");

    Expect(core::IsPathWithin(fixtures, fixtures / "features" / "styles.docx"),
           "IsPathWithin inside");
    Expect(!core::IsPathWithin(fixtures / "features", fixtures / "security" / "dde.docx"),
           "IsPathWithin sibling");
}

void TestPerformance(const fs::path& fixtures) {
    const fs::path path = fixtures / "features" / "long-300-pages.docx";
    double best = 1e9;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto start = std::chrono::steady_clock::now();
        const core::OpenResult result = core::OpenDocumentFile(path);
        const double elapsed = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        best = std::min(best, elapsed);
        Expect(result.ok, "the 300-page document opens");
    }
    std::cout << "300-page fixture: " << best << " ms (best of 3)\n";
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    Expect(best < 500.0, "the 300-page document opens in under 500 ms");
#endif
}


// Text boxes, shapes, and equations count only where docx-preview 0.4.1 does
// not draw them; see FeatureIsNotDisplayed() in DocCore.cpp.
void TestDisplayedFeatures() {
    const auto features = [](const std::string& body) {
        return FeaturesText(OpenEntries(MinimalDocx(body)));
    };
    const std::string wpsTextBox =
        "<w:drawing><wp:inline><wp:extent cx=\"1\" cy=\"1\"/><wp:docPr id=\"1\" name=\"t\"/>"
        "<a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/"
        "wordprocessingShape\"><wps:wsp><wps:txbx><w:txbxContent><w:p><w:r><w:t>In box</w:t>"
        "</w:r></w:p></w:txbxContent></wps:txbx></wps:wsp></a:graphicData></a:graphic>"
        "</wp:inline></w:drawing>";
    const std::string vmlTextBox =
        "<w:pict><v:shape style=\"width:10pt;height:10pt\"><v:textbox><w:txbxContent><w:p><w:r>"
        "<w:t>In box</w:t></w:r></w:p></w:txbxContent></v:textbox></v:shape></w:pict>";
    const auto alternate = [](const std::string& choice, const std::string& fallback) {
        return "<w:p><w:r><mc:AlternateContent><mc:Choice Requires=\"wps\">" + choice +
               "</mc:Choice><mc:Fallback>" + fallback +
               "</mc:Fallback></mc:AlternateContent></w:r></w:p>";
    };

    ExpectEqual(features("<w:p><w:r>" + wpsTextBox + "</w:r></w:p>"), "text-box=1",
                "a DrawingML text box without a fallback is not displayed");
    ExpectEqual(features(alternate(wpsTextBox, vmlTextBox)), "",
                "a DrawingML text box with a VML fallback is displayed");
    ExpectEqual(features(alternate(wpsTextBox, "<w:drawing/>" + vmlTextBox)), "text-box=1",
                "docx-preview reads only the first element of a fallback");
    ExpectEqual(features("<w:p><mc:AlternateContent><mc:Choice Requires=\"wps\"><w:r>" +
                         wpsTextBox + "</w:r></mc:Choice><mc:Fallback><w:r>" + vmlTextBox +
                         "</w:r></mc:Fallback></mc:AlternateContent></w:p>"),
                "text-box=1", "an AlternateContent outside a run is not read at all");
    ExpectEqual(features("<w:p><w:r>" + vmlTextBox + "</w:r></w:p>"), "",
                "a VML text box in a run is displayed");
    ExpectEqual(features("<w:p><w:r><w:pict><v:group><v:shape><v:textbox><w:txbxContent>"
                         "<w:p/></w:txbxContent></v:textbox></v:shape></v:group></w:pict></w:r></w:p>"),
                "text-box=1", "a VML text box inside a group is not displayed");
    ExpectEqual(features("<w:p><w:r><w:pict><v:rect style=\"width:9pt\"/><v:oval/><v:line/>"
                         "</w:pict></w:r></w:p>"),
                "", "VML rectangles, ovals, and lines are drawn");
    ExpectEqual(features("<w:p><w:r><w:pict><v:shape path=\"m0,0l1,1e\"/><v:roundrect/>"
                         "</w:pict></w:r></w:p>"),
                "shape=2", "VML paths and rounded rectangles are not drawn");
    ExpectEqual(features(alternate("<wps:wsp/>", "<w:pict><v:rect/></w:pict>")), "",
                "a DrawingML shape with a rectangle fallback is drawn");
    ExpectEqual(features(alternate("<wps:wsp/>", "<w:pict><v:roundrect/></w:pict>")), "shape=1",
                "a DrawingML shape with a rounded rectangle fallback is not drawn");
    ExpectEqual(features("<w:p><w:r><w:drawing><wp:inline><a:graphic><a:graphicData uri=\"x\">"
                         "<lc:lockedCanvas xmlns:lc=\"http://schemas.openxmlformats.org/drawingml/"
                         "2006/lockedCanvas\"><a:sp/><a:sp/></lc:lockedCanvas></a:graphicData>"
                         "</a:graphic></wp:inline></w:drawing></w:r></w:p>"),
                "shape=1", "a locked canvas counts as one shape");

    const auto math = [](const std::string& content) {
        return "<w:p><m:oMath>" + content + "</m:oMath></w:p>";
    };
    const std::string x = "<m:r><m:t>x</m:t></m:r>";
    ExpectEqual(features(math(x + "<m:f><m:num>" + x + "</m:num><m:den>" + x +
                              "</m:den></m:f><m:sSup><m:e>" + x + "</m:e><m:sup>" + x +
                              "</m:sup></m:sSup>")),
                "", "fractions and superscripts are displayed");
    ExpectEqual(features("<w:p><m:oMathPara><m:oMathParaPr/><m:oMath>" + x +
                         "</m:oMath><m:oMath>" + x + "</m:oMath></m:oMathPara></w:p>"),
                "", "an equation paragraph is displayed");
    ExpectEqual(features(math("<m:sSubSup><m:e>" + x + "</m:e><m:sub>" + x + "</m:sub><m:sup>" +
                              x + "</m:sup></m:sSubSup>")),
                "equation=1", "m:sSubSup loses its content");
    ExpectEqual(features(math("<m:acc><m:e>" + x + "</m:e></m:acc>")), "equation=1",
                "m:acc loses its content");
    ExpectEqual(features(math("<m:d><m:e>" + x + "</m:e></m:d>")), "equation=1",
                "a delimiter without m:dPr stops docx-preview");
    ExpectEqual(features(math("<m:d><m:dPr/><m:e>" + x + "</m:e></m:d>")), "",
                "a delimiter with m:dPr is displayed");
    ExpectEqual(features(math("<m:nary><m:sub>" + x + "</m:sub><m:e>" + x + "</m:e></m:nary>")),
                "equation=1", "an n-ary operator with only a lower limit stops docx-preview");
    ExpectEqual(features(math("<m:nary><m:naryPr/><m:sub/><m:sup/><m:e>" + x + "</m:e></m:nary>")),
                "", "an n-ary operator with both limits is displayed");
    ExpectEqual(features(math("<m:rad><m:radPr><m:degHide/></m:radPr><m:e>" + x + "</m:e></m:rad>")),
                "equation=1", "a radical whose degree docx-preview cannot find");
    ExpectEqual(features(math("<m:rad><m:radPr><m:degHide m:val=\"1\"/></m:radPr><m:e>" + x +
                              "</m:e></m:rad>")),
                "", "a square root is displayed");
    ExpectEqual(features("<w:p><w:hyperlink w:anchor=\"a\"><m:oMath>" + x +
                         "</m:oMath></w:hyperlink></w:p>"),
                "equation=1", "an equation inside a hyperlink is not read");
    ExpectEqual(features("<w:p><w:ins w:id=\"1\"><m:oMath>" + x + "</m:oMath></w:ins></w:p>"),
                "tracked-changes=1", "an inserted equation is displayed once accepted");
    ExpectEqual(features("<w:p><w:sdt><w:sdtContent><m:oMath>" + x +
                         "</m:oMath></w:sdtContent></w:sdt></w:p>"),
                "", "an equation in an inline content control is displayed");

    // docx-preview drops w:fldSimple and its content, so every simple field
    // reaches the reader as its cached result.
    std::string output = SanitizedBody(
        "<w:p><w:fldSimple w:instr=\" PAGE \"><w:r><w:t>7</w:t></w:r></w:fldSimple>"
        "<w:fldSimple w:instr=\"INCLUDETEXT x\"><w:r><w:t> cached</w:t></w:r></w:fldSimple></w:p>");
    Expect(BodyText(output) == "7 cached" && !Contains(output, "fldSimple") &&
               !Contains(output, "INCLUDETEXT"),
           "simple fields become their result runs");

    // PAGE-like simple fields keep their kind as a complex field so the reader
    // can number each page; only the keyword is written.
    output = SanitizedBody(
        "<w:p><w:r><w:t>Page </w:t></w:r><w:fldSimple w:instr=\" page  \\* MERGEFORMAT \">"
        "<w:r><w:t>1</w:t></w:r></w:fldSimple><w:r><w:t> of </w:t></w:r>"
        "<w:fldSimple w:instr=\"NUMPAGES\"><w:r><w:t>9</w:t></w:r></w:fldSimple></w:p>");
    Expect(BodyText(output) == "Page 1 of 9", "page fields keep their cached text");
    Expect(!Contains(output, "fldSimple") && !Contains(output, "MERGEFORMAT"),
           "page fields lose the simple form and their switches");
    Expect(Contains(output, "<w:instrText xml:space=\"preserve\"> PAGE </w:instrText>") &&
               Contains(output, "<w:instrText xml:space=\"preserve\"> NUMPAGES </w:instrText>"),
           "page fields keep only their keyword");
    Expect(Contains(output, "<w:fldChar w:fldCharType=\"begin\"/>") &&
               Contains(output, "<w:fldChar w:fldCharType=\"separate\"/>") &&
               Contains(output, "<w:fldChar w:fldCharType=\"end\"/>"),
           "page fields become complex fields");
}


// The link rule every host applies before the operating system sees a link.
void TestLinks() {
    using leandoc::core::NormalizeExternalLink;
    auto link = [](std::string_view href) {
        return NormalizeExternalLink(href).value_or("<refused>");
    };
    ExpectEqual(link("https://example.com/a?b=c#d"), "https://example.com/a?b=c#d",
                "a normalised https link passes unchanged");
    ExpectEqual(link("HTTPS://example.com/"), "https://example.com/",
                "the scheme is compared without case and lowered");
    ExpectEqual(link("http://user@example.com:8080/x"), "http://user@example.com:8080/x",
                "userinfo and port are kept");
    ExpectEqual(link("MailTo:reader@example.com"), "mailto:reader@example.com",
                "mailto is accepted in any case");
    ExpectEqual(link("mailto:a@example.com?subject=Hi&attach=/home/u/.ssh/id_rsa&body=x"),
                "mailto:a@example.com?subject=Hi&body=x", "attach= is dropped");
    ExpectEqual(link("mailto:a@example.com?ATTACHMENT=%2Fetc%2Fpasswd&CC=b@example.com"),
                "mailto:a@example.com?CC=b@example.com",
                "parameters are matched without case and only the allow-list stays");
    ExpectEqual(link("mailto:?cc=b@example.com"), "mailto:?cc=b@example.com",
                "a cc recipient alone is enough");
    ExpectEqual(link("mailto:a@example.com#frag"), "mailto:a@example.com",
                "the mailto fragment is dropped");
    for (const char* refused :
         {"", "mailto:", "mailto:?subject=x", "mailto:?attach=/etc/passwd",
          "https://", "https:///path", "http:example.com", "https://?q", "ftp://example.com/",
          "file:///etc/passwd", "javascript:alert(1)", "data:text/html,x",
          "https://exa mple.com/", "mailto:a b@example.com", "https://example.com/\\x",
          "https://example.com/\"x", "https://example.com/<x>", "https://ex\x01.com/",
          "https://ex\xc3\xa9.com/", "leandoc://app/reader.html", "noscheme"}) {
        Expect(!NormalizeExternalLink(refused), std::string("refused: ") + refused);
    }
    Expect(!NormalizeExternalLink("https://example.com/" + std::string(8200, 'a')),
           "links over 8192 bytes are refused");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: leandoc_core_tests <tests/fixtures>\n";
        return 2;
    }
    const fs::path fixtures = argv[1];
    TestFixtures(fixtures);
    TestLimits();
    TestNames();
    TestXmlSafety();
    TestJson();
    TestOutline();
    TestRightToLeft();
    TestSanitizing();
    TestDeletedParagraphMarks();
    TestDisplayedFeatures();
    TestLinks();
    TestClassification(fixtures);
    TestPerformance(fixtures);
    std::cout << checks - failures << " of " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
