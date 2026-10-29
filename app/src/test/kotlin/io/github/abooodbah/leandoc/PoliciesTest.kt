package io.github.abooodbah.leandoc

import java.io.ByteArrayInputStream
import java.io.InputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class PoliciesTest {
    @Test
    fun webAndMailLinksGoToOtherApps() {
        assertEquals(LinkAction.External("https://example.org/a"), LinkAction.forHref("https://example.org/a"))
        assertEquals(LinkAction.External("mailto:a@b.example"), LinkAction.forHref(" mailto:a@b.example "))
    }

    @Test
    fun otherLinksAreRefused() {
        for (href in listOf("javascript:alert(1)", "file:///etc/hosts", "data:text/html,x", "ms-word:ofe|u|x", "//host/x")) {
            assertTrue(href, LinkAction.forHref(href) is LinkAction.Refuse)
        }
        assertTrue((LinkAction.forHref("other.docx") as LinkAction.Refuse).message.contains("other files"))
        assertEquals(LinkAction.Ignore, LinkAction.forHref("#_Toc1"))
    }

    @Test
    fun readsUpToTheLimitAndNoFurther() {
        val bytes = ByteArray(1000) { it.toByte() }
        assertArrayEquals(bytes, DocumentReader.readLimited(ByteArrayInputStream(bytes), limit = 1000))
        try {
            DocumentReader.readLimited(ByteArrayInputStream(ByteArray(1001)), limit = 1000)
            fail("expected TooLargeException")
        } catch (expected: DocumentReader.TooLargeException) {
            assertEquals(DocumentReader.TOO_LARGE, expected.message)
        }
    }

    @Test
    fun aDeclaredSizeOverTheLimitIsRefusedWithoutReading() {
        var reads = 0
        val stream = object : InputStream() {
            override fun read(): Int {
                reads++
                return 0
            }
        }
        try {
            DocumentReader.readLimited(stream, limit = 10, sizeHint = 11)
            fail("expected TooLargeException")
        } catch (expected: DocumentReader.TooLargeException) {
            assertEquals(0, reads)
        }
    }

    @Test
    fun theLimitFitsAPhone() {
        assertEquals(64L * 1024 * 1024, DocumentReader.MAX_BYTES)
    }

    @Test
    fun webViewVersions() {
        assertTrue(WebViewSupport.isSupported("80.0.3987.99"))
        assertTrue(WebViewSupport.isSupported("140.0.1"))
        assertFalse(WebViewSupport.isSupported("79.0.3945.136"))
        assertFalse(WebViewSupport.isSupported("69.0.3497.100"))
        assertFalse(WebViewSupport.isSupported(null))
        assertTrue(WebViewSupport.message("69.0.3497.100").contains("69.0.3497.100"))
    }
}
