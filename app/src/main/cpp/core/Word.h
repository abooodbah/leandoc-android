#pragma once

// Internal to the core: WordprocessingML analysis, sanitizing, and outline.
// Every function here works on an already parsed and depth-checked part.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/DocCore.h"
#include "core/Xml.h"

namespace leandoc::core::detail {

constexpr std::size_t kFeatureCount = static_cast<std::size_t>(Feature::EmbeddedFonts) + 1;

struct FeatureTally {
    std::array<std::uint32_t, kFeatureCount> counts{};
    // w:commentReference occurrences, used when no comments part exists.
    std::uint32_t commentReferences = 0;

    void Add(Feature feature, std::uint32_t amount = 1) {
        auto& slot = counts[static_cast<std::size_t>(feature)];
        slot = amount > UINT32_MAX - slot ? UINT32_MAX : slot + amount;
    }
    // Takes back up to `amount` of what was added since the count was
    // `before`, for content the reader turns out to show after all.
    void Credit(Feature feature, std::uint32_t before, std::uint32_t amount) {
        auto& slot = counts[static_cast<std::size_t>(feature)];
        const std::uint32_t added = slot > before ? slot - before : 0;
        slot -= added < amount ? added : amount;
    }
    std::uint32_t Get(Feature feature) const {
        return counts[static_cast<std::size_t>(feature)];
    }
};

// Relationship ids of the part that point outside the package.
using ExternalIds = std::unordered_set<std::string>;

// Counts every feature in one story part (document, header, footer,
// footnotes, endnotes). Content inside mc:Fallback is not counted because it
// repeats the content of its mc:Choice.
void AnalyzeStory(
    pugi::xml_document& document,
    const ExternalIds& externalIds,
    FeatureTally& tally);

// Counts elements with this namespace and local name anywhere in the part.
std::uint32_t CountElements(pugi::xml_document& document, Ns ns, const char* local);

// The external relationship id named by settings' w:attachedTemplate, if any.
std::string AttachedTemplateId(pugi::xml_document& settings);

// Rewrites a retained part in place per SPEC section 3.5: removes forbidden
// elements and dangling relationship references, accepts tracked changes,
// and keeps only the cached result of external and DDE fields. `keptIds` are
// the relationship ids that survive; `usedIds` receives the ones still
// referenced afterwards.
void SanitizePart(
    pugi::xml_document& document,
    const std::unordered_set<std::string>& keptIds,
    std::unordered_set<std::string>& usedIds);

// True for an http, https, or mailto URL (scheme compared case-insensitively).
bool IsAllowedLinkTarget(std::string_view target);

struct StyleTable {
    struct Style {
        std::string basedOn;
        int outlineLevel = -1;  // w:outlineLvl, -1 when absent
        int headingLevel = -1;  // from a built-in "heading N" name
        int bidi = -1;          // -1 absent, 0 off, 1 on
    };
    std::unordered_map<std::string, Style> paragraphStyles;
    std::string defaultParagraphStyle;
    int defaultBidi = -1;  // docDefaults pPrDefault
};

StyleTable ReadStyles(pugi::xml_document* styles);

struct OutlineResult {
    std::vector<OutlineEntry> entries;
    bool rightToLeft = false;
};

// Walks the body of a sanitized main document part.
OutlineResult BuildOutline(pugi::xml_document& document, const StyleTable& styles);

// Collapses whitespace, trims, makes the text valid UTF-8, and caps it at
// `maximumBytes` on a character boundary.
std::string CleanText(std::string_view text, std::size_t maximumBytes);

}  // namespace leandoc::core::detail
