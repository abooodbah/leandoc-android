package io.github.abooodbah.leandoc

import android.app.Activity
import android.os.ParcelFileDescriptor
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The system's light and dark switch while a document is open, as at sunset.
 * Android used to create the activity again: the file was read and cleaned
 * again, the page reloaded and the reading position was lost. Now the app
 * keeps the WebView and only the colours change, and only for System.
 */
@RunWith(AndroidJUnit4::class)
class SystemThemeTest {

    @Before
    fun setUp() {
        assumeTrue(Reader.webViewMajor >= WebViewSupport.MINIMUM_MAJOR)
        Reader.context.getSharedPreferences("reader", 0).edit().clear().commit()
        night(false)
    }

    @After
    fun tearDown() {
        night(false)
        Reader.context.getSharedPreferences("reader", 0).edit().clear().commit()
    }

    @Test
    fun theSystemSwitchKeepsTheDocumentAndRecolours() = openLong { scenario ->
        assertEquals("#faf9f7", canvas(scenario))
        val before = place(scenario)
        night(true)
        Reader.waitFor(scenario, "($CANVAS) === '#0e0d0b'", timeoutMs = 15_000)
        assertSame(scenario, before)
        waitForBar(scenario, 0xFF1A1917.toInt())
        night(false)
        Reader.waitFor(scenario, "($CANVAS) === '#faf9f7'", timeoutMs = 15_000)
        assertSame(scenario, before)
        waitForBar(scenario, 0xFFFFFFFF.toInt())
    }

    @Test
    fun aChosenThemeStaysAsItIs() = openLong { scenario ->
        Reader.js(scenario, "window.LeanDocHost.receive(JSON.stringify({type: 'theme', theme: 'dusk'}))")
        Reader.js(scenario, "window.webkit.messageHandlers.leandoc.postMessage(JSON.stringify({type: 'setTheme', theme: 'dusk'}))")
        waitForBar(scenario, 0xFF29241E.toInt())
        val before = place(scenario)
        night(true)
        android.os.SystemClock.sleep(4_000)
        assertSame(scenario, before)
        assertEquals("dusk", Reader.js(scenario, "document.documentElement.getAttribute('data-theme')"))
        assertEquals("#221e19", canvas(scenario))
        waitForBar(scenario, 0xFF29241E.toInt())
    }

    private class Place(val activity: Activity, val scrollTop: Int)

    private fun place(scenario: ActivityScenario<MainActivity>): Place {
        Reader.js(scenario, "(function () { var s = document.getElementById('docScroller'); s.scrollTop = (s.scrollHeight - s.clientHeight) * 0.5; })()")
        Reader.waitFor(scenario, "${Reader.SCROLL_TOP} > 1000")
        android.os.SystemClock.sleep(1_000)
        var activity: Activity? = null
        scenario.onActivity { activity = it }
        val top = (Reader.js(scenario, "Math.round(${Reader.SCROLL_TOP})") as Number).toInt()
        return Place(activity!!, top)
    }

    private fun assertSame(scenario: ActivityScenario<MainActivity>, before: Place) {
        var activity: Activity? = null
        scenario.onActivity { activity = it }
        assertTrue("the activity was kept", activity === before.activity)
        val top = (Reader.js(scenario, "Math.round(${Reader.SCROLL_TOP})") as Number).toInt()
        assertTrue("reading position $top, was ${before.scrollTop}", Math.abs(top - before.scrollTop) < 200)
    }

    private fun canvas(scenario: ActivityScenario<MainActivity>) = Reader.js(scenario, CANVAS) as String

    private fun waitForBar(scenario: ActivityScenario<MainActivity>, color: Int) {
        val deadline = android.os.SystemClock.uptimeMillis() + 10_000
        var bar = 0
        while (android.os.SystemClock.uptimeMillis() < deadline) {
            scenario.onActivity { bar = it.window.statusBarColor }
            if (bar == color) return
            android.os.SystemClock.sleep(100)
        }
        assertEquals(color, bar)
    }

    private fun night(on: Boolean) {
        val out = Reader.instrumentation.uiAutomation.executeShellCommand("cmd uimode night " + if (on) "yes" else "no")
        ParcelFileDescriptor.AutoCloseInputStream(out).use { it.readBytes() }
    }

    private fun openLong(block: (ActivityScenario<MainActivity>) -> Unit) =
        Reader.open("features/long-300-pages.docx").use { scenario ->
            Reader.waitForDocument(scenario, timeoutMs = 120_000)
            block(scenario)
        }

    private companion object {
        const val CANVAS = "getComputedStyle(document.documentElement).getPropertyValue('--canvas').trim().toLowerCase()"
    }
}
