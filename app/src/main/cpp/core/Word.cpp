#include "core/Word.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <unordered_map>
#include <utility>

namespace leandoc::core::detail {
namespace {

bool EndsWith(const char* text, const char* suffix) {
    const std::size_t textLength = std::strlen(text);
    const std::size_t suffixLength = std::strlen(suffix);
    return textLength >= suffixLength &&
           std::memcmp(text + textLength - suffixLength, suffix, suffixLength) == 0;
}

bool IsOneOf(const char* value, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (std::strcmp(value, name) == 0) {
            return true;
        }
    }
    return false;
}

// Tracked-change markup counted as one change each.
bool IsTrackedChange(const char* local) {
    return IsOneOf(
        local,
        {"ins", "del", "moveFrom", "moveTo", "rPrChange", "pPrChange",
         "sectPrChange", "tblPrChange", "trPrChange", "tcPrChange",
         "numberingChange", "tblGridChange", "tblPrExChange", "cellIns",
         "cellDel", "cellMerge"});
}

bool IsPropertyChange(const char* local) {
    return IsOneOf(
        local,
        {"rPrChange", "pPrChange", "sectPrChange", "tblPrChange", "trPrChange",
         "tcPrChange", "numberingChange", "tblGridChange", "tblPrExChange"});
}

// WordprocessingML elements that never reach the reader.
bool IsRemovedWordElement(const char* local) {
    return IsOneOf(
        local,
        {"del", "moveFrom", "delText", "delInstrText", "cellIns", "cellDel",
         "cellMerge", "moveFromRangeStart", "moveFromRangeEnd",
         "moveToRangeStart", "moveToRangeEnd", "customXmlDelRangeStart",
         "customXmlDelRangeEnd", "customXmlInsRangeStart",
         "customXmlInsRangeEnd", "customXmlMoveFromRangeStart",
         "customXmlMoveFromRangeEnd", "customXmlMoveToRangeStart",
         "customXmlMoveToRangeEnd", "commentRangeStart", "commentRangeEnd",
         "commentReference", "object", "control", "altChunk", "subDoc",
         "contentPart", "attachedTemplate", "mailMerge", "embedRegular",
         "embedBold", "embedItalic", "embedBoldItalic"}) ||
           IsPropertyChange(local);
}

// Content whose presence inside an mc:Choice makes the core fall back.
bool IsForbiddenContent(const QName& name) {
    switch (name.ns) {
        case Ns::W:
            return IsOneOf(
                name.local, {"object", "control", "altChunk", "subDoc", "contentPart"});
        case Ns::W14:
            return std::strcmp(name.local, "contentPart") == 0;
        case Ns::O:
            return std::strcmp(name.local, "OLEObject") == 0;
        case Ns::C:
        case Ns::Cx:
            return std::strcmp(name.local, "chart") == 0;
        case Ns::Dgm:
            return std::strcmp(name.local, "relIds") == 0;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Field instructions

enum class FieldKind { Ordinary, External, Dde, UnsafeHyperlink };

std::string UppercaseAscii(std::string_view value) {
    std::string output(value);
    for (char& character : output) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return output;
}

bool IsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n' || character == '\f' || character == '\v';
}

// Splits a field instruction into tokens; quoted strings are one token
// without their quotes.
std::vector<std::string> Tokenize(std::string_view instruction) {
    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < instruction.size() && tokens.size() < 64) {
        while (index < instruction.size() && IsSpace(instruction[index])) {
            ++index;
        }
        if (index >= instruction.size()) {
            break;
        }
        std::string token;
        if (instruction[index] == '"') {
            ++index;
            while (index < instruction.size() && instruction[index] != '"') {
                token += instruction[index++];
            }
            ++index;
        } else {
            while (index < instruction.size() && !IsSpace(instruction[index]) &&
                   instruction[index] != '"') {
                token += instruction[index++];
            }
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

FieldKind ClassifyField(std::string_view instruction) {
    const auto tokens = Tokenize(instruction);
    if (tokens.empty()) {
        return FieldKind::Ordinary;
    }
    const std::string keyword = UppercaseAscii(tokens.front());
    if (keyword == "INCLUDEPICTURE" || keyword == "INCLUDETEXT" ||
        keyword == "INCLUDE" || keyword == "LINK" || keyword == "IMPORT") {
        return FieldKind::External;
    }
    if (keyword == "DDE" || keyword == "DDEAUTO") {
        return FieldKind::Dde;
    }
    if (keyword == "HYPERLINK") {
        // The first argument that is not a switch is the target. \l names a
        // bookmark inside the document; \o, \t, and \m take one argument.
        std::string target;
        for (std::size_t index = 1; index < tokens.size(); ++index) {
            const std::string& token = tokens[index];
            if (!token.empty() && token[0] == '\\') {
                const std::string flag = UppercaseAscii(token);
                if (flag == "\\L" || flag == "\\O" || flag == "\\T" || flag == "\\M") {
                    ++index;
                }
                continue;
            }
            target = token;
            break;
        }
        if (!target.empty() && !IsAllowedLinkTarget(target)) {
            return FieldKind::UnsafeHyperlink;
        }
    }
    return FieldKind::Ordinary;
}

bool ShouldStripField(FieldKind kind) {
    return kind != FieldKind::Ordinary;
}

void CountField(FieldKind kind, FeatureTally& tally) {
    if (kind == FieldKind::External) {
        tally.Add(Feature::ExternalContent);
    } else if (kind == FieldKind::Dde) {
        tally.Add(Feature::DdeField);
    }
}

const char* FieldCharType(const NsScope& scope, pugi::xml_node element) {
    const pugi::xml_attribute type = FindAttribute(scope, element, Ns::W, "fldCharType");
    return type ? type.value() : "";
}

// ---------------------------------------------------------------------------
// What docx-preview 0.4.1 draws. The feature counts report only content the
// reader cannot show, so text boxes, shapes, and equations are counted only
// where docx-preview would drop them. The evidence for each rule is in the
// comment above FeatureIsNotDisplayed() in DocCore.cpp.

// VML elements docx-preview turns into SVG (parseVmlElement). Anything else
// inside a w:pict is dropped together with its content.
bool IsDrawnVmlElement(const char* local) {
    return IsOneOf(local, {"shape", "rect", "oval", "line", "textbox"});
}

// VML shapes that come out as visible SVG geometry. v:shape becomes an empty
// <g>, so a v:shape without a text box or image draws nothing.
bool IsDrawnVmlGeometry(const char* local) {
    return IsOneOf(local, {"rect", "oval", "line"});
}

bool IsVmlShapeElement(const char* local) {
    return IsOneOf(
        local, {"shape", "rect", "roundrect", "oval", "line", "polyline", "arc", "curve"});
}

// OMML elements docx-preview maps to MathML (mmlTagMap), plus m:r. An
// element outside this list is dropped with everything inside it.
bool IsDrawnMathElement(const char* local) {
    return IsOneOf(
        local, {"oMath", "oMathPara", "f", "func", "fName", "num", "den", "rad", "deg",
                "e", "sSup", "sSub", "sPre", "sup", "sub", "d", "nary", "eqArr", "lim",
                "limLow", "m", "mr", "box", "bar", "groupChr", "r"});
}

// ---------------------------------------------------------------------------
// Analysis

class Analyzer final {
public:
    Analyzer(
        const ExternalIds& externalIds,
        FeatureTally& tally,
        std::vector<pugi::xml_node>& undrawnMath)
        : externalIds_(externalIds), tally_(tally), undrawnMath_(undrawnMath) {}

    void Run(pugi::xml_document& document) {
        if (pugi::xml_node root = document.document_element()) {
            Walk(root);
        }
        // An unterminated field still counts once its instruction is known.
        for (const Frame& frame : fields_) {
            if (!frame.evaluated) {
                CountField(ClassifyField(frame.instruction), tally_);
            }
        }
    }

private:
    struct Frame {
        std::string instruction;
        bool separated = false;
        bool evaluated = false;
    };

    bool HasChild(pugi::xml_node element, Ns ns, const char* local) {
        for (pugi::xml_node child = element.first_child(); child;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const bool match = scope_.Element(child).Is(ns, local);
            scope_.Pop(mark);
            if (match) {
                return true;
            }
        }
        return false;
    }

    bool HasDescendant(pugi::xml_node element, Ns ns, const char* local) {
        for (pugi::xml_node child = element.first_child(); child;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const bool match =
                scope_.Element(child).Is(ns, local) || HasDescendant(child, ns, local);
            scope_.Pop(mark);
            if (match) {
                return true;
            }
        }
        return false;
    }

    bool IsExternalReference(pugi::xml_node element, Ns ns, const char* local) {
        const pugi::xml_attribute attribute = FindAttribute(scope_, element, ns, local);
        return attribute && externalIds_.count(attribute.value()) > 0;
    }

    // The QName of the ancestor `up` levels above the current element (0 is
    // the parent), or an empty name.
    QName Ancestor(std::size_t up) const {
        if (ancestors_.size() < up + 2) {
            return {};
        }
        return ancestors_[ancestors_.size() - 2 - up];
    }

    // Counts the text boxes and shapes docx-preview draws from a VML subtree
    // whose root it parses (a w:pict child of a run). `drawn` tells whether
    // `element` itself is still on a drawn path.
    void CountDrawnVml(pugi::xml_node element, std::uint32_t& textBoxes, std::uint32_t& shapes) {
        for (pugi::xml_node child = element.first_child(); child;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const QName name = scope_.Element(child);
            if (name.ns == Ns::V && IsDrawnVmlElement(name.local)) {
                if (std::strcmp(name.local, "textbox") == 0) {
                    ++textBoxes;
                } else if (IsDrawnVmlGeometry(name.local) &&
                           !HasChild(child, Ns::V, "imagedata") &&
                           !HasChild(child, Ns::V, "textbox")) {
                    ++shapes;
                }
                CountDrawnVml(child, textBoxes, shapes);
            }
            scope_.Pop(mark);
        }
    }

    // After the mc:Choice of an mc:AlternateContent has been counted: inside a
    // run, docx-preview shows the mc:Fallback instead (it supports no
    // Requires namespace), so text boxes and shapes it draws from a VML
    // fallback are taken off the Choice's counts.
    void CreditFallback(pugi::xml_node alternate, std::uint32_t textBoxesBefore,
                        std::uint32_t shapesBefore) {
        if (!Ancestor(0).Is(Ns::W, "r")) {
            return;
        }
        pugi::xml_node fallback;
        for (pugi::xml_node child = alternate.first_child(); child && !fallback;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            if (scope_.Element(child).Is(Ns::Mc, "Fallback")) {
                fallback = child;
            }
            scope_.Pop(mark);
        }
        if (!fallback) {
            return;
        }
        const std::size_t mark = scope_.Mark();
        scope_.Push(fallback);
        pugi::xml_node first = fallback.first_child();
        while (first && first.type() != pugi::node_element) {
            first = first.next_sibling();
        }
        std::uint32_t textBoxes = 0;
        std::uint32_t shapes = 0;
        if (first) {
            scope_.Push(first);
            if (scope_.Element(first).Is(Ns::W, "pict")) {
                CountDrawnVml(first, textBoxes, shapes);
            }
        }
        scope_.Pop(mark);
        tally_.Credit(Feature::TextBox, textBoxesBefore, textBoxes);
        tally_.Credit(Feature::Shape, shapesBefore, shapes);
    }

    // docx-preview reads m:oMath and m:oMathPara only as children of a
    // paragraph, or of an inline content control in one. w:ins and w:moveTo
    // are unwrapped in the sanitized package.
    bool IsMathPlacementDrawn() const {
        std::size_t up = 0;
        const auto skipRevisions = [this, &up]() {
            while (Ancestor(up).Is(Ns::W, "ins") || Ancestor(up).Is(Ns::W, "moveTo")) {
                ++up;
            }
        };
        skipRevisions();
        if (Ancestor(up).Is(Ns::W, "sdtContent") && Ancestor(up + 1).Is(Ns::W, "sdt")) {
            up += 2;
            skipRevisions();
        }
        return Ancestor(up).Is(Ns::W, "p");
    }

    // True when docx-preview renders every part of this equation. Unknown
    // OMML elements lose their content, and some missing property or
    // argument elements make docx-preview throw, which stops the whole
    // document from rendering.
    bool IsMathDrawn(pugi::xml_node element) {
        bool drawn = true;
        bool hasE = false;
        bool hasSub = false;
        bool hasSup = false;
        bool hasDeg = false;
        bool degHidden = false;
        bool hasOwnProperties = false;
        const QName self = scope_.Element(element);
        const std::string ownProperties = std::string(self.local) + "Pr";
        for (pugi::xml_node child = element.first_child(); child && drawn;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const QName name = scope_.Element(child);
            if (name.ns == Ns::M) {
                if (ownProperties == name.local) {
                    hasOwnProperties = true;
                    if (self.Is(Ns::M, "rad")) {
                        if (pugi::xml_node hide = FindMathChild(child, "degHide")) {
                            // docx-preview reads only an explicit true value.
                            const std::size_t inner = scope_.Mark();
                            scope_.Push(hide);
                            const char* value =
                                FindAttribute(scope_, hide, Ns::M, "val").value();
                            degHidden = IsOneOf(value, {"1", "on", "true"});
                            scope_.Pop(inner);
                        }
                    }
                } else if (EndsWith(name.local, "Pr")) {
                    // Other property elements (ctrlPr, argPr) carry no text.
                } else if (!IsDrawnMathElement(name.local)) {
                    drawn = false;
                } else if (std::strcmp(name.local, "r") != 0) {
                    hasE = hasE || std::strcmp(name.local, "e") == 0;
                    hasSub = hasSub || std::strcmp(name.local, "sub") == 0;
                    hasSup = hasSup || std::strcmp(name.local, "sup") == 0;
                    hasDeg = hasDeg || std::strcmp(name.local, "deg") == 0;
                    drawn = IsMathDrawn(child);
                }
            }
            scope_.Pop(mark);
        }
        if (!drawn || self.ns != Ns::M) {
            return drawn;
        }
        const char* local = self.local;
        if (IsOneOf(local, {"d", "groupChr", "bar"})) {
            return hasOwnProperties;
        }
        if (std::strcmp(local, "nary") == 0) {
            return hasE && hasSub == hasSup;
        }
        if (std::strcmp(local, "sPre") == 0) {
            return hasE && hasSub && hasSup;
        }
        if (std::strcmp(local, "rad") == 0) {
            return hasE && (hasDeg || degHidden);
        }
        return true;
    }

    pugi::xml_node FindMathChild(pugi::xml_node element, const char* local) {
        for (pugi::xml_node child = element.first_child(); child;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const bool match = scope_.Element(child).Is(Ns::M, local);
            scope_.Pop(mark);
            if (match) {
                return child;
            }
        }
        return {};
    }

    void FieldCharacter(pugi::xml_node element) {
        const char* type = FieldCharType(scope_, element);
        if (std::strcmp(type, "begin") == 0) {
            fields_.push_back({});
        } else if (std::strcmp(type, "separate") == 0) {
            if (!fields_.empty() && !fields_.back().separated) {
                fields_.back().separated = true;
                fields_.back().evaluated = true;
                CountField(ClassifyField(fields_.back().instruction), tally_);
            }
        } else if (std::strcmp(type, "end") == 0) {
            if (!fields_.empty()) {
                if (!fields_.back().evaluated) {
                    CountField(ClassifyField(fields_.back().instruction), tally_);
                }
                fields_.pop_back();
            }
        }
    }

    void Walk(pugi::xml_node element) {
        const std::size_t mark = scope_.Mark();
        scope_.Push(element);
        const QName name = scope_.Element(element);
        ancestors_.push_back(name);
        bool descend = true;
        bool enteredMath = false;
        bool enteredObject = false;
        bool alternate = false;
        const std::uint32_t textBoxesBefore = tally_.Get(Feature::TextBox);
        const std::uint32_t shapesBefore = tally_.Get(Feature::Shape);

        // docx-preview parses a w:pict that is a child of a run, and inside it
        // only an unbroken chain of the VML elements it knows.
        const bool vmlDrawnBefore = vmlDrawn_;
        if (name.Is(Ns::W, "pict")) {
            vmlDrawn_ = Ancestor(0).Is(Ns::W, "r");
        } else if (name.ns == Ns::V) {
            vmlDrawn_ = vmlDrawn_ && IsDrawnVmlElement(name.local);
        } else {
            vmlDrawn_ = false;
        }

        switch (name.ns) {
            case Ns::W:
                if (IsTrackedChange(name.local)) {
                    tally_.Add(Feature::TrackedChanges);
                } else if (std::strcmp(name.local, "object") == 0) {
                    if (!HasDescendant(element, Ns::W, "control")) {
                        tally_.Add(Feature::OleObject);
                    }
                    enteredObject = true;
                } else if (std::strcmp(name.local, "control") == 0) {
                    tally_.Add(Feature::ActiveX);
                } else if (std::strcmp(name.local, "altChunk") == 0) {
                    tally_.Add(Feature::AltChunk);
                } else if (std::strcmp(name.local, "commentReference") == 0) {
                    ++tally_.commentReferences;
                } else if (std::strcmp(name.local, "fldChar") == 0) {
                    FieldCharacter(element);
                } else if (std::strcmp(name.local, "instrText") == 0) {
                    if (!fields_.empty() && !fields_.back().separated &&
                        fields_.back().instruction.size() < 4096) {
                        fields_.back().instruction += element.text().get();
                    }
                } else if (std::strcmp(name.local, "fldSimple") == 0) {
                    const pugi::xml_attribute instruction =
                        FindAttribute(scope_, element, Ns::W, "instr");
                    CountField(ClassifyField(instruction.value()), tally_);
                }
                break;
            case Ns::Mc:
                descend = std::strcmp(name.local, "Fallback") != 0;
                alternate = std::strcmp(name.local, "AlternateContent") == 0;
                break;
            case Ns::Wps:
                if (std::strcmp(name.local, "txbx") == 0) {
                    tally_.Add(Feature::TextBox);
                } else if (std::strcmp(name.local, "wsp") == 0 &&
                           !HasChild(element, Ns::Wps, "txbx")) {
                    tally_.Add(Feature::Shape);
                }
                break;
            case Ns::V:
                if (std::strcmp(name.local, "textbox") == 0) {
                    if (!vmlDrawn_) {
                        tally_.Add(Feature::TextBox);
                    }
                } else if (std::strcmp(name.local, "imagedata") == 0) {
                    if (FindAttribute(scope_, element, Ns::R, "link") ||
                        IsExternalReference(element, Ns::R, "id") ||
                        IsExternalReference(element, Ns::O, "relid")) {
                        tally_.Add(Feature::RemoteImage);
                    }
                } else if (objectDepth_ == 0 && IsVmlShapeElement(name.local) &&
                           !HasChild(element, Ns::V, "imagedata") &&
                           !HasChild(element, Ns::V, "textbox") &&
                           !(vmlDrawn_ && IsDrawnVmlGeometry(name.local))) {
                    tally_.Add(Feature::Shape);
                }
                break;
            case Ns::O:
                if (std::strcmp(name.local, "OLEObject") == 0 && objectDepth_ == 0) {
                    tally_.Add(Feature::OleObject);
                }
                break;
            case Ns::C:
            case Ns::Cx:
                if (std::strcmp(name.local, "chart") == 0) {
                    tally_.Add(Feature::Chart);
                }
                break;
            case Ns::Dgm:
                if (std::strcmp(name.local, "relIds") == 0) {
                    tally_.Add(Feature::SmartArt);
                }
                break;
            case Ns::Lc:
                // A locked canvas groups DrawingML shapes; docx-preview draws
                // only pictures from a:graphicData. Counted once, not per shape.
                if (std::strcmp(name.local, "lockedCanvas") == 0) {
                    tally_.Add(Feature::Shape);
                    descend = false;
                }
                break;
            case Ns::M:
                if (std::strcmp(name.local, "oMath") == 0 ||
                    std::strcmp(name.local, "oMathPara") == 0) {
                    if (mathDepth_ == 0 &&
                        !(IsMathPlacementDrawn() && IsMathDrawn(element))) {
                        tally_.Add(Feature::Equation);
                        undrawnMath_.push_back(element);
                    }
                    enteredMath = true;
                }
                break;
            case Ns::A:
                if (std::strcmp(name.local, "blip") == 0 &&
                    (FindAttribute(scope_, element, Ns::R, "link") ||
                     IsExternalReference(element, Ns::R, "embed"))) {
                    tally_.Add(Feature::RemoteImage);
                }
                break;
            default:
                break;
        }

        if (descend) {
            mathDepth_ += enteredMath ? 1 : 0;
            objectDepth_ += enteredObject ? 1 : 0;
            for (pugi::xml_node child = element.first_child(); child;
                 child = child.next_sibling()) {
                if (child.type() == pugi::node_element) {
                    Walk(child);
                }
            }
            mathDepth_ -= enteredMath ? 1 : 0;
            objectDepth_ -= enteredObject ? 1 : 0;
        }
        if (alternate) {
            CreditFallback(element, textBoxesBefore, shapesBefore);
        }
        vmlDrawn_ = vmlDrawnBefore;
        ancestors_.pop_back();
        scope_.Pop(mark);
    }

    const ExternalIds& externalIds_;
    FeatureTally& tally_;
    std::vector<pugi::xml_node>& undrawnMath_;
    NsScope scope_;
    std::vector<Frame> fields_;
    std::vector<QName> ancestors_;  // the current element is last
    int mathDepth_ = 0;
    int objectDepth_ = 0;
    bool vmlDrawn_ = false;
};

// ---------------------------------------------------------------------------
// Sanitizing

class Sanitizer final {
public:
    Sanitizer(
        const std::unordered_set<std::string>& keptIds,
        std::unordered_set<std::string>& usedIds)
        : keptIds_(keptIds), usedIds_(usedIds) {}

    void Run(pugi::xml_document& document) {
        pugi::xml_node root = document.document_element();
        if (!root) {
            return;
        }
        const std::size_t mark = scope_.Mark();
        scope_.Push(root);
        Process(root, scope_.Element(root));
        scope_.Pop(mark);
    }

private:
    enum class Action { Keep, Remove, Unwrap, Alternate };

    bool IsKept(const pugi::xml_attribute& attribute) const {
        return attribute && keptIds_.count(attribute.value()) > 0;
    }

    // True when a descendant of `element` is forbidden content.
    bool ContainsForbidden(pugi::xml_node element) {
        const std::size_t mark = scope_.Mark();
        scope_.Push(element);
        bool found = false;
        for (pugi::xml_node child = element.first_child(); child && !found;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t inner = scope_.Mark();
            scope_.Push(child);
            found = IsForbiddenContent(scope_.Element(child));
            scope_.Pop(inner);
            found = found || ContainsForbidden(child);
        }
        scope_.Pop(mark);
        return found;
    }

    bool RowIsDeleted(pugi::xml_node row) {
        for (pugi::xml_node child = row.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            bool deleted = false;
            if (scope_.Element(child).Is(Ns::W, "trPr")) {
                for (pugi::xml_node marker = child.first_child(); marker;
                     marker = marker.next_sibling()) {
                    if (marker.type() != pugi::node_element) {
                        continue;
                    }
                    const std::size_t inner = scope_.Mark();
                    scope_.Push(marker);
                    deleted = deleted || scope_.Element(marker).Is(Ns::W, "del");
                    scope_.Pop(inner);
                }
            }
            scope_.Pop(mark);
            if (deleted) {
                return true;
            }
        }
        return false;
    }

    // The first element child of `element` named w:`local`, or none.
    pugi::xml_node WordChild(pugi::xml_node element, const char* local) {
        for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const bool found = scope_.Element(child).Is(Ns::W, local);
            scope_.Pop(mark);
            if (found) {
                return child;
            }
        }
        return {};
    }

    // True when `element` holds nothing that shows once tracked changes are
    // accepted: deleted content, empty runs, and markers only.
    bool HoldsNothing(pugi::xml_node element) {
        for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const QName name = scope_.Element(child);
            bool nothing = false;
            if (name.ns == Ns::W) {
                if (IsOneOf(name.local, {"pPr", "rPr", "del", "moveFrom", "proofErr",
                                         "bookmarkEnd", "lastRenderedPageBreak"}) ||
                    IsRemovedWordElement(name.local)) {
                    nothing = true;
                } else if (IsOneOf(name.local, {"r", "ins", "moveTo"})) {
                    nothing = HoldsNothing(child);
                }
            }
            scope_.Pop(mark);
            if (!nothing) {
                return false;
            }
        }
        return true;
    }

    // Accepting a deleted paragraph mark joins the paragraph to the next one.
    // When nothing of the paragraph is left, nothing of it shows before the
    // block that follows, so it goes; LeanDoc showed each one as an empty
    // line. A paragraph that keeps content, ends its container, or ends a
    // section stays.
    bool ParagraphIsJoined(pugi::xml_node paragraph) {
        pugi::xml_node pPr = WordChild(paragraph, "pPr");
        if (!pPr) {
            return false;
        }
        const std::size_t mark = scope_.Mark();
        scope_.Push(pPr);
        pugi::xml_node rPr = WordChild(pPr, "rPr");
        bool deleted = false;
        if (rPr && !WordChild(pPr, "sectPr")) {
            scope_.Push(rPr);
            deleted = static_cast<bool>(WordChild(rPr, "del"));
        }
        scope_.Pop(mark);
        if (!deleted || !HoldsNothing(paragraph)) {
            return false;
        }
        pugi::xml_node next = paragraph.next_sibling();
        while (next && next.type() != pugi::node_element) {
            next = next.next_sibling();
        }
        if (!next) {
            return false;
        }
        const std::size_t inner = scope_.Mark();
        scope_.Push(next);
        const QName nextName = scope_.Element(next);
        const bool joins = nextName.Is(Ns::W, "p") || nextName.Is(Ns::W, "tbl") ||
                           nextName.Is(Ns::W, "sdt");
        scope_.Pop(inner);
        return joins;
    }

    Action Decide(pugi::xml_node element, const QName& name, const QName& parent) {
        switch (name.ns) {
            case Ns::W: {
                const char* local = name.local;
                if (std::strcmp(local, "ins") == 0 || std::strcmp(local, "moveTo") == 0) {
                    // Inside rPr or trPr these are markers, not containers.
                    return EndsWith(parent.local, "Pr") ? Action::Remove : Action::Unwrap;
                }
                if (IsRemovedWordElement(local)) {
                    return Action::Remove;
                }
                if (std::strcmp(local, "fldSimple") == 0) {
                    // Every simple field becomes its cached result runs. For
                    // external and DDE fields this drops the instruction;
                    // for the rest it is what the reader can show, because
                    // docx-preview 0.4.1 drops w:fldSimple with its content.
                    return Action::Unwrap;
                }
                if (std::strcmp(local, "hyperlink") == 0) {
                    const pugi::xml_attribute id = FindAttribute(scope_, element, Ns::R, "id");
                    if (id && !IsKept(id) &&
                        !FindAttribute(scope_, element, Ns::W, "anchor")) {
                        return Action::Unwrap;
                    }
                    return Action::Keep;
                }
                if (IsOneOf(local, {"headerReference", "footerReference",
                                    "printerSettings", "recipientData"})) {
                    return IsKept(FindAttribute(scope_, element, Ns::R, "id"))
                               ? Action::Keep
                               : Action::Remove;
                }
                if (std::strcmp(local, "drawing") == 0 && ContainsForbidden(element)) {
                    return Action::Remove;
                }
                if (std::strcmp(local, "tr") == 0 && RowIsDeleted(element)) {
                    return Action::Remove;
                }
                if (std::strcmp(local, "p") == 0 && ParagraphIsJoined(element)) {
                    return Action::Remove;
                }
                return Action::Keep;
            }
            case Ns::Mc:
                return std::strcmp(name.local, "AlternateContent") == 0
                           ? Action::Alternate
                           : Action::Keep;
            case Ns::W14:
                return std::strcmp(name.local, "contentPart") == 0 ? Action::Remove
                                                                    : Action::Keep;
            case Ns::O:
                return std::strcmp(name.local, "OLEObject") == 0 ? Action::Remove
                                                                  : Action::Keep;
            case Ns::Asvg:
                // SVG parts are never copied; Word keeps a PNG fallback.
                return std::strcmp(name.local, "svgBlip") == 0 ? Action::Remove
                                                                : Action::Keep;
            case Ns::A:
                if (IsOneOf(name.local, {"hlinkClick", "hlinkHover", "hlinkMouseOver"})) {
                    return IsKept(FindAttribute(scope_, element, Ns::R, "id"))
                               ? Action::Keep
                               : Action::Remove;
                }
                return Action::Keep;
            case Ns::C:
            case Ns::Cx:
            case Ns::Dgm:
                return IsForbiddenContent(name) ? Action::Remove : Action::Keep;
            default:
                return Action::Keep;
        }
    }

    void FixAttributes(pugi::xml_node element, const QName& name) {
        pugi::xml_attribute attribute = element.first_attribute();
        while (attribute) {
            pugi::xml_attribute next = attribute.next_attribute();
            const QName attributeName = scope_.Attribute(attribute);
            bool remove = false;
            if (attributeName.ns == Ns::R ||
                (attributeName.ns == Ns::O && std::strcmp(attributeName.local, "relid") == 0)) {
                if (attributeName.ns == Ns::R && std::strcmp(attributeName.local, "link") == 0) {
                    remove = true;
                } else if (keptIds_.count(attribute.value()) > 0) {
                    usedIds_.insert(attribute.value());
                } else {
                    remove = true;
                }
            } else if (name.ns == Ns::V && attributeName.ns == Ns::None &&
                       std::strcmp(attributeName.local, "href") == 0 &&
                       !IsAllowedLinkTarget(attribute.value())) {
                remove = true;
            }
            if (remove) {
                element.remove_attribute(attribute);
            }
            attribute = next;
        }
    }

    // PAGE, NUMPAGES, and SECTIONPAGES differ on every page, but docx-preview
    // shows a field's cached result, which Word saved once. A simple field
    // of these kinds keeps its instruction as a complex field, so the reader
    // can number each page. Only the keyword is written, never the original
    // instruction text; switches are dropped.
    const char* PageFieldKeyword(pugi::xml_node field) {
        const pugi::xml_attribute instruction = FindAttribute(scope_, field, Ns::W, "instr");
        if (!instruction) {
            return nullptr;
        }
        std::string text = instruction.value();
        const std::size_t start = text.find_first_not_of(" \t");
        if (start == std::string::npos) {
            return nullptr;
        }
        const std::size_t end = text.find_first_of(" \t\\", start);
        std::string keyword = text.substr(start, end == std::string::npos ? end : end - start);
        for (char& character : keyword) {
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 'a' + 'A');
            }
        }
        for (const char* known : {"PAGE", "NUMPAGES", "SECTIONPAGES"}) {
            if (keyword == known) {
                return known;
            }
        }
        return nullptr;
    }

    pugi::xml_node UnwrapPageField(pugi::xml_node parent, pugi::xml_node field,
                                   const char* keyword) {
        const std::string fieldName = field.name();
        const std::size_t colon = fieldName.find(':');
        const std::string prefix =
            colon == std::string::npos ? std::string() : fieldName.substr(0, colon + 1);
        std::vector<pugi::xml_attribute> declarations;
        for (pugi::xml_attribute attribute = field.first_attribute(); attribute;
             attribute = attribute.next_attribute()) {
            if (std::strncmp(attribute.name(), "xmlns", 5) == 0) {
                declarations.push_back(attribute);
            }
        }
        auto run = [&](pugi::xml_node before) {
            pugi::xml_node created = before ? parent.insert_child_before(pugi::node_element, before)
                                            : parent.append_child(pugi::node_element);
            created.set_name((prefix + "r").c_str());
            for (const pugi::xml_attribute& declaration : declarations) {
                created.append_attribute(declaration.name()) = declaration.value();
            }
            return created;
        };
        auto fieldCharacter = [&](pugi::xml_node before, const char* type) {
            pugi::xml_node character = run(before).append_child((prefix + "fldChar").c_str());
            character.append_attribute((prefix + "fldCharType").c_str()) = type;
        };
        fieldCharacter(field, "begin");
        pugi::xml_node instruction = run(field).append_child((prefix + "instrText").c_str());
        instruction.append_attribute("xml:space") = "preserve";
        instruction.text().set((std::string(" ") + keyword + " ").c_str());
        fieldCharacter(field, "separate");
        const pugi::xml_node after = field.next_sibling();
        const pugi::xml_node first = Unwrap(parent, field);
        fieldCharacter(after, "end");
        return first;
    }

    // Moves the element's children in front of it and removes it. Namespace
    // declarations on the element are copied to the moved children so their
    // prefixes still resolve. Returns the first moved node, or the next
    // sibling when there was nothing to move.
    pugi::xml_node Unwrap(pugi::xml_node parent, pugi::xml_node element) {
        pugi::xml_node next = element.next_sibling();
        pugi::xml_node first = element.first_child();
        std::vector<pugi::xml_attribute> declarations;
        for (pugi::xml_attribute attribute = element.first_attribute(); attribute;
             attribute = attribute.next_attribute()) {
            if (std::strncmp(attribute.name(), "xmlns", 5) == 0) {
                declarations.push_back(attribute);
            }
        }
        while (pugi::xml_node child = element.first_child()) {
            if (child.type() == pugi::node_element) {
                for (const pugi::xml_attribute& declaration : declarations) {
                    if (!child.attribute(declaration.name())) {
                        child.append_attribute(declaration.name()) = declaration.value();
                    }
                }
            }
            parent.insert_move_before(child, element);
        }
        parent.remove_child(element);
        return first ? first : next;
    }

    // Returns true when `element` is a run that lost all of its content.
    bool Process(pugi::xml_node element, const QName& name) {
        FixAttributes(element, name);
        bool removedChild = false;
        pugi::xml_node child = element.first_child();
        while (child) {
            if (child.type() != pugi::node_element) {
                child = child.next_sibling();
                continue;
            }
            pugi::xml_node next = child.next_sibling();
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const QName childName = scope_.Element(child);
            Action action = Decide(child, childName, name);

            if (action == Action::Alternate) {
                pugi::xml_node choice;
                pugi::xml_node fallback;
                for (pugi::xml_node part = child.first_child(); part;
                     part = part.next_sibling()) {
                    if (part.type() != pugi::node_element) {
                        continue;
                    }
                    const std::size_t inner = scope_.Mark();
                    scope_.Push(part);
                    const QName partName = scope_.Element(part);
                    if (!choice && partName.Is(Ns::Mc, "Choice")) {
                        choice = part;
                    } else if (partName.Is(Ns::Mc, "Fallback")) {
                        fallback = part;
                    }
                    scope_.Pop(inner);
                }
                if (choice && !ContainsForbidden(choice)) {
                    action = Action::Keep;
                } else if (fallback && !ContainsForbidden(fallback)) {
                    // Replace the whole element with the fallback content.
                    // Unwrap carries namespace declarations from both levels.
                    scope_.Pop(mark);
                    pugi::xml_node part = child.first_child();
                    while (part) {
                        pugi::xml_node following = part.next_sibling();
                        if (part != fallback) {
                            child.remove_child(part);
                        }
                        part = following;
                    }
                    Unwrap(child, fallback);
                    child = Unwrap(element, child);
                    removedChild = true;
                    continue;
                } else {
                    action = Action::Remove;
                }
            }

            switch (action) {
                case Action::Remove:
                    scope_.Pop(mark);
                    element.remove_child(child);
                    removedChild = true;
                    child = next;
                    break;
                case Action::Unwrap: {
                    const char* pageField =
                        childName.Is(Ns::W, "fldSimple") ? PageFieldKeyword(child) : nullptr;
                    scope_.Pop(mark);
                    if (pageField != nullptr) {
                        child = UnwrapPageField(element, child, pageField);
                    } else {
                        child = Unwrap(element, child);
                    }
                    removedChild = true;
                    break;
                }
                case Action::Keep:
                case Action::Alternate: {
                    const bool emptied = Process(child, childName);
                    scope_.Pop(mark);
                    if (emptied) {
                        element.remove_child(child);
                        removedChild = true;
                    }
                    child = next;
                    break;
                }
            }
        }
        if (!removedChild) {
            return false;
        }
        // An extension list emptied by removing svgBlip goes too.
        if (name.Is(Ns::A, "ext") || name.Is(Ns::A, "extLst")) {
            for (pugi::xml_node rest = element.first_child(); rest; rest = rest.next_sibling()) {
                if (rest.type() == pugi::node_element) {
                    return false;
                }
            }
            return true;
        }
        if (!name.Is(Ns::W, "r")) {
            return false;
        }
        for (pugi::xml_node rest = element.first_child(); rest; rest = rest.next_sibling()) {
            if (rest.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(rest);
            const bool properties = scope_.Element(rest).Is(Ns::W, "rPr");
            scope_.Pop(mark);
            if (!properties) {
                return false;
            }
        }
        return true;
    }

    const std::unordered_set<std::string>& keptIds_;
    std::unordered_set<std::string>& usedIds_;
    NsScope scope_;
};

// Removes the instruction of external, DDE, and unsafe hyperlink fields and
// keeps their cached result. Runs after Sanitizer, so deleted runs are gone.
class FieldStripper final {
public:
    void Run(pugi::xml_document& document) {
        if (pugi::xml_node root = document.document_element()) {
            Walk(root);
        }
        // A field that never ends loses its instruction too.
        while (!frames_.empty()) {
            Frame frame = std::move(frames_.back());
            frames_.pop_back();
            if (frame.instructionOpen) {
                frame.strip = ShouldStripField(ClassifyField(frame.instruction));
            }
            if (frame.strip) {
                StripFrame(frame);
            }
        }
        Apply();
    }

private:
    struct Tracked {
        std::uint64_t order;
        pugi::xml_node node;
    };

    struct Frame {
        std::string instruction;
        bool instructionOpen = true;
        bool strip = false;
        Tracked beginRun{};
        Tracked beginChar{};
        Tracked endRun{};
        Tracked endChar{};
        std::vector<Tracked> instructionRuns;  // removed whole when stripped
        std::vector<Tracked> allRuns;          // every run after the begin run
        std::vector<Tracked> instructionTexts;
    };

    void Record(Frame& frame, const Tracked& run) {
        frame.allRuns.push_back(run);
        if (frame.instructionOpen) {
            frame.instructionRuns.push_back(run);
        }
    }

    void StripFrame(const Frame& frame) {
        for (const Tracked& run : frame.instructionRuns) {
            removeWhole_.push_back(run);
        }
        for (const Tracked& text : frame.instructionTexts) {
            removeWhole_.push_back(text);
        }
        if (frame.beginChar.node) {
            removeWhole_.push_back(frame.beginChar);
            emptyCheck_.push_back(frame.beginRun);
        }
        if (frame.endChar.node) {
            removeWhole_.push_back(frame.endChar);
            emptyCheck_.push_back(frame.endRun);
        }
    }

    void FieldCharacter(pugi::xml_node element) {
        const char* type = FieldCharType(scope_, element);
        const Tracked self{order_, element};
        if (std::strcmp(type, "begin") == 0) {
            Frame frame;
            frame.beginRun = currentRun_;
            frame.beginChar = self;
            frames_.push_back(std::move(frame));
        } else if (std::strcmp(type, "separate") == 0) {
            if (!frames_.empty() && frames_.back().instructionOpen) {
                Frame& frame = frames_.back();
                frame.instructionOpen = false;
                frame.strip = ShouldStripField(ClassifyField(frame.instruction));
            }
        } else if (std::strcmp(type, "end") == 0) {
            if (frames_.empty()) {
                return;
            }
            Frame frame = std::move(frames_.back());
            frames_.pop_back();
            if (frame.instructionOpen) {
                frame.instructionOpen = false;
                frame.strip = ShouldStripField(ClassifyField(frame.instruction));
            }
            frame.endRun = currentRun_;
            frame.endChar = self;
            if (frame.strip) {
                StripFrame(frame);
            }
            // A field nested in an outer instruction belongs to that
            // instruction: if the outer field is stripped, so is this one.
            if (!frames_.empty()) {
                Frame& outer = frames_.back();
                for (const Tracked& run : frame.allRuns) {
                    Record(outer, run);
                }
            }
        }
    }

    void Walk(pugi::xml_node element) {
        ++order_;
        const std::size_t mark = scope_.Mark();
        scope_.Push(element);
        const QName name = scope_.Element(element);
        const Tracked previousRun = currentRun_;
        if (name.Is(Ns::W, "r")) {
            currentRun_ = {order_, element};
            if (!frames_.empty()) {
                Record(frames_.back(), currentRun_);
            }
        } else if (name.Is(Ns::W, "fldChar")) {
            FieldCharacter(element);
        } else if (name.Is(Ns::W, "instrText")) {
            if (!frames_.empty() && frames_.back().instructionOpen) {
                Frame& frame = frames_.back();
                if (frame.instruction.size() < 4096) {
                    frame.instruction += element.text().get();
                }
                frame.instructionTexts.push_back({order_, element});
            }
        }
        for (pugi::xml_node child = element.first_child(); child;
             child = child.next_sibling()) {
            if (child.type() == pugi::node_element) {
                Walk(child);
            }
        }
        currentRun_ = previousRun;
        scope_.Pop(mark);
    }

    static bool OnlyRunProperties(pugi::xml_node run) {
        for (pugi::xml_node child = run.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const char* name = child.name();
            const char* colon = std::strchr(name, ':');
            if (std::strcmp(colon ? colon + 1 : name, "rPr") != 0) {
                return false;
            }
        }
        return true;
    }

    // Removals run from the deepest, latest node backwards, so a node is
    // never touched after one of its ancestors has been freed.
    void Apply() {
        struct Step {
            std::uint64_t order;
            pugi::xml_node node;
            bool onlyIfEmpty;
        };
        std::vector<Step> steps;
        std::unordered_set<const void*> whole;
        for (const Tracked& item : removeWhole_) {
            if (item.node && whole.insert(item.node.internal_object()).second) {
                steps.push_back({item.order, item.node, false});
            }
        }
        std::unordered_set<const void*> checked;
        for (const Tracked& item : emptyCheck_) {
            if (item.node && whole.count(item.node.internal_object()) == 0 &&
                checked.insert(item.node.internal_object()).second) {
                steps.push_back({item.order, item.node, true});
            }
        }
        std::sort(steps.begin(), steps.end(), [](const Step& left, const Step& right) {
            return left.order > right.order;
        });
        for (const Step& step : steps) {
            if (step.onlyIfEmpty && !OnlyRunProperties(step.node)) {
                continue;
            }
            step.node.parent().remove_child(step.node);
        }
    }

    NsScope scope_;
    std::vector<Frame> frames_;
    Tracked currentRun_{};
    std::uint64_t order_ = 0;
    std::vector<Tracked> removeWhole_;
    std::vector<Tracked> emptyCheck_;
};

// ---------------------------------------------------------------------------
// Outline

int ParseLevel(const char* value) {
    if (value == nullptr || value[0] < '0' || value[0] > '9') {
        return -1;
    }
    int level = 0;
    for (const char* digit = value; *digit >= '0' && *digit <= '9'; ++digit) {
        level = level * 10 + (*digit - '0');
        if (level > 100) {
            return -1;
        }
    }
    return level;
}

// Built-in heading styles are named "heading 1" to "heading 9" whatever the
// display language.
int HeadingLevelFromName(std::string_view name) {
    if (name.size() != 9) {
        return -1;
    }
    static constexpr char kPrefix[] = "heading ";
    for (std::size_t index = 0; index < 8; ++index) {
        char character = name[index];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != kPrefix[index]) {
            return -1;
        }
    }
    return name[8] >= '1' && name[8] <= '9' ? name[8] - '1' : -1;
}

struct ParagraphProperties {
    std::string style;
    int outlineLevel = -1;
    int bidi = -1;
};

ParagraphProperties ReadParagraphProperties(NsScope& scope, pugi::xml_node pPr) {
    ParagraphProperties properties;
    for (pugi::xml_node child = pPr.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::size_t mark = scope.Mark();
        scope.Push(child);
        const QName name = scope.Element(child);
        if (name.ns == Ns::W) {
            const pugi::xml_attribute value = FindAttribute(scope, child, Ns::W, "val");
            if (std::strcmp(name.local, "pStyle") == 0) {
                properties.style = value.value();
            } else if (std::strcmp(name.local, "outlineLvl") == 0) {
                properties.outlineLevel = ParseLevel(value.value());
            } else if (std::strcmp(name.local, "bidi") == 0) {
                properties.bidi = IsOn(value ? value.value() : nullptr) ? 1 : 0;
            }
        }
        scope.Pop(mark);
    }
    return properties;
}

pugi::xml_node FindChild(NsScope& scope, pugi::xml_node element, Ns ns, const char* local) {
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::size_t mark = scope.Mark();
        scope.Push(child);
        const bool match = scope.Element(child).Is(ns, local);
        scope.Pop(mark);
        if (match) {
            return child;
        }
    }
    return {};
}

class OutlineBuilder final {
public:
    explicit OutlineBuilder(const StyleTable& styles) : styles_(styles) {}

    OutlineResult Run(pugi::xml_document& document) {
        pugi::xml_node root = document.document_element();
        if (!root) {
            return std::move(result_);
        }
        scope_.Push(root);
        if (!scope_.Element(root).Is(Ns::W, "document")) {
            return std::move(result_);
        }
        pugi::xml_node body = FindChild(scope_, root, Ns::W, "body");
        if (body) {
            scope_.Push(body);
            WalkBlocks(body);
        }
        result_.rightToLeft = DefaultIsBidi() || firstParagraphBidi_;
        return std::move(result_);
    }

private:
    // Follows the style chain with a cycle guard.
    template <typename Pick>
    int FromStyleChain(const std::string& styleId, Pick pick) const {
        std::string current = styleId.empty() ? styles_.defaultParagraphStyle : styleId;
        for (int steps = 0; steps < 32 && !current.empty(); ++steps) {
            const auto found = styles_.paragraphStyles.find(current);
            if (found == styles_.paragraphStyles.end()) {
                break;
            }
            const int value = pick(found->second);
            if (value >= 0) {
                return value;
            }
            current = found->second.basedOn;
        }
        return -1;
    }

    bool DefaultIsBidi() const {
        const int fromStyle = FromStyleChain(
            styles_.defaultParagraphStyle,
            [](const StyleTable::Style& style) { return style.bidi; });
        if (fromStyle >= 0) {
            return fromStyle == 1;
        }
        return styles_.defaultBidi == 1;
    }

    void WalkBlocks(pugi::xml_node element) {
        for (pugi::xml_node child = element.first_child(); child;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const QName name = scope_.Element(child);
            if (name.Is(Ns::W, "p")) {
                Paragraph(child);
            } else if (!name.Is(Ns::W, "txbxContent") && !name.Is(Ns::Mc, "Fallback")) {
                WalkBlocks(child);
            }
            scope_.Pop(mark);
        }
    }

    void CollectText(pugi::xml_node element, std::string& text) {
        for (pugi::xml_node child = element.first_child(); child;
             child = child.next_sibling()) {
            if (child.type() != pugi::node_element) {
                continue;
            }
            const std::size_t mark = scope_.Mark();
            scope_.Push(child);
            const QName name = scope_.Element(child);
            if (name.ns == Ns::W) {
                if (std::strcmp(name.local, "t") == 0) {
                    text += child.text().get();
                } else if (IsOneOf(name.local, {"tab", "br", "cr"})) {
                    text += ' ';
                } else if (std::strcmp(name.local, "noBreakHyphen") == 0) {
                    text += '-';
                } else if (std::strcmp(name.local, "p") == 0) {
                    // A paragraph nested in this one (a text box): not ours.
                } else if (!IsOneOf(name.local, {"txbxContent", "del", "moveFrom",
                                                 "delText", "instrText", "pPr"})) {
                    CollectText(child, text);
                }
            } else if (!name.Is(Ns::Mc, "Fallback")) {
                CollectText(child, text);
            }
            scope_.Pop(mark);
            if (text.size() > 4096) {
                return;
            }
        }
    }

    void Paragraph(pugi::xml_node paragraph) {
        const std::uint32_t index = paragraphIndex_++;
        ParagraphProperties direct;
        if (pugi::xml_node pPr = FindChild(scope_, paragraph, Ns::W, "pPr")) {
            const std::size_t mark = scope_.Mark();
            scope_.Push(pPr);
            direct = ReadParagraphProperties(scope_, pPr);
            scope_.Pop(mark);
        }

        int level = direct.outlineLevel;
        if (level < 0) {
            level = FromStyleChain(direct.style, [](const StyleTable::Style& style) {
                return style.outlineLevel;
            });
        }
        if (level < 0) {
            level = FromStyleChain(direct.style, [](const StyleTable::Style& style) {
                return style.headingLevel;
            });
        }

        const bool needBidi = !firstParagraphFound_;
        if (level < 0 || level > 8) {
            if (!needBidi) {
                return;
            }
        }
        std::string raw;
        CollectText(paragraph, raw);
        std::string text = CleanText(raw, 200);

        if (needBidi && !text.empty()) {
            firstParagraphFound_ = true;
            int bidi = direct.bidi;
            if (bidi < 0) {
                bidi = FromStyleChain(direct.style, [](const StyleTable::Style& style) {
                    return style.bidi;
                });
            }
            if (bidi < 0) {
                bidi = styles_.defaultBidi;
            }
            firstParagraphBidi_ = bidi == 1;
        }
        if (level >= 0 && level <= 8 && !text.empty()) {
            OutlineEntry entry;
            entry.level = static_cast<unsigned>(level) + 1;
            entry.text = std::move(text);
            entry.paragraphIndex = index;
            result_.entries.push_back(std::move(entry));
        }
    }

    const StyleTable& styles_;
    NsScope scope_;
    OutlineResult result_;
    std::uint32_t paragraphIndex_ = 0;
    bool firstParagraphFound_ = false;
    bool firstParagraphBidi_ = false;
};

std::uint32_t CountIn(NsScope& scope, pugi::xml_node element, Ns ns, const char* local) {
    std::uint32_t count = 0;
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::size_t mark = scope.Mark();
        scope.Push(child);
        if (scope.Element(child).Is(ns, local)) {
            ++count;
        }
        count += CountIn(scope, child, ns, local);
        scope.Pop(mark);
    }
    return count;
}

}  // namespace

constexpr const char* kUndrawnMathNamespace =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

void AnalyzeStory(
    pugi::xml_document& document,
    const ExternalIds& externalIds,
    FeatureTally& tally) {
    std::vector<pugi::xml_node> undrawnMath;
    Analyzer analyzer(externalIds, tally, undrawnMath);
    analyzer.Run(document);
    // An equation the reader cannot draw is reported in the fidelity bar and
    // replaced by a visible marker. Some malformed OMML makes docx-preview throw,
    // which would otherwise blank the whole document. The run declares its own
    // namespace so it is valid whatever prefix the part uses.
    for (pugi::xml_node math : undrawnMath) {
        pugi::xml_node parent = math.parent();
        if (!parent) {
            continue;
        }
        pugi::xml_node run = parent.insert_child_before("w:r", math);
        run.append_attribute("xmlns:w") = kUndrawnMathNamespace;
        run.append_child("w:t").text() = "[Equation]";
        parent.remove_child(math);
    }
}

std::uint32_t CountElements(pugi::xml_document& document, Ns ns, const char* local) {
    NsScope scope;
    return CountIn(scope, document, ns, local);
}

std::string AttachedTemplateId(pugi::xml_document& settings) {
    NsScope scope;
    pugi::xml_node root = settings.document_element();
    if (!root) {
        return {};
    }
    scope.Push(root);
    pugi::xml_node attached = FindChild(scope, root, Ns::W, "attachedTemplate");
    if (!attached) {
        return {};
    }
    scope.Push(attached);
    return FindAttribute(scope, attached, Ns::R, "id").value();
}

void SanitizePart(
    pugi::xml_document& document,
    const std::unordered_set<std::string>& keptIds,
    std::unordered_set<std::string>& usedIds) {
    Sanitizer sanitizer(keptIds, usedIds);
    sanitizer.Run(document);
    FieldStripper stripper;
    stripper.Run(document);
}

bool IsAllowedLinkTarget(std::string_view target) {
    std::size_t colon = target.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        return false;
    }
    std::string scheme;
    for (std::size_t index = 0; index < colon; ++index) {
        char character = target[index];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (!((character >= 'a' && character <= 'z') ||
              (index > 0 && ((character >= '0' && character <= '9') ||
                             character == '+' || character == '-' || character == '.')))) {
            return false;
        }
        scheme += character;
    }
    return scheme == "http" || scheme == "https" || scheme == "mailto";
}

StyleTable ReadStyles(pugi::xml_document* styles) {
    StyleTable table;
    if (styles == nullptr) {
        return table;
    }
    NsScope scope;
    pugi::xml_node root = styles->document_element();
    if (!root) {
        return table;
    }
    scope.Push(root);
    for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::size_t mark = scope.Mark();
        scope.Push(child);
        const QName name = scope.Element(child);
        if (name.Is(Ns::W, "docDefaults")) {
            if (pugi::xml_node pPrDefault = FindChild(scope, child, Ns::W, "pPrDefault")) {
                const std::size_t inner = scope.Mark();
                scope.Push(pPrDefault);
                if (pugi::xml_node pPr = FindChild(scope, pPrDefault, Ns::W, "pPr")) {
                    scope.Push(pPr);
                    table.defaultBidi = ReadParagraphProperties(scope, pPr).bidi;
                }
                scope.Pop(inner);
            }
        } else if (name.Is(Ns::W, "style")) {
            const char* type = FindAttribute(scope, child, Ns::W, "type").value();
            const std::string id = FindAttribute(scope, child, Ns::W, "styleId").value();
            if (std::strcmp(type, "paragraph") == 0 && !id.empty()) {
                StyleTable::Style style;
                for (pugi::xml_node part = child.first_child(); part;
                     part = part.next_sibling()) {
                    if (part.type() != pugi::node_element) {
                        continue;
                    }
                    const std::size_t inner = scope.Mark();
                    scope.Push(part);
                    const QName partName = scope.Element(part);
                    const char* value = FindAttribute(scope, part, Ns::W, "val").value();
                    if (partName.Is(Ns::W, "basedOn")) {
                        style.basedOn = value;
                    } else if (partName.Is(Ns::W, "name")) {
                        style.headingLevel = HeadingLevelFromName(value);
                    } else if (partName.Is(Ns::W, "pPr")) {
                        const ParagraphProperties properties =
                            ReadParagraphProperties(scope, part);
                        style.outlineLevel = properties.outlineLevel;
                        style.bidi = properties.bidi;
                    }
                    scope.Pop(inner);
                }
                const pugi::xml_attribute isDefault =
                    FindAttribute(scope, child, Ns::W, "default");
                if (isDefault && IsOn(isDefault.value()) &&
                    table.defaultParagraphStyle.empty()) {
                    table.defaultParagraphStyle = id;
                }
                table.paragraphStyles.emplace(id, std::move(style));
            }
        }
        scope.Pop(mark);
    }
    return table;
}

OutlineResult BuildOutline(pugi::xml_document& document, const StyleTable& styles) {
    OutlineBuilder builder(styles);
    return builder.Run(document);
}

std::string CleanText(std::string_view text, std::size_t maximumBytes) {
    std::string output;
    output.reserve(std::min(text.size(), maximumBytes + 4));
    bool pendingSpace = false;
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first == ' ' || first == '\t' || first == '\n' || first == '\r' ||
            first == '\f' || first == '\v') {
            pendingSpace = !output.empty();
            ++index;
            continue;
        }
        // Decode one UTF-8 sequence, or substitute U+FFFD for a bad one.
        std::size_t length = 1;
        bool valid = true;
        if (first >= 0x80) {
            std::uint32_t minimum = 0;
            std::uint32_t codePoint = 0;
            if (first >= 0xC2 && first <= 0xDF) {
                length = 2;
                codePoint = first & 0x1F;
                minimum = 0x80;
            } else if (first >= 0xE0 && first <= 0xEF) {
                length = 3;
                codePoint = first & 0x0F;
                minimum = 0x800;
            } else if (first >= 0xF0 && first <= 0xF4) {
                length = 4;
                codePoint = first & 0x07;
                minimum = 0x10000;
            } else {
                valid = false;
            }
            if (valid && index + length > text.size()) {
                valid = false;
            }
            for (std::size_t offset = 1; valid && offset < length; ++offset) {
                const auto next = static_cast<unsigned char>(text[index + offset]);
                if ((next & 0xC0) != 0x80) {
                    valid = false;
                } else {
                    codePoint = (codePoint << 6) | (next & 0x3F);
                }
            }
            if (valid && (codePoint < minimum || codePoint > 0x10FFFF ||
                          (codePoint >= 0xD800 && codePoint <= 0xDFFF))) {
                valid = false;
            }
            if (!valid) {
                length = 1;
            }
        } else if (first < 0x20 || first == 0x7F) {
            // Other control characters read as nothing.
            ++index;
            continue;
        }
        const std::string_view piece =
            valid ? text.substr(index, length) : std::string_view("\xEF\xBF\xBD", 3);
        const std::size_t needed = piece.size() + (pendingSpace ? 1 : 0);
        if (output.size() + needed > maximumBytes) {
            break;
        }
        if (pendingSpace) {
            output += ' ';
            pendingSpace = false;
        }
        output.append(piece);
        index += length;
    }
    return output;
}

}  // namespace leandoc::core::detail
