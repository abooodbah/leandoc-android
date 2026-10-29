package io.github.abooodbah.leandoc

import java.io.File
import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ReaderAssetsTest {
    private val assetDir = File("src/main/assets/reader")

    @Test
    fun servesListedFilesAndTheCurrentPackageOnly() {
        assertEquals(
            ReaderAssets.Route.Asset("reader/reader.html", "text/html"),
            ReaderAssets.route("https://reader.leandoc.invalid/reader.html"),
        )
        assertEquals(
            ReaderAssets.Route.Asset("reader/vendor/docx-preview.min.js", "text/javascript"),
            ReaderAssets.route("https://reader.leandoc.invalid/vendor/docx-preview.min.js?x#y"),
        )
        assertEquals(ReaderAssets.Route.Package(7), ReaderAssets.route(ReaderAssets.packageUrl(7)))
        val refused = listOf(
            "https://example.com/reader.html",
            "http://reader.leandoc.invalid/reader.html",
            "https://reader.leandoc.invalid.example.com/reader.html",
            "https://reader.leandoc.invalid:8443/reader.html",
            "https://reader.leandoc.invalid/",
            "https://reader.leandoc.invalid/../reader.html",
            "https://reader.leandoc.invalid/pkg//document.docx",
            "https://reader.leandoc.invalid/pkg/-1/document.docx",
            "https://reader.leandoc.invalid/pkg/1x/document.docx",
            "https://reader.leandoc.invalid/pkg/1/other.docx",
            "https://reader.leandoc.invalid/pkg/1/../../reader.html",
            "https://reader.leandoc.invalid/pkg/9999999999999999999/document.docx",
            "https://reader.leandoc.invalid/THIRD_PARTY_NOTICES.md",
            "data:text/html,hi",
            "file:///android_asset/reader/reader.html",
        )
        for (url in refused) assertNull(url, ReaderAssets.route(url))
    }

    @Test
    fun everyServedFileIsPackagedAndEveryPackagedFileIsServed() {
        val packaged = assetDir.walkTopDown().filter { it.isFile }
            .map { "/" + it.relativeTo(assetDir).invariantSeparatorsPath }.toSet()
        assertEquals(packaged, ReaderAssets.paths)
    }

    @Test
    fun pageLoadsTheAndroidBridgeBeforeTheReader() {
        val page = File(assetDir, "reader.html").readText()
        val bridge = page.indexOf("""<script src="/android-host.js"></script>""")
        val firstScript = page.indexOf("""<script src="/vendor/jszip.min.js" defer></script>""")
        assertTrue("bridge script present", bridge > 0)
        assertTrue("bridge runs before every other script", bridge < firstScript)
        // The desktop policy stays in force: no frames, objects or workers, and
        // fetch only from the page's own origin (the package).
        assertTrue(page.contains("default-src 'none'"))
        assertTrue(page.contains("connect-src 'self'"))
        assertTrue(page.contains("worker-src 'none'"))
    }

    @Test
    fun copiedFilesMatchTheirRecordedSums() {
        val sums = File("../third_party/reader-assets.sha256").readLines()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .associate { line -> line.substringAfter("  ") to line.substringBefore("  ") }
        assertTrue("sums recorded", sums.size >= 50)
        for ((path, expected) in sums) {
            val bytes = File("..", path).readBytes()
            val actual = MessageDigest.getInstance("SHA-256").digest(bytes)
                .joinToString("") { "%02x".format(it) }
            assertEquals("$path changed; run scripts/update-reader-assets.sh", expected, actual)
        }
    }
}
