#pragma once

// The one rule every host applies before handing a document link to the
// operating system. The reader sends links already normalised by the WHATWG
// URL parser (assets/fixups.js classifyHref), so a well-formed link passes
// unchanged apart from the scheme's case and the mailto parameters.

#include <optional>
#include <string>
#include <string_view>

namespace leandoc::core {

// Returns the link to open, or nothing when LeanDoc must not open it.
//
// - Only http, https (with a host), and mailto, the scheme compared without
//   regard to case and written in lower case in the result.
// - At most 8192 bytes; no control characters, spaces, non-ASCII bytes,
//   quotes, backslashes, angle brackets, or backticks (the reader
//   percent-encodes them).
// - mailto is rebuilt from its addresses and only the subject, body, cc,
//   and bcc parameters, so parameters such as attach= never reach a mail
//   client; the fragment is dropped. A mailto with no address left is
//   refused.
std::optional<std::string> NormalizeExternalLink(std::string_view href);

}  // namespace leandoc::core
