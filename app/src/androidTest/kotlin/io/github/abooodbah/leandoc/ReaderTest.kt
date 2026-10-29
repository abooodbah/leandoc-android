package io.github.abooodbah.leandoc

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.Uri
import android.os.SystemClock
import android.webkit.WebView
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Opens the desktop app's own test documents (tests/fixtures in the desktop
 * repository) through a content provider, the way other apps share files.
 */
@RunWith(AndroidJUnit4::class)
class ReaderTest {

    @Before
    fun needsAWebViewThatRunsThePage() {
        assumeTrue(
            "docx-preview needs WebView ${WebViewSupport.MINIMUM_MAJOR} or later",
            Reader.webViewMajor >= WebViewSupport.MINIMUM_MAJOR,
        )
    }

    private fun bodyText(scenario: ActivityScenario<MainActivity>): String =
        Reader.text(scenario, "#docBody").orEmpty()

    @Test
    fun opensADocxFromAViewIntent() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            val text = bodyText(scenario)
            assertTrue(text, text.contains("Styles and inheritance"))
            assertTrue(text, text.contains("Base style paragraph in Georgia and blue."))
            assertEquals("styles.docx", Reader.text(scenario, "#fileName"))
        }
    }

    @Test
    fun templatesOpenAndTheirMacrosAreReported() {
        Reader.open("kinds/template.dotx", "application/vnd.openxmlformats-officedocument.wordprocessingml.template").use { scenario ->
            Reader.waitForDocument(scenario)
            assertTrue(bodyText(scenario).contains("A Word template."))
        }
        Reader.open("kinds/template.dotm", "application/vnd.ms-word.template.macroenabled.12").use { scenario ->
            Reader.waitForDocument(scenario)
            assertTrue(bodyText(scenario).contains("A macro-enabled template."))
            assertTrue(Reader.text(scenario, "#fidelityBar").orEmpty().contains("macro"))
        }
    }

    @Test
    fun aMacroDocumentIsShownWithItsMacrosRemoved() {
        Reader.open("security/macro.docm", "application/vnd.ms-word.document.macroenabled.12").use { scenario ->
            Reader.waitForDocument(scenario)
            assertTrue(bodyText(scenario).contains("A macro-enabled document."))
            assertEquals(false, Reader.js(scenario, "document.getElementById('fidelityBar').hidden"))
            assertTrue(Reader.text(scenario, "#fidelityBar").orEmpty().contains("macro"))
        }
    }

    @Test
    fun theFidelityBarListsWhatIsNotShown() {
        Reader.open("features/not-displayed.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            assertEquals(false, Reader.js(scenario, "document.getElementById('fidelityBar').hidden"))
            val bar = Reader.text(scenario, "#fidelityBar").orEmpty()
            for (word in listOf("text box", "equation", "chart", "SmartArt", "shape")) {
                assertTrue("fidelity bar mentions $word: $bar", bar.contains(word, ignoreCase = true))
            }
        }
    }

    @Test
    fun remoteImagesAndTemplatesAreNeverRequested() {
        for (name in listOf("security/remote-image.docx", "security/remote-template.docx")) {
            Reader.open(name).use { scenario ->
                Reader.waitForDocument(scenario)
                scenario.onActivity { activity ->
                    val outside = activity.blockedRequests.filter { !it.startsWith(ReaderAssets.ORIGIN) }
                    assertEquals("$name made no request outside the reader", emptyList<String>(), outside)
                }
            }
        }
    }

    @Test
    fun aZipBombIsRefusedByTheCore() {
        Reader.open("security/zip-bomb.docx").use { scenario ->
            Reader.waitForState(scenario)
            assertEquals("This document could not be opened", Reader.text(scenario, "#stateEyebrow"))
            assertTrue(Reader.text(scenario, "#stateMessage").orEmpty().contains("64 MB limit"))
        }
    }

    @Test
    fun aTruncatedFileShowsTheCoresMessage() {
        Reader.open("security/truncated.docx").use { scenario ->
            Reader.waitForState(scenario)
            assertEquals("This document could not be opened", Reader.text(scenario, "#stateEyebrow"))
            assertEquals("truncated.docx", Reader.text(scenario, "#stateTitle"))
        }
    }

    @Test
    fun aFileOverTheLimitIsRefusedByItsSize() {
        Reader.open("huge.docx").use { scenario ->
            Reader.waitForState(scenario)
            assertTrue(Reader.text(scenario, "#stateMessage").orEmpty().contains("64 MB limit"))
        }
    }

    @Test
    fun aStreamOverTheLimitIsRefusedWhileReading() {
        Reader.open("huge.docx-nosize").use { scenario ->
            Reader.waitForState(scenario)
            assertTrue(Reader.text(scenario, "#stateMessage").orEmpty().contains("64 MB limit"))
        }
        // The app stopped reading near the limit instead of taking the whole stream.
        val written = Reader.written("huge.docx-nosize")
        assertTrue("delivered $written of ${FixtureProvider.HUGE_BYTES} bytes", written in 1 until FixtureProvider.HUGE_BYTES)
    }

    @Test
    fun aLegacyDocOffersAnotherApp() {
        val chooser = object : Instrumentation.ActivityMonitor() {
            var seen: Intent? = null
            override fun onStartActivity(intent: Intent): Instrumentation.ActivityResult? {
                if (intent.action != Intent.ACTION_CHOOSER) return null
                seen = intent
                return Instrumentation.ActivityResult(Activity.RESULT_CANCELED, null)
            }
        }
        Reader.instrumentation.addMonitor(chooser)
        try {
            Reader.open("security/legacy.doc", "application/msword").use { scenario ->
                Reader.waitForState(scenario)
                assertEquals("Word 97-2003 document", Reader.text(scenario, "#stateEyebrow"))
                assertEquals("Open in another app", Reader.text(scenario, "#stateEditorButton")?.trim())
                Reader.js(scenario, "document.getElementById('stateEditorButton').click()")
                val deadline = SystemClock.uptimeMillis() + 10_000
                while (chooser.seen == null && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(100)
                val target = if (android.os.Build.VERSION.SDK_INT >= 33) {
                    chooser.seen!!.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    chooser.seen!!.getParcelableExtra(Intent.EXTRA_INTENT)
                }
                assertEquals(Intent.ACTION_VIEW, target!!.action)
                assertEquals(FixtureProvider.uri("security/legacy.doc"), target.data)
            }
        } finally {
            Reader.instrumentation.removeMonitor(chooser)
        }
    }

    @Test
    fun aLongDocumentRendersAndScrollsByTouch() {
        Reader.open("features/long-300-pages.docx").use { scenario ->
            Reader.waitForDocument(scenario, timeoutMs = 120_000)
            assertEquals(0L, Reader.js(scenario, "Math.round(${Reader.SCROLL_TOP})").asLong())
            Reader.swipeUp(scenario)
            Reader.waitFor(scenario, "${Reader.SCROLL_TOP} > 100")
        }
    }

    @Test
    fun readingViewReflowsTheDocument() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            Reader.js(scenario, "document.getElementById('viewReading').click()")
            Reader.waitFor(scenario, "document.documentElement.dataset.view === 'reading'")
            Reader.waitForDocument(scenario)
            assertTrue(bodyText(scenario).contains("Styles and inheritance"))
            Reader.js(scenario, "document.getElementById('viewPage').click()")
            Reader.waitFor(scenario, "document.documentElement.dataset.view === 'page'")
        }
    }

    @Test
    fun rotatingKeepsTheDocument() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            var before: Activity? = null
            scenario.onActivity {
                before = it
                it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            Reader.waitFor(scenario, "window.innerWidth > window.innerHeight")
            scenario.onActivity { assertTrue("the activity was kept", it === before) }
            Reader.waitForDocument(scenario)
            assertTrue(bodyText(scenario).contains("Styles and inheritance"))
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            Reader.waitFor(scenario, "window.innerWidth < window.innerHeight")
        }
    }

    @Test
    fun findCountsMatchesInTheDocument() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            Reader.js(scenario, "var f=document.getElementById('findInput'); f.value='Chapter'; f.dispatchEvent(new Event('input'))")
            Reader.waitFor(scenario, "/^1\\/[0-9]+$/.test(document.getElementById('findCount').textContent)")
        }
    }

    @Test
    fun externalLinksGoToAnotherApp() {
        val browser = object : Instrumentation.ActivityMonitor() {
            var seen: Uri? = null
            override fun onStartActivity(intent: Intent): Instrumentation.ActivityResult? {
                if (intent.action != Intent.ACTION_VIEW || intent.data?.scheme?.startsWith("http") != true) return null
                seen = intent.data
                return Instrumentation.ActivityResult(Activity.RESULT_CANCELED, null)
            }
        }
        Reader.instrumentation.addMonitor(browser)
        try {
            Reader.open("features/hyperlinks.docx").use { scenario ->
                Reader.waitForDocument(scenario)
                Reader.js(scenario, "document.querySelector('#docBody a[data-ld-link=\"external\"][href^=\"http\"]').click()")
                val deadline = SystemClock.uptimeMillis() + 10_000
                while (browser.seen == null && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(100)
                assertTrue("a web link was handed to another app", browser.seen != null)
            }
        } finally {
            Reader.instrumentation.removeMonitor(browser)
        }
    }

    @Test
    fun thePageCannotReachTheNetwork() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            Reader.js(scenario, "window.__fetch = 'pending'; fetch('https://example.com/').then(() => { window.__fetch = 'loaded' }, () => { window.__fetch = 'refused' })")
            Reader.waitFor(scenario, "window.__fetch !== 'pending'")
            assertEquals("refused", Reader.js(scenario, "window.__fetch"))
        }
    }

    @Test
    fun theAppRefusesRemotePagesEvenWithoutThePagePolicy() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            scenario.onActivity { it.webViewForTest!!.loadUrl("https://example.com/") }
            val deadline = SystemClock.uptimeMillis() + 10_000
            var blocked = false
            while (!blocked && SystemClock.uptimeMillis() < deadline) {
                scenario.onActivity { blocked = "https://example.com/" in it.blockedRequests }
                if (!blocked) SystemClock.sleep(100)
            }
            assertTrue("the request was answered by the app, not the network", blocked)
        }
    }

    @Test
    fun onlyTheCurrentPackageIsServed() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            Reader.js(
                scenario,
                "window.__old = 'pending'; fetch('/pkg/999/document.docx').then(r => { window.__old = String(r.status) }, () => { window.__old = 'refused' })",
            )
            Reader.waitFor(scenario, "window.__old !== 'pending'")
            assertEquals("403", Reader.js(scenario, "window.__old"))
        }
    }

    @Test
    fun thePageCannotNavigateAway() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            Reader.js(scenario, "location.href = 'https://example.com/'")
            SystemClock.sleep(2_000)
            assertTrue(bodyText(scenario).contains("Styles and inheritance"))
            scenario.onActivity {
                assertFalse("the navigation was never requested", "https://example.com/" in it.blockedRequests)
            }
        }
    }

    @Test
    fun filePathsAreRefusedEvenWhenTheAppCouldReadThem() {
        val private = java.io.File(Reader.context.filesDir, "private.docx")
        Reader.instrumentation.context.assets.open("fixtures/features/styles.docx").use { input ->
            private.outputStream().use { input.copyTo(it) }
        }
        try {
            val view = Intent(Intent.ACTION_VIEW)
                .setDataAndType(Uri.fromFile(private), Reader.DOCX)
                .setClass(Reader.context, MainActivity::class.java)
            ActivityScenario.launch<MainActivity>(view).use { scenario ->
                Reader.waitForState(scenario)
                assertTrue(Reader.text(scenario, "#stateMessage").orEmpty().contains("could not read this file"))
            }
        } finally {
            private.delete()
        }
    }

    @Test
    fun sharedFilesOpen() {
        val share = Intent(Intent.ACTION_SEND)
            .setType(Reader.DOCX)
            .putExtra(Intent.EXTRA_STREAM, FixtureProvider.uri("features/styles.docx"))
            .setClass(Reader.context, MainActivity::class.java)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        ActivityScenario.launch<MainActivity>(share).use { scenario ->
            Reader.waitForDocument(scenario)
            assertTrue(bodyText(scenario).contains("Styles and inheritance"))
        }
    }

    @Test
    fun theLauncherShowsTheWelcomePage() {
        val launch = Reader.context.packageManager.getLaunchIntentForPackage(Reader.context.packageName)!!
        ActivityScenario.launch<MainActivity>(launch).use { scenario ->
            Reader.waitForState(scenario)
            assertEquals("Read Word documents without opening an office suite.", Reader.text(scenario, "#stateTitle"))
        }
    }

    @Test
    fun theOpenButtonUsesTheSystemFilePicker() {
        val picker = object : Instrumentation.ActivityMonitor() {
            var seen: Intent? = null
            override fun onStartActivity(intent: Intent): Instrumentation.ActivityResult? {
                if (intent.action != Intent.ACTION_OPEN_DOCUMENT) return null
                seen = intent
                val result = Intent().setData(FixtureProvider.uri("features/hyperlinks.docx"))
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                return Instrumentation.ActivityResult(Activity.RESULT_OK, result)
            }
        }
        Reader.instrumentation.addMonitor(picker)
        try {
            Reader.open("features/styles.docx").use { scenario ->
                Reader.waitForDocument(scenario)
                Reader.js(scenario, "document.getElementById('openButton').click()")
                Reader.waitFor(scenario, "document.getElementById('fileName').textContent === 'hyperlinks.docx'")
                Reader.waitForDocument(scenario)
                assertTrue(picker.seen!!.hasCategory(Intent.CATEGORY_OPENABLE))
            }
        } finally {
            Reader.instrumentation.removeMonitor(picker)
        }
    }

    @Test
    fun aCrashedRendererIsReplacedAndTheDocumentReturns() {
        Reader.open("features/styles.docx").use { scenario ->
            Reader.waitForDocument(scenario)
            val before = Reader.webView(scenario)
            scenario.onActivity { it.webViewForTest!!.loadUrl("chrome://crash") }
            val deadline = SystemClock.uptimeMillis() + 20_000
            while (Reader.webView(scenario) === before && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(100)
            assertNotSame(before, Reader.webView(scenario))
            Reader.waitForDocument(scenario)
            assertTrue(bodyText(scenario).contains("Styles and inheritance"))
        }
    }

    private fun Any?.asLong(): Long = (this as Number).toLong()
}

/** Runs only on devices whose WebView is too old for the reader page. */
@RunWith(AndroidJUnit4::class)
class OldWebViewTest {
    @Test
    fun anOldWebViewGetsAPlainMessageInsteadOfABrokenPage() {
        assumeTrue("needs a WebView older than ${WebViewSupport.MINIMUM_MAJOR}", Reader.webViewMajor < WebViewSupport.MINIMUM_MAJOR)
        Reader.open("features/styles.docx").use { scenario ->
            scenario.onActivity { activity ->
                assertEquals(null, activity.webViewForTest)
                val root = activity.findViewById<android.view.ViewGroup>(android.R.id.content).getChildAt(0) as android.view.ViewGroup
                val message = (root.getChildAt(0) as TextView).text.toString()
                assertTrue(message, message.contains("Android System WebView ${WebViewSupport.MINIMUM_MAJOR} or later"))
                assertTrue(message, message.contains(WebView.getCurrentWebViewPackage()!!.versionName!!))
            }
        }
    }
}
