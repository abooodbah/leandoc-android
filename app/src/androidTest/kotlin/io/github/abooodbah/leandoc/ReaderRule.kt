package io.github.abooodbah.leandoc

import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import android.view.MotionEvent
import android.webkit.WebView
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.json.JSONObject
import org.json.JSONTokener
import org.junit.Assert.fail

/** Helpers for driving the reader page from instrumented tests. */
object Reader {
    val instrumentation = InstrumentationRegistry.getInstrumentation()!!
    val context = instrumentation.targetContext!!

    const val DOCX = "application/vnd.openxmlformats-officedocument.wordprocessingml.document"

    fun viewIntent(uri: Uri, type: String = DOCX): Intent =
        Intent(Intent.ACTION_VIEW)
            .setDataAndType(uri, type)
            .setClass(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)

    fun open(name: String, type: String = DOCX): ActivityScenario<MainActivity> =
        ActivityScenario.launch(viewIntent(FixtureProvider.uri(name), type))

    fun webView(scenario: ActivityScenario<MainActivity>): WebView {
        var view: WebView? = null
        scenario.onActivity { view = it.webViewForTest }
        return view ?: error("no WebView")
    }

    /** Runs [script] in the page and returns its result decoded from JSON. */
    fun js(scenario: ActivityScenario<MainActivity>, script: String): Any? {
        val latch = CountDownLatch(1)
        var result: String? = null
        scenario.onActivity { activity ->
            val view = activity.webViewForTest
            if (view == null) {
                latch.countDown()
            } else {
                view.evaluateJavascript(script) { value ->
                    result = value
                    latch.countDown()
                }
            }
        }
        if (!latch.await(15, TimeUnit.SECONDS)) fail("script timed out: $script")
        // evaluateJavascript gives JSON; JSON null comes back as JSONObject.NULL.
        return result?.let { JSONTokener(it).nextValue() }?.takeIf { it != JSONObject.NULL }
    }

    fun waitFor(
        scenario: ActivityScenario<MainActivity>,
        condition: String,
        timeoutMs: Long = 30_000,
    ) {
        val deadline = SystemClock.uptimeMillis() + timeoutMs
        while (SystemClock.uptimeMillis() < deadline) {
            if (js(scenario, "Boolean($condition)") == true) return
            SystemClock.sleep(100)
        }
        fail("timed out waiting for $condition; state: " + js(scenario, STATE))
    }

    fun waitForDocument(scenario: ActivityScenario<MainActivity>, timeoutMs: Long = 45_000) =
        waitFor(scenario, "$RENDERED && !document.getElementById('readerPane').hidden", timeoutMs)

    /** The empty page or a fallback page: anything shown instead of a document. */
    fun waitForState(scenario: ActivityScenario<MainActivity>) =
        waitFor(scenario, "!document.getElementById('statePane').hidden && " +
            "(document.getElementById('statePane').dataset.state === 'fallback' || " +
            "document.getElementById('statePane').dataset.state === 'empty')")

    fun text(scenario: ActivityScenario<MainActivity>, selector: String): String? =
        js(scenario, "(function(){var n=document.querySelector(${JSONObject.quote(selector)});return n?n.textContent:null})()")
            as? String

    /** The page scrolls inside #docScroller, not the window. */
    const val SCROLL_TOP = "document.getElementById('docScroller').scrollTop"

    /** A finger swipe up across the middle of the WebView, as a reader scrolls. */
    fun swipeUp(scenario: ActivityScenario<MainActivity>) {
        val view = webView(scenario)
        val location = IntArray(2)
        instrumentation.runOnMainSync { view.getLocationOnScreen(location) }
        val x = location[0] + view.width / 2f
        val startY = location[1] + view.height * 0.8f
        val endY = location[1] + view.height * 0.2f
        val down = SystemClock.uptimeMillis()
        instrumentation.sendPointerSync(MotionEvent.obtain(down, down, MotionEvent.ACTION_DOWN, x, startY, 0))
        val steps = 12
        for (step in 1..steps) {
            val y = startY + (endY - startY) * step / steps
            val time = down + step * 10L
            instrumentation.sendPointerSync(MotionEvent.obtain(down, time, MotionEvent.ACTION_MOVE, x, y, 0))
        }
        val up = down + (steps + 1) * 10L
        instrumentation.sendPointerSync(MotionEvent.obtain(down, up, MotionEvent.ACTION_UP, x, endY, 0))
    }

    /** Bytes the fixture provider delivered for its last stream of [path]. */
    fun written(path: String): Long {
        val deadline = SystemClock.uptimeMillis() + 15_000
        while (SystemClock.uptimeMillis() < deadline) {
            val bytes = context.contentResolver
                .call(FixtureProvider.uri(path), "written", path, null)?.getLong("bytes") ?: -1
            if (bytes >= 0) return bytes
            SystemClock.sleep(100)
        }
        fail("the fixture stream for $path did not finish")
        return -1
    }

    /** The WebView major version, or 0 when there is none. */
    val webViewMajor: Int
        get() = WebViewSupport.majorVersion(WebView.getCurrentWebViewPackage()?.versionName) ?: 0

    private const val RENDERED = "document.documentElement.dataset.renderState === 'ready'"
    private const val STATE =
        "JSON.stringify({render: document.documentElement.dataset.renderState," +
            " state: (document.getElementById('statePane')||{dataset:{}}).dataset.state," +
            " eyebrow: (document.getElementById('stateEyebrow')||{}).textContent," +
            " title: (document.getElementById('stateTitle')||{}).textContent," +
            " message: (document.getElementById('stateMessage')||{}).textContent," +
            " readerHidden: (document.getElementById('readerPane')||{}).hidden})"
}
