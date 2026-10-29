#include "core/Zip.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "miniz.h"

namespace leandoc::core::detail {
namespace {

constexpr std::uint64_t kRatioThresholdBytes = 1024ull * 1024ull;
constexpr std::size_t kMaximumNameBytes = 512;
constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50u;
constexpr std::size_t kLocalHeaderSize = 30;

std::uint16_t ReadLe16(const unsigned char* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::uint32_t ReadLe32(const unsigned char* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

void AppendLe16(std::string& output, std::uint32_t value) {
    output += static_cast<char>(value & 0xFF);
    output += static_cast<char>((value >> 8) & 0xFF);
}

void AppendLe32(std::string& output, std::uint32_t value) {
    AppendLe16(output, value & 0xFFFF);
    AppendLe16(output, (value >> 16) & 0xFFFF);
}

void FailPartTooLarge(Failure& failure, const Limits& limits) {
    failure.Set(
        "part-too-large",
        "A part of this document expands past LeanDoc's " +
            std::to_string(limits.maximumPartBytes / (1024ull * 1024ull)) +
            " MB limit.");
}

void FailTotalTooLarge(Failure& failure, const Limits& limits) {
    failure.Set(
        "part-too-large",
        "This document expands past LeanDoc's " +
            std::to_string(
                limits.maximumTotalUncompressedBytes / (1024ull * 1024ull)) +
            " MB limit.");
}

std::size_t ReadFromMemory(void* opaque, mz_uint64 offset, void* buffer, std::size_t size) {
    const auto* bytes = static_cast<const std::string_view*>(opaque);
    if (offset >= bytes->size()) {
        return 0;
    }
    const auto available =
        static_cast<std::size_t>(std::min<mz_uint64>(bytes->size() - offset, size));
    std::memcpy(buffer, bytes->data() + offset, available);
    return available;
}

}  // namespace

std::string LowercaseAscii(std::string_view value) {
    std::string output(value);
    for (char& character : output) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return output;
}

bool IsSafeEntryName(std::string_view name) {
    if (name.empty() || name.size() > kMaximumNameBytes) {
        return false;
    }
    if (name.front() == '/' || name.find('\\') != std::string_view::npos ||
        name.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = name.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? name.size() : slash;
        const std::string_view segment = name.substr(start, end - start);
        if (segment == "..") {
            return false;
        }
        // A drive letter such as "C:" at the start of any segment.
        if (segment.size() >= 2 && segment[1] == ':' &&
            ((segment[0] >= 'A' && segment[0] <= 'Z') ||
             (segment[0] >= 'a' && segment[0] <= 'z'))) {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

struct ZipReader::Archive {
    mz_zip_archive zip{};
    bool initialised = false;
};

ZipReader::ZipReader(const Limits& limits)
    : limits_(limits), archive_(std::make_unique<Archive>()) {}

ZipReader::~ZipReader() {
    if (archive_->initialised) {
        mz_zip_reader_end(&archive_->zip);
    }
}

bool ZipReader::Open(std::string_view bytes, Failure& failure) {
    bytes_ = bytes;
    mz_zip_archive& zip = archive_->zip;
    // A user read callback instead of mz_zip_reader_init_mem: miniz's memory
    // reader forms an out-of-range pointer for offsets past the end.
    zip.m_pRead = ReadFromMemory;
    zip.m_pIO_opaque = &bytes_;
    if (!mz_zip_reader_init(&zip, bytes.size(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        FailCorruptZip(failure);
        return false;
    }
    archive_->initialised = true;

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count > limits_.maximumEntries) {
        failure.Set(
            "too-many-entries",
            "This document has more than " +
                std::to_string(limits_.maximumEntries) +
                " parts, which is over LeanDoc's limit.");
        return false;
    }

    entries_.reserve(count);
    byKey_.reserve(count);
    counted_.assign(count, false);
    std::string name;
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, index, &stat)) {
            FailCorruptZip(failure);
            return false;
        }
        const mz_uint required = mz_zip_reader_get_filename(&zip, index, nullptr, 0);
        if (required == 0) {
            FailCorruptZip(failure);
            return false;
        }
        name.assign(required, '\0');
        mz_zip_reader_get_filename(&zip, index, name.data(), required);
        name.resize(required - 1);

        ZipEntry entry;
        entry.name = name;
        entry.key = LowercaseAscii(name);
        entry.index = index;
        entry.compressedSize = stat.m_comp_size;
        entry.uncompressedSize = stat.m_uncomp_size;
        entry.localHeaderOffset = stat.m_local_header_ofs;
        entry.crc32 = stat.m_crc32;
        entry.method = stat.m_method;
        entry.encrypted = stat.m_is_encrypted != 0 ||
                          (stat.m_bit_flag & (1u | 0x40u)) != 0;
        entry.directory = !name.empty() && name.back() == '/';
        entry.rawNameHasNul = name.find('\0') != std::string::npos;
        // The first entry wins a lookup; ValidateEntries rejects duplicates.
        byKey_.emplace(entry.key, entries_.size());
        entries_.push_back(std::move(entry));
    }
    return true;
}

bool ZipReader::ValidateEntries(Failure& failure) const {
    std::unordered_map<std::string, bool> seen;
    seen.reserve(entries_.size());
    for (const ZipEntry& entry : entries_) {
        if (entry.rawNameHasNul || !IsSafeEntryName(entry.name)) {
            failure.Set(
                "unsafe-entry-name",
                "This document contains a part with an unsafe name.");
            return false;
        }
        if (!seen.emplace(entry.key, true).second) {
            failure.Set(
                "duplicate-entry",
                "This document contains two parts with the same name.");
            return false;
        }
        if (entry.encrypted) {
            failure.Set(
                "encrypted-entry",
                "This document contains an encrypted part.");
            return false;
        }
        if (!entry.directory && entry.method != 0 && entry.method != MZ_DEFLATED) {
            failure.Set(
                "corrupt-zip",
                "This document uses a compression method LeanDoc does not support.");
            return false;
        }
    }
    return true;
}

const ZipEntry* ZipReader::Find(std::string_view name) const {
    const auto found = byKey_.find(LowercaseAscii(name));
    return found == byKey_.end() ? nullptr : &entries_[found->second];
}

bool ZipReader::Read(const ZipEntry& entry, std::string& output, Failure& failure) {
    output.clear();
    if (entry.encrypted) {
        failure.Set("encrypted-entry", "This document contains an encrypted part.");
        return false;
    }
    if (entry.method != 0 && entry.method != MZ_DEFLATED) {
        failure.Set(
            "corrupt-zip",
            "This document uses a compression method LeanDoc does not support.");
        return false;
    }
    const std::uint64_t declared = entry.uncompressedSize;
    if (declared > limits_.maximumPartBytes ||
        declared > std::numeric_limits<std::size_t>::max()) {
        FailPartTooLarge(failure, limits_);
        return false;
    }
    // Divided rather than multiplied, so a forged compressed size cannot
    // overflow the comparison.
    const std::uint64_t ratio =
        limits_.maximumCompressionRatio == 0 ? 1 : limits_.maximumCompressionRatio;
    if (declared > kRatioThresholdBytes &&
        (entry.compressedSize == 0 || (declared - 1) / ratio >= entry.compressedSize)) {
        failure.Set(
            "compression-ratio",
            "A part of this document is compressed more than LeanDoc allows.");
        return false;
    }
    if ((entry.method == 0 && entry.compressedSize != declared) ||
        (entry.compressedSize == 0 && declared != 0)) {
        FailCorruptZip(failure);
        return false;
    }
    const bool alreadyCounted = counted_[entry.index];
    if (!alreadyCounted &&
        declared > limits_.maximumTotalUncompressedBytes - totalInflated_) {
        FailTotalTooLarge(failure, limits_);
        return false;
    }

    // Locate the data through the local header, with every offset checked
    // against the archive size.
    const auto* base = reinterpret_cast<const unsigned char*>(bytes_.data());
    const std::uint64_t size = bytes_.size();
    if (entry.localHeaderOffset > size || size - entry.localHeaderOffset < kLocalHeaderSize) {
        FailCorruptZip(failure);
        return false;
    }
    const unsigned char* header = base + entry.localHeaderOffset;
    if (ReadLe32(header) != kLocalHeaderSignature) {
        FailCorruptZip(failure);
        return false;
    }
    const std::uint64_t dataOffset = entry.localHeaderOffset + kLocalHeaderSize +
                                     ReadLe16(header + 26) + ReadLe16(header + 28);
    if (dataOffset > size || size - dataOffset < entry.compressedSize) {
        FailCorruptZip(failure);
        return false;
    }
    const unsigned char* data = base + dataOffset;

    output.resize(static_cast<std::size_t>(declared));
    if (entry.method == 0) {
        if (declared != 0) {
            std::memcpy(output.data(), data, static_cast<std::size_t>(declared));
        }
    } else {
        // The output buffer is exactly the declared size. A stream that tries
        // to produce more stops with HAS_MORE_OUTPUT and fails.
        auto inflator = std::make_unique<tinfl_decompressor>();
        tinfl_init(inflator.get());
        std::size_t inputSize = static_cast<std::size_t>(entry.compressedSize);
        std::size_t outputSize = output.size();
        auto* out = reinterpret_cast<mz_uint8*>(output.data());
        const tinfl_status status = tinfl_decompress(
            inflator.get(), data, &inputSize, out, out, &outputSize,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
            output.clear();
            FailPartTooLarge(failure, limits_);
            return false;
        }
        if (status != TINFL_STATUS_DONE || outputSize != output.size()) {
            output.clear();
            FailCorruptZip(failure);
            return false;
        }
    }
    if (Crc32(output) != entry.crc32) {
        output.clear();
        FailCorruptZip(failure);
        return false;
    }
    if (!alreadyCounted) {
        counted_[entry.index] = true;
        totalInflated_ += declared;
    }
    return true;
}

std::uint32_t Crc32(std::string_view data) {
    // mz_crc32 takes a size_t, so large inputs need no chunking.
    return static_cast<std::uint32_t>(mz_crc32(
        MZ_CRC32_INIT, reinterpret_cast<const mz_uint8*>(data.data()), data.size()));
}

void StoredZipWriter::Add(std::string_view name, std::string_view data) {
    Central central;
    central.name = std::string(name);
    central.crc32 = Crc32(data);
    central.size = static_cast<std::uint32_t>(data.size());
    central.offset = static_cast<std::uint32_t>(output_.size());

    AppendLe32(output_, kLocalHeaderSignature);
    AppendLe16(output_, 20);       // version needed
    AppendLe16(output_, 0x0800);   // UTF-8 names
    AppendLe16(output_, 0);        // stored
    AppendLe16(output_, 0);        // 00:00:00
    AppendLe16(output_, 0x0021);   // 1980-01-01
    AppendLe32(output_, central.crc32);
    AppendLe32(output_, central.size);
    AppendLe32(output_, central.size);
    AppendLe16(output_, static_cast<std::uint32_t>(name.size()));
    AppendLe16(output_, 0);
    output_.append(name);
    output_.append(data);
    central_.push_back(std::move(central));
}

std::string StoredZipWriter::Finish() {
    const auto directoryOffset = static_cast<std::uint32_t>(output_.size());
    for (const Central& central : central_) {
        AppendLe32(output_, 0x02014b50u);
        AppendLe16(output_, 20);  // version made by
        AppendLe16(output_, 20);  // version needed
        AppendLe16(output_, 0x0800);
        AppendLe16(output_, 0);
        AppendLe16(output_, 0);
        AppendLe16(output_, 0x0021);
        AppendLe32(output_, central.crc32);
        AppendLe32(output_, central.size);
        AppendLe32(output_, central.size);
        AppendLe16(output_, static_cast<std::uint32_t>(central.name.size()));
        AppendLe16(output_, 0);  // extra
        AppendLe16(output_, 0);  // comment
        AppendLe16(output_, 0);  // disk
        AppendLe16(output_, 0);  // internal attributes
        AppendLe32(output_, 0);  // external attributes
        AppendLe32(output_, central.offset);
        output_.append(central.name);
    }
    const auto directorySize =
        static_cast<std::uint32_t>(output_.size() - directoryOffset);
    AppendLe32(output_, 0x06054b50u);
    AppendLe16(output_, 0);
    AppendLe16(output_, 0);
    AppendLe16(output_, static_cast<std::uint32_t>(central_.size()));
    AppendLe16(output_, static_cast<std::uint32_t>(central_.size()));
    AppendLe32(output_, directorySize);
    AppendLe32(output_, directoryOffset);
    AppendLe16(output_, 0);
    central_.clear();
    return std::move(output_);
}

}  // namespace leandoc::core::detail
