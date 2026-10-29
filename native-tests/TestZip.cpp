#include "TestZip.h"

#include "miniz.h"

namespace leandoc::tests {
namespace {

void Le16(std::string& output, std::uint32_t value) {
    output += static_cast<char>(value & 0xFF);
    output += static_cast<char>((value >> 8) & 0xFF);
}

void Le32(std::string& output, std::uint32_t value) {
    Le16(output, value & 0xFFFF);
    Le16(output, value >> 16);
}

}  // namespace

std::string Deflate(std::string_view data) {
    std::size_t size = 0;
    void* compressed = tdefl_compress_mem_to_heap(
        data.data(), data.size(), &size, TDEFL_DEFAULT_MAX_PROBES);
    std::string output(static_cast<const char*>(compressed), size);
    mz_free(compressed);
    return output;
}

std::uint32_t Crc32(std::string_view data) {
    return static_cast<std::uint32_t>(mz_crc32(
        MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
}

std::string BuildZip(const std::vector<TestEntry>& entries) {
    std::string output;
    std::string central;
    for (const TestEntry& entry : entries) {
        const std::string payload = entry.deflate ? Deflate(entry.data) : entry.data;
        const std::uint32_t crc =
            entry.crc32 >= 0 ? static_cast<std::uint32_t>(entry.crc32) : Crc32(entry.data);
        const std::uint32_t declared = entry.declaredSize >= 0
                                           ? static_cast<std::uint32_t>(entry.declaredSize)
                                           : static_cast<std::uint32_t>(entry.data.size());
        const std::uint32_t method =
            entry.method >= 0 ? static_cast<std::uint32_t>(entry.method) : (entry.deflate ? 8u : 0u);
        const auto offset = static_cast<std::uint32_t>(output.size());

        Le32(output, 0x04034b50u);
        Le16(output, 20);
        Le16(output, entry.flags);
        Le16(output, method);
        Le16(output, 0);
        Le16(output, 0x21);
        Le32(output, crc);
        Le32(output, static_cast<std::uint32_t>(payload.size()));
        Le32(output, declared);
        Le16(output, static_cast<std::uint32_t>(entry.name.size()));
        Le16(output, 0);
        output += entry.name;
        output += payload;

        Le32(central, 0x02014b50u);
        Le16(central, 20);
        Le16(central, 20);
        Le16(central, entry.flags);
        Le16(central, method);
        Le16(central, 0);
        Le16(central, 0x21);
        Le32(central, crc);
        Le32(central, static_cast<std::uint32_t>(payload.size()));
        Le32(central, declared);
        Le16(central, static_cast<std::uint32_t>(entry.name.size()));
        Le16(central, 0);
        Le16(central, 0);
        Le16(central, 0);
        Le16(central, 0);
        Le32(central, 0);
        Le32(central, offset);
        central += entry.name;
    }
    const auto directoryOffset = static_cast<std::uint32_t>(output.size());
    output += central;
    Le32(output, 0x06054b50u);
    Le16(output, 0);
    Le16(output, 0);
    Le16(output, static_cast<std::uint32_t>(entries.size()));
    Le16(output, static_cast<std::uint32_t>(entries.size()));
    Le32(output, static_cast<std::uint32_t>(central.size()));
    Le32(output, directoryOffset);
    Le16(output, 0);
    return output;
}

std::string DocumentXml(const std::string& bodyXml) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<w:document "
           "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
           "xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" "
           "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
           "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
           "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\" "
           "xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\" "
           "xmlns:v=\"urn:schemas-microsoft-com:vml\" "
           "xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
           "xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\">"
           "<w:body>" +
           bodyXml + "</w:body></w:document>";
}

std::vector<TestEntry> MinimalDocx(
    const std::string& bodyXml,
    const std::string& extraRelationships,
    const std::string& extraOverrides) {
    std::vector<TestEntry> entries;
    entries.push_back(
        {"[Content_Types].xml",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
         "<Default Extension=\"rels\" "
         "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
         "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
         "<Default Extension=\"png\" ContentType=\"image/png\"/>"
         "<Override PartName=\"/word/document.xml\" ContentType=\"application/"
         "vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>" +
             extraOverrides + "</Types>"});
    entries.push_back(
        {"_rels/.rels",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
         "2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
         "</Relationships>"});
    entries.push_back({"word/document.xml", DocumentXml(bodyXml), true});
    entries.push_back(
        {"word/_rels/document.xml.rels",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">" +
             extraRelationships + "</Relationships>"});
    return entries;
}

}  // namespace leandoc::tests
