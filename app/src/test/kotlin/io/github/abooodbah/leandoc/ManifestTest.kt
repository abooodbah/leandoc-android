package io.github.abooodbah.leandoc

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/**
 * Reads the source manifest. The merged manifest of the release APK is checked
 * again in CI with aapt2, because a library could add permissions there.
 */
class ManifestTest {
    private val android = "http://schemas.android.com/apk/res/android"

    private val manifest: Element by lazy {
        val factory = DocumentBuilderFactory.newInstance().apply { isNamespaceAware = true }
        factory.newDocumentBuilder().parse(File("src/main/AndroidManifest.xml")).documentElement
    }

    private fun elements(name: String): List<Element> {
        val nodes = manifest.getElementsByTagName(name)
        return (0 until nodes.length).map { nodes.item(it) as Element }
    }

    private fun Element.attr(name: String): String = getAttributeNS(android, name)

    private fun Element.children(name: String): List<Element> {
        val nodes = getElementsByTagName(name)
        return (0 until nodes.length).map { nodes.item(it) as Element }
    }

    @Test
    fun requestsNoPermissions() {
        val requested = elements("uses-permission") + elements("uses-permission-sdk-23")
        assertEquals(emptyList<String>(), requested.map { it.attr("name") })
    }

    @Test
    fun opensWordDocumentsFromOtherApps() {
        val viewFilters = elements("intent-filter").filter { filter ->
            filter.children("action").any { it.attr("name") == "android.intent.action.VIEW" }
        }
        val types = viewFilters.flatMap { it.children("data") }.map { it.attr("mimeType") }
        for (type in WORD_TYPES) assertTrue("VIEW filters accept $type: $types", type in types)
        val schemes = viewFilters.flatMap { it.children("data") }.map { it.attr("scheme") }
        assertTrue("VIEW filters accept content URIs: $schemes", "content" in schemes)
        assertTrue("legacy .doc is not claimed", "application/msword" !in types)
    }

    companion object {
        val WORD_TYPES = listOf(
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
            "application/vnd.openxmlformats-officedocument.wordprocessingml.template",
            "application/vnd.ms-word.document.macroEnabled.12",
            "application/vnd.ms-word.template.macroEnabled.12",
        )
    }

    @Test
    fun webViewSendsNothing() {
        val meta = elements("meta-data").associate { it.attr("name") to it.attr("value") }
        assertEquals("true", meta["android.webkit.WebView.MetricsOptOut"])
        assertEquals("false", meta["android.webkit.WebView.EnableSafeBrowsing"])
    }
}
