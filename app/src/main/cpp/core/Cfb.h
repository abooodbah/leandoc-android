#pragma once

// Internal to the core: recognises OLE compound files (Word 97-2003 and
// encrypted OOXML) by their directory, without reading any stream.

#include <string_view>

#include "core/DocCore.h"

namespace leandoc::core::detail {

bool HasCompoundFileSignature(std::string_view bytes);

// LegacyDoc, EncryptedOoxml, or NotADocument (another OLE format).
FileKind ClassifyCompoundFile(std::string_view bytes);

}  // namespace leandoc::core::detail
