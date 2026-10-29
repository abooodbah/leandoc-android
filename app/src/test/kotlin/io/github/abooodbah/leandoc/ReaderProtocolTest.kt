package io.github.abooodbah.leandoc

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ReaderProtocolTest {
    @Test
    fun commandsFromThePage() {
        assertEquals(ReaderCommand.Ready, ReaderCommand.parse("""{"type":"ready"}"""))
        assertEquals(ReaderCommand.Open, ReaderCommand.parse("""{"type":"open"}"""))
        assertEquals(ReaderCommand.Reload, ReaderCommand.parse("""{"type":"reload"}"""))
        assertEquals(ReaderCommand.OpenInEditor, ReaderCommand.parse("""{"type":"openInEditor","tab":1}"""))
        assertEquals(ReaderCommand.SetTheme("dark"), ReaderCommand.parse("""{"type":"setTheme","theme":"dark"}"""))
        assertEquals(ReaderCommand.SetView("reading"), ReaderCommand.parse("""{"type":"setView","view":"reading"}"""))
        assertEquals(ReaderCommand.Link("https://a.example/"), ReaderCommand.parse("""{"type":"link","href":"https://a.example/"}"""))
        assertEquals(ReaderCommand.Rendered(4, 12), ReaderCommand.parse("""{"type":"rendered","tab":1,"generation":4,"pages":12,"ms":30}"""))
        assertEquals(
            ReaderCommand.RenderFailed(4, "boom"),
            ReaderCommand.parse("""{"type":"renderFailed","tab":1,"generation":4,"message":"boom"}"""),
        )
    }

    @Test
    fun badOrUnknownCommandsAreIgnored() {
        for (text in listOf(
            "", "ready", "[]", """{"type":"setTheme","theme":"sepia"}""", """{"type":"setView","view":"web"}""",
            """{"type":"link","href":""}""", """{"type":"rendered","pages":3}""", """{"type":"rendered","generation":-1}""",
            """{"type":"selectTab","id":2}""", """{"type":"openRecent","id":"0"}""", """{"type":"clearRecent"}""",
        )) {
            assertNull(text, ReaderCommand.parse(text))
        }
        assertNull(ReaderCommand.parse("""{"type":"link","href":"${"a".repeat(8193)}"}"""))
    }

    @Test
    fun hostMessagesMatchTheDesktopShape() {
        val host = JSONObject(HostMessages.host("0.1.0"))
        assertEquals("host", host.getString("type"))
        assertEquals("android", host.getString("platform"))
        assertEquals("0.1.0", host.getString("version"))
        assertEquals("Open in another app", host.getJSONObject("editor").getString("label"))
        assertFalse(host.getBoolean("tabs"))

        val theme = JSONObject(HostMessages.theme("light", "reading"))
        assertEquals("light", theme.getString("theme"))
        assertEquals("reading", theme.getString("view"))

        val report = """{"ok":true,"errorCode":"","kind":"docx","sourceBytes":10,"features":[],"outline":[]}"""
        val document = JSONObject(
            HostMessages.document(3, "Report \"Q3\".docx", ReaderAssets.packageUrl(3), report),
        )
        assertEquals("document", document.getString("type"))
        assertEquals(1, document.getInt("tab"))
        assertEquals(3, document.getLong("generation"))
        assertEquals("Report \"Q3\".docx", document.getString("fileName"))
        assertEquals("https://reader.leandoc.invalid/pkg/3/document.docx", document.getString("packageUrl"))
        assertTrue(document.getJSONObject("report").getBoolean("ok"))

        val fallback = JSONObject(HostMessages.fallback(5, "old.doc", report))
        assertEquals("fallback", fallback.getString("type"))
        assertEquals(5, fallback.getLong("generation"))

        assertEquals("empty", JSONObject(HostMessages.empty()).getString("type"))
        assertEquals("warning", JSONObject(HostMessages.status("x", "warning")).getString("tone"))
    }

    @Test
    fun failureReportsHaveEveryKeyTheReaderReads() {
        val report = JSONObject(HostMessages.failureReport("too-large", DocumentReader.TOO_LARGE, 70_000_000))
        assertFalse(report.getBoolean("ok"))
        assertEquals("too-large", report.getString("errorCode"))
        assertEquals("This file is larger than LeanDoc's 64 MB limit.", report.getString("errorMessage"))
        assertEquals(70_000_000, report.getLong("sourceBytes"))
        assertEquals(0, report.getJSONArray("features").length())
        assertEquals(0, report.getJSONArray("outline").length())
        assertFalse(report.getJSONObject("properties").getBoolean("rightToLeft"))
    }
}
