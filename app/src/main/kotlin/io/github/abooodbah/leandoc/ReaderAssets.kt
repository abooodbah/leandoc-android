package io.github.abooodbah.leandoc

/**
 * The reader page is served from the app's assets under an https origin that
 * cannot exist (.invalid is reserved), so its absolute paths such as
 * /reader.css work as on the desktop. Only the listed files and the current
 * document's package are served. Every other request, local or remote, gets
 * no answer.
 */
object ReaderAssets {
    const val ORIGIN = "https://reader.leandoc.invalid"
    const val PAGE_URL = "$ORIGIN/reader.html"
    const val ASSET_ROOT = "reader"
    private const val PACKAGE_PREFIX = "/pkg/"
    private const val PACKAGE_NAME = "/document.docx"
    const val PACKAGE_TYPE = "application/vnd.openxmlformats-officedocument.wordprocessingml.document"

    private val files = mapOf(
        "/reader.html" to "text/html",
        "/reader.css" to "text/css",
        "/reader.js" to "text/javascript",
        "/fixups.js" to "text/javascript",
        "/fidelity.js" to "text/javascript",
        "/android-host.js" to "text/javascript",
        "/vendor/jszip.min.js" to "text/javascript",
        "/vendor/docx-preview.min.js" to "text/javascript",
        "/fonts/ibm-plex-sans-latin-400-normal.woff2" to "font/woff2",
        "/fonts/ibm-plex-sans-latin-500-normal.woff2" to "font/woff2",
        "/fonts/ibm-plex-sans-latin-600-normal.woff2" to "font/woff2",
        "/fonts/ibm-plex-serif-latin-600-normal.woff2" to "font/woff2",
        "/fonts/ibm-plex-mono-latin-400-normal.woff2" to "font/woff2",
        "/fonts/ibm-plex-sans-arabic-arabic-400-normal.woff2" to "font/woff2",
        "/fonts/ibm-plex-sans-arabic-arabic-600-normal.woff2" to "font/woff2",
    )

    val paths: Set<String> get() = files.keys

    sealed interface Route {
        data class Asset(val assetPath: String, val mimeType: String) : Route

        /** The sanitized package of the document with this generation. */
        data class Package(val generation: Long) : Route
    }

    fun packageUrl(generation: Long): String = "$ORIGIN$PACKAGE_PREFIX$generation$PACKAGE_NAME"

    /** What to serve for [url], or null when the request must be blocked. */
    fun route(url: String): Route? {
        if (!url.startsWith("$ORIGIN/")) return null
        // Queries and fragments never select a different file.
        val path = url.substring(ORIGIN.length).substringBefore('#').substringBefore('?')
        files[path]?.let { return Route.Asset(ASSET_ROOT + path, it) }
        if (path.startsWith(PACKAGE_PREFIX) && path.endsWith(PACKAGE_NAME)) {
            val generation = path.substring(PACKAGE_PREFIX.length, path.length - PACKAGE_NAME.length)
            if (generation.isNotEmpty() && generation.all { it in '0'..'9' } && generation.length <= 18) {
                return Route.Package(generation.toLong())
            }
        }
        return null
    }
}
