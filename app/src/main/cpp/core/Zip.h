#pragma once

// Internal to the core: a bounded ZIP reader on top of miniz's central
// directory parser, and a small writer for stored (uncompressed) archives.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/DocCore.h"
#include "core/Failure.h"

namespace leandoc::core::detail {

struct ZipEntry {
    std::string name;  // as stored in the archive
    std::string key;   // ASCII-lowercased name, used for OPC lookups
    std::uint32_t index = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t uncompressedSize = 0;
    std::uint64_t localHeaderOffset = 0;
    std::uint32_t crc32 = 0;
    std::uint16_t method = 0;
    bool encrypted = false;
    bool directory = false;
    bool rawNameHasNul = false;
};

class ZipReader final {
public:
    explicit ZipReader(const Limits& limits);
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    // Parses the central directory. Fails with corrupt-zip or
    // too-many-entries. The bytes must outlive the reader.
    bool Open(std::string_view bytes, Failure& failure);

    // Checks every entry: names, case-insensitive duplicates, encryption,
    // and compression methods.
    bool ValidateEntries(Failure& failure) const;

    // Case-insensitive lookup of a part name without a leading slash.
    const ZipEntry* Find(std::string_view name) const;
    const std::vector<ZipEntry>& Entries() const { return entries_; }

    // Inflates one entry within the part, ratio, and running-total limits and
    // checks its CRC.
    bool Read(const ZipEntry& entry, std::string& output, Failure& failure);

private:
    struct Archive;
    const Limits& limits_;
    std::string_view bytes_;
    std::unique_ptr<Archive> archive_;
    std::vector<ZipEntry> entries_;
    std::unordered_map<std::string, std::size_t> byKey_;
    std::vector<bool> counted_;
    std::uint64_t totalInflated_ = 0;
};

// Validates one archive entry name against SPEC section 3.2.
bool IsSafeEntryName(std::string_view name);

std::string LowercaseAscii(std::string_view value);

// Writes a ZIP with every entry stored, a fixed 1980-01-01 timestamp, and
// UTF-8 names.
class StoredZipWriter final {
public:
    void Add(std::string_view name, std::string_view data);
    std::string Finish();

private:
    struct Central {
        std::string name;
        std::uint32_t crc32 = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };
    std::string output_;
    std::vector<Central> central_;
};

std::uint32_t Crc32(std::string_view data);

}  // namespace leandoc::core::detail
