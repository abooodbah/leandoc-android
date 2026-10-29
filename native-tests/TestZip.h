#pragma once

// Builds ZIP archives in memory for the core tests. Unlike a real writer it
// can lie: declared sizes, CRCs, flags, and methods are all overridable.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace leandoc::tests {

struct TestEntry {
    std::string name;
    std::string data;
    bool deflate = false;
    // Overrides; negative or zero-flag values mean "use the real value".
    long long declaredSize = -1;
    long long crc32 = -1;
    int method = -1;
    std::uint16_t flags = 0;
};

std::string Deflate(std::string_view data);
std::uint32_t Crc32(std::string_view data);
std::string BuildZip(const std::vector<TestEntry>& entries);

// The minimal parts of a valid .docx with the given document body XML (the
// content of w:body) and extra entries.
std::vector<TestEntry> MinimalDocx(
    const std::string& bodyXml,
    const std::string& extraRelationships = "",
    const std::string& extraOverrides = "");

std::string DocumentXml(const std::string& bodyXml);

}  // namespace leandoc::tests
