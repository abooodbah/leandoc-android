#pragma once

// LeanDoc's portable core. This header is the contract between the core, the
// three native hosts, and the leandoc-inspect command-line tool. Hosts never
// parse a document themselves: they read bytes, call OpenDocumentFile(), serve
// the sanitized package to the reader, and forward the JSON report.
//
// The core never performs I/O other than reading the one requested file, never
// follows a relationship outside the package, and never executes anything.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace leandoc::core {

// Every bound is checked before memory is committed. A document that exceeds a
// limit fails closed with a named error; it is never partially rendered.
struct Limits {
    std::uint64_t maximumInputBytes = 256ull * 1024ull * 1024ull;
    std::uint32_t maximumEntries = 4096;
    std::uint64_t maximumPartBytes = 64ull * 1024ull * 1024ull;
    std::uint64_t maximumTotalUncompressedBytes = 512ull * 1024ull * 1024ull;
    // Applies to entries whose uncompressed size exceeds 1 MiB.
    std::uint32_t maximumCompressionRatio = 100;
    std::uint32_t maximumXmlDepth = 256;
};

enum class FileKind {
    Docx,            // application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml
    Dotx,            // ...wordprocessingml.template.main+xml
    Docm,            // application/vnd.ms-word.document.macroEnabled.main+xml
    Dotm,            // application/vnd.ms-word.template.macroEnabledTemplate.main+xml
    LegacyDoc,       // OLE compound file without EncryptionInfo (Word 97-2003)
    EncryptedOoxml,  // OLE compound file holding EncryptionInfo + EncryptedPackage
    Rtf,             // starts with "{\rtf"
    Odt,             // ZIP whose "mimetype" entry is application/vnd.oasis.opendocument.text
    NotADocument,    // anything else, including other OOXML (xlsx, pptx)
};

// True for kinds the reader renders. The rest get a fallback page that offers
// the "open in editor" action.
bool IsRenderableKind(FileKind kind);
const char* FileKindName(FileKind kind);  // "docx", "legacy-doc", ...

// Stable feature codes. The reader and leandoc-inspect use the code; the
// display wording lives in one table in the core (FeatureLabel) so the
// fidelity bar, the CLI, and the tests agree.
enum class Feature {
    // Content LeanDoc does not display in v0.1. Reported in the fidelity bar
    // as "not displayed". Text boxes, equations, and shapes count only in the
    // forms the reader's docx-preview does not draw; the rules and evidence
    // are beside FeatureIsNotDisplayed() in DocCore.cpp.
    TextBox,          // wps:txbx without a drawn VML fallback, v:textbox docx-preview skips
    Equation,         // top-level m:oMath, m:oMathPara docx-preview cannot render fully
    Chart,            // c:chart references
    SmartArt,         // dgm:relIds
    Shape,            // wps:wsp / v:shape / locked canvas etc. without a picture, not drawn
    OleObject,        // w:object, o:OLEObject, embedded packages
    ActiveX,          // w:control, activeX parts
    AltChunk,         // w:altChunk (imported HTML/RTF/MHT content)
    // Present, handled safely, and disclosed. Reported as notices.
    TrackedChanges,   // w:ins, w:del, w:moveFrom, w:moveTo, *PrChange (shown as accepted)
    Comments,         // comments part or w:commentReference
    Macros,           // vbaProject.bin or a macro-enabled content type (never run)
    RemoteTemplate,   // attachedTemplate with an external target (never fetched)
    RemoteImage,      // r:link or external image relationships (never fetched)
    ExternalContent,  // other TargetMode="External" relationships, INCLUDETEXT/INCLUDEPICTURE fields (never fetched)
    DdeField,         // DDE / DDEAUTO fields (never run)
    EmbeddedFonts,    // w:embedRegular etc. (not loaded; system fonts are used)
};

const char* FeatureCode(Feature feature);     // "text-box", "tracked-changes", ...
bool FeatureIsNotDisplayed(Feature feature);  // true for the first group
// Singular and plural nouns for the fidelity sentence, for example
// {"text box", "text boxes"}.
struct FeatureLabel {
    const char* singular;
    const char* plural;
};
FeatureLabel LabelFor(Feature feature);

struct FeatureCount {
    Feature feature;
    std::uint32_t count = 0;
};

struct OutlineEntry {
    unsigned level = 1;  // 1..9, from the paragraph's effective outline level
    std::string text;    // plain text, whitespace collapsed, at most 200 bytes
    // Zero-based index among the body's w:p elements in document order,
    // counting paragraphs inside table cells. Paragraphs inside text box
    // content (w:txbxContent) and inside mc:Fallback are not counted: they
    // are not part of the document's block flow, and docx-preview draws text
    // box paragraphs, when it draws them at all, inside SVG. The count is
    // taken on the sanitized package the reader receives. The reader uses it
    // to find the rendered paragraph.
    std::uint32_t paragraphIndex = 0;
};

struct DocumentProperties {
    std::string title;    // docProps/core.xml dc:title, may be empty
    std::string creator;  // dc:creator, may be empty
    bool rightToLeft = false;  // the first body paragraph or the default paragraph style is bidi
};

struct OpenResult {
    // True when the core read the file without an error. Recognised kinds the
    // reader does not render (legacy-doc, encrypted-ooxml, rtf, odt) are ok
    // with an empty package; not-a-document is never ok.
    bool ok = false;
    // Stable error code for hosts and tests: "too-large", "not-a-zip",
    // "corrupt-zip", "too-many-entries", "part-too-large",
    // "compression-ratio", "unsafe-entry-name", "duplicate-entry",
    // "encrypted-entry", "missing-main-part", "xml-doctype", "xml-malformed",
    // "xml-too-deep", "read-failed". Empty when ok.
    std::string errorCode;
    std::string errorMessage;  // one plain sentence for the reader
    FileKind kind = FileKind::NotADocument;
    std::uint64_t sourceBytes = 0;

    // A new ZIP built only from allow-listed parts, with every external
    // relationship, the attached template, macros, ActiveX, OLE payloads,
    // embedded fonts, and comments removed, and tracked changes accepted.
    // Empty unless ok and IsRenderableKind(kind).
    std::string sanitizedPackage;

    std::vector<FeatureCount> features;  // only features with count > 0
    std::vector<OutlineEntry> outline;
    DocumentProperties properties;
};

// Classifies by content, never by extension alone. Reads at most the first
// 8 bytes plus, for ZIP files, the central directory.
FileKind DetectKind(std::string_view bytes);

OpenResult OpenDocumentBytes(std::string_view bytes, const Limits& limits = {});

// Reads a regular file (no directories, devices, or FIFOs) within
// limits.maximumInputBytes and calls OpenDocumentBytes().
OpenResult OpenDocumentFile(
    const std::filesystem::path& path,
    const Limits& limits = {});

// Extensions LeanDoc registers for: .docx .dotx .docm .dotm .doc .rtf .odt.
bool IsSupportedDocumentPath(const std::filesystem::path& path);

// The report as one JSON object, used by the reader bridge and by
// leandoc-inspect. Shape (all keys always present):
// {"ok":bool,"errorCode":str,"errorMessage":str,"kind":str,"sourceBytes":num,
//  "features":[{"code":str,"count":num,"notDisplayed":bool,
//               "singular":str,"plural":str}],
//  "outline":[{"level":num,"text":str,"paragraph":num}],
//  "properties":{"title":str,"creator":str,"rightToLeft":bool}}
std::string ReportToJson(const OpenResult& result);

// Same contract as LeanMark's helpers.
bool IsPathWithin(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate);
void AppendJsonString(std::string& output, std::string_view value);
std::string EscapeJsonString(std::string_view value);

}  // namespace leandoc::core
