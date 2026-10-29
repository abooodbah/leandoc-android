#pragma once

// Internal to the core. A failure carries one of the stable error codes listed
// in DocCore.h and the sentence the reader shows.

#include <string>

namespace leandoc::core::detail {

struct Failure {
    std::string code;
    std::string message;

    bool Failed() const { return !code.empty(); }
    void Set(const char* newCode, std::string newMessage) {
        if (code.empty()) {
            code = newCode;
            message = std::move(newMessage);
        }
    }
};

inline void FailCorruptZip(Failure& failure) {
    failure.Set("corrupt-zip", "This document's ZIP structure is damaged.");
}

inline void FailMalformedXml(Failure& failure) {
    failure.Set(
        "xml-malformed", "This document contains XML that LeanDoc cannot read.");
}

}  // namespace leandoc::core::detail
