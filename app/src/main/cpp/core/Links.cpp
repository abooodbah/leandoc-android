#include "core/Links.h"

#include <array>

namespace leandoc::core {
namespace {

constexpr std::size_t kMaximumLinkBytes = 8192;

char Lower(char character) {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                : character;
}

bool EqualsIgnoringCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (Lower(left[index]) != Lower(right[index])) {
            return false;
        }
    }
    return true;
}

bool ForbiddenByte(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte <= 0x20 || byte >= 0x7F || character == '"' || character == '\\' ||
           character == '<' || character == '>' || character == '`';
}

std::optional<std::string> NormalizeWeb(std::string_view scheme, std::string_view rest) {
    // rest follows "scheme:" and must be "//authority[/path][?query][#fragment]".
    if (rest.substr(0, 2) != "//") {
        return std::nullopt;
    }
    const std::string_view afterSlashes = rest.substr(2);
    const std::size_t authorityEnd = afterSlashes.find_first_of("/?#");
    std::string_view authority = afterSlashes.substr(0, authorityEnd);
    const std::size_t at = authority.rfind('@');
    if (at != std::string_view::npos) {
        authority.remove_prefix(at + 1);
    }
    std::string_view host = authority;
    if (!host.empty() && host.front() == '[') {
        const std::size_t close = host.find(']');
        host = close == std::string_view::npos ? std::string_view() : host.substr(0, close + 1);
    } else {
        host = host.substr(0, host.find(':'));
    }
    if (host.empty() || host == "[]") {
        return std::nullopt;
    }
    std::string result;
    for (const char character : scheme) {
        result.push_back(Lower(character));
    }
    result += ':';
    result += rest;
    return result;
}

std::optional<std::string> NormalizeMailto(std::string_view rest) {
    rest = rest.substr(0, rest.find('#'));
    const std::size_t question = rest.find('?');
    const std::string_view addresses = rest.substr(0, question);
    std::string kept;
    bool hasRecipient = !addresses.empty();
    if (question != std::string_view::npos) {
        static constexpr std::array<std::string_view, 4> kAllowed = {
            "subject", "body", "cc", "bcc"};
        std::string_view query = rest.substr(question + 1);
        while (!query.empty()) {
            const std::size_t ampersand = query.find('&');
            const std::string_view pair = query.substr(0, ampersand);
            query = ampersand == std::string_view::npos ? std::string_view()
                                                        : query.substr(ampersand + 1);
            const std::string_view key = pair.substr(0, pair.find('='));
            bool allowed = false;
            for (const std::string_view name : kAllowed) {
                allowed = allowed || EqualsIgnoringCase(key, name);
            }
            if (!allowed) {
                continue;
            }
            const std::string_view value =
                pair.find('=') == std::string_view::npos ? std::string_view()
                                                          : pair.substr(pair.find('=') + 1);
            if ((EqualsIgnoringCase(key, "cc") || EqualsIgnoringCase(key, "bcc")) &&
                !value.empty()) {
                hasRecipient = true;
            }
            kept += kept.empty() ? '?' : '&';
            kept += pair;
        }
    }
    if (!hasRecipient) {
        return std::nullopt;
    }
    return "mailto:" + std::string(addresses) + kept;
}

}  // namespace

std::optional<std::string> NormalizeExternalLink(std::string_view href) {
    if (href.empty() || href.size() > kMaximumLinkBytes) {
        return std::nullopt;
    }
    for (const char character : href) {
        if (ForbiddenByte(character)) {
            return std::nullopt;
        }
    }
    const std::size_t colon = href.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view scheme = href.substr(0, colon);
    const std::string_view rest = href.substr(colon + 1);
    if (EqualsIgnoringCase(scheme, "http") || EqualsIgnoringCase(scheme, "https")) {
        return NormalizeWeb(scheme, rest);
    }
    if (EqualsIgnoringCase(scheme, "mailto")) {
        return NormalizeMailto(rest);
    }
    return std::nullopt;
}

}  // namespace leandoc::core
