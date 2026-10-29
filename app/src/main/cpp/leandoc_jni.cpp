// JNI entry point for io.github.abooodbah.leandoc.DocCore. The document rules
// live in core/, shared unchanged with the desktop app; this file only moves
// bytes across the JNI boundary.

#include <jni.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "core/DocCore.h"

namespace {

namespace core = leandoc::core;

constexpr char kResultClass[] = "io/github/abooodbah/leandoc/OpenedDocument";

jbyteArray ToByteArray(JNIEnv* env, std::string_view value) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(value.size()));
    if (array != nullptr && !value.empty()) {
        env->SetByteArrayRegion(
            array, 0, static_cast<jsize>(value.size()),
            reinterpret_cast<const jbyte*>(value.data()));
    }
    return array;
}

}  // namespace

extern "C" {

// Classifies and sanitizes one document. Returns an OpenedDocument holding the
// core's JSON report (UTF-8) and, for documents the reader renders, the
// sanitized package; otherwise the package is null.
JNIEXPORT jobject JNICALL
Java_io_github_abooodbah_leandoc_DocCore_open(
    JNIEnv* env, jclass, jbyteArray document, jlong maximumInputBytes) {
    std::string bytes;
    const jsize length = document == nullptr ? 0 : env->GetArrayLength(document);
    bytes.resize(static_cast<std::size_t>(length));
    if (length > 0) {
        env->GetByteArrayRegion(document, 0, length, reinterpret_cast<jbyte*>(bytes.data()));
    }

    core::Limits limits;
    if (maximumInputBytes > 0) {
        limits.maximumInputBytes = static_cast<std::uint64_t>(maximumInputBytes);
    }
    core::OpenResult result = core::OpenDocumentBytes(bytes, limits);
    std::string().swap(bytes);
    const std::string report = core::ReportToJson(result);

    jclass type = env->FindClass(kResultClass);
    if (type == nullptr) {
        return nullptr;
    }
    jmethodID constructor = env->GetMethodID(type, "<init>", "([B[B)V");
    if (constructor == nullptr) {
        return nullptr;
    }
    jbyteArray reportBytes = ToByteArray(env, report);
    jbyteArray package = nullptr;
    if (!result.sanitizedPackage.empty()) {
        package = ToByteArray(env, result.sanitizedPackage);
        std::string().swap(result.sanitizedPackage);
    }
    if (reportBytes == nullptr || env->ExceptionCheck()) {
        return nullptr;
    }
    return env->NewObject(type, constructor, reportBytes, package);
}

}  // extern "C"
