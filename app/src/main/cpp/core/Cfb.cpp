#include "core/Cfb.h"

#include <cstdint>
#include <string>

namespace leandoc::core::detail {
namespace {

constexpr std::uint32_t kEndOfChain = 0xFFFFFFFEu;
constexpr std::uint32_t kMaximumRegularSector = 0xFFFFFFFAu;
constexpr std::size_t kHeaderDifatEntries = 109;

std::uint32_t ReadLe32(std::string_view bytes, std::uint64_t offset) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data()) + offset;
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint16_t ReadLe16(std::string_view bytes, std::uint64_t offset) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data()) + offset;
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

// Reads just enough of a compound file to list the directory entry names.
// Every sector number is bounds-checked and every chain walk is capped by the
// number of sectors in the file, so a cyclic or truncated file ends the walk.
class CompoundFile final {
public:
    explicit CompoundFile(std::string_view bytes) : bytes_(bytes) {}

    bool Init() {
        if (bytes_.size() < 512) {
            return false;
        }
        const std::uint16_t shift = ReadLe16(bytes_, 30);
        if (shift != 9 && shift != 12) {
            return false;
        }
        sectorSize_ = 1u << shift;
        sectorCount_ = bytes_.size() <= sectorSize_
                           ? 0
                           : (bytes_.size() - sectorSize_ + sectorSize_ - 1) / sectorSize_;
        return true;
    }

    // Calls visit(name) for every directory entry name, in ASCII; non-ASCII
    // characters become '?'. Returns false if the directory chain is broken.
    template <typename Visit>
    bool ForEachName(Visit&& visit) const {
        std::uint32_t sector = ReadLe32(bytes_, 48);
        std::uint64_t steps = 0;
        while (sector != kEndOfChain) {
            if (sector > kMaximumRegularSector || sector >= sectorCount_ ||
                ++steps > sectorCount_) {
                return false;
            }
            const std::uint64_t offset = SectorOffset(sector);
            if (offset + sectorSize_ > bytes_.size()) {
                return false;
            }
            for (std::uint32_t entry = 0; entry < sectorSize_ / 128; ++entry) {
                const std::uint64_t base = offset + entry * 128ull;
                const std::uint16_t nameBytes = ReadLe16(bytes_, base + 64);
                const auto type = static_cast<unsigned char>(bytes_[base + 66]);
                if (type == 0 || nameBytes < 2 || nameBytes > 64) {
                    continue;
                }
                std::string name;
                for (std::uint32_t index = 0; index + 2 < nameBytes; index += 2) {
                    const std::uint16_t unit = ReadLe16(bytes_, base + index);
                    name += unit < 0x80 ? static_cast<char>(unit) : '?';
                }
                visit(name);
            }
            if (!NextSector(sector, sector)) {
                return false;
            }
        }
        return true;
    }

private:
    std::uint64_t SectorOffset(std::uint32_t sector) const {
        return (static_cast<std::uint64_t>(sector) + 1) * sectorSize_;
    }

    bool DifatEntry(std::uint64_t index, std::uint32_t& fatSector) const {
        if (index < kHeaderDifatEntries) {
            fatSector = ReadLe32(bytes_, 76 + index * 4);
            return true;
        }
        index -= kHeaderDifatEntries;
        const std::uint64_t perSector = sectorSize_ / 4 - 1;
        std::uint32_t difat = ReadLe32(bytes_, 68);
        std::uint64_t steps = 0;
        while (index >= perSector) {
            if (!ReadSectorEntry(difat, perSector, difat) || ++steps > sectorCount_) {
                return false;
            }
            index -= perSector;
        }
        return ReadSectorEntry(difat, index, fatSector);
    }

    bool ReadSectorEntry(
        std::uint32_t sector,
        std::uint64_t index,
        std::uint32_t& value) const {
        if (sector > kMaximumRegularSector || sector >= sectorCount_ ||
            index >= sectorSize_ / 4) {
            return false;
        }
        const std::uint64_t offset = SectorOffset(sector) + index * 4;
        if (offset + 4 > bytes_.size()) {
            return false;
        }
        value = ReadLe32(bytes_, offset);
        return true;
    }

    bool NextSector(std::uint32_t sector, std::uint32_t& next) const {
        const std::uint64_t perSector = sectorSize_ / 4;
        std::uint32_t fatSector = 0;
        return DifatEntry(sector / perSector, fatSector) &&
               ReadSectorEntry(fatSector, sector % perSector, next);
    }

    std::string_view bytes_;
    std::uint32_t sectorSize_ = 512;
    std::uint64_t sectorCount_ = 0;
};

bool EqualsIgnoringCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool HasCompoundFileSignature(std::string_view bytes) {
    static constexpr unsigned char kSignature[] = {
        0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    if (bytes.size() < sizeof(kSignature)) {
        return false;
    }
    for (std::size_t index = 0; index < sizeof(kSignature); ++index) {
        if (static_cast<unsigned char>(bytes[index]) != kSignature[index]) {
            return false;
        }
    }
    return true;
}

FileKind ClassifyCompoundFile(std::string_view bytes) {
    CompoundFile file(bytes);
    if (!file.Init()) {
        // A damaged Word 97-2003 file still gets the fallback page.
        return FileKind::LegacyDoc;
    }
    bool encryptionInfo = false;
    bool encryptedPackage = false;
    bool wordDocument = false;
    const bool complete = file.ForEachName([&](const std::string& name) {
        encryptionInfo = encryptionInfo || EqualsIgnoringCase(name, "EncryptionInfo");
        encryptedPackage =
            encryptedPackage || EqualsIgnoringCase(name, "EncryptedPackage");
        wordDocument = wordDocument || EqualsIgnoringCase(name, "WordDocument");
    });
    if (encryptionInfo || encryptedPackage) {
        return FileKind::EncryptedOoxml;
    }
    if (wordDocument || !complete) {
        return FileKind::LegacyDoc;
    }
    // A readable compound file without a Word stream: Excel, PowerPoint,
    // Outlook, and other OLE formats.
    return FileKind::NotADocument;
}

}  // namespace leandoc::core::detail
