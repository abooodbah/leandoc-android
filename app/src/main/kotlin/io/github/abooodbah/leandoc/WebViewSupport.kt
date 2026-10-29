package io.github.abooodbah.leandoc

/**
 * docx-preview 0.4.1 uses optional chaining and the ?? operator, which
 * WebView parses from version 80. Older WebView builds cannot load the page at
 * all, so the app shows a plain message instead.
 */
object WebViewSupport {
    const val MINIMUM_MAJOR = 80

    /** The major version in a WebView versionName such as "124.0.6367.82", or null. */
    fun majorVersion(versionName: String?): Int? =
        versionName?.trim()?.substringBefore('.')?.toIntOrNull()

    fun isSupported(versionName: String?): Boolean {
        val major = majorVersion(versionName) ?: return false
        return major >= MINIMUM_MAJOR
    }

    fun message(versionName: String?): String {
        val found = versionName?.takeIf { it.isNotBlank() } ?: "none"
        return "LeanDoc needs Android System WebView $MINIMUM_MAJOR or later to show " +
            "documents. This device has $found. Update Android System WebView from " +
            "Google Play, then open the file again."
    }
}
