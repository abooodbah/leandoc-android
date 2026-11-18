package io.github.abooodbah.leandoc

import android.content.Intent
import android.content.pm.ActivityInfo
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
import kotlin.math.roundToInt

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
        var activity: MainActivity? = null
        scenario.onActivity { activity = it }
        val target = activity!!
        val location = IntArray(2)
        instrumentation.runOnMainSync { view.getLocationOnScreen(location) }
        val x = location[0] + view.width / 2f
        val startY = location[1] + view.height * 0.8f
        val endY = location[1] + view.height * 0.2f
        val down = SystemClock.uptimeMillis()
        Focus.touch(target, MotionEvent.ACTION_DOWN, down, down, x, startY)
        val steps = 12
        for (step in 1..steps) {
            val y = startY + (endY - startY) * step / steps
            val time = down + step * 10L
            Focus.touch(target, MotionEvent.ACTION_MOVE, down, time, x, y)
        }
        val up = down + (steps + 1) * 10L
        Focus.touch(target, MotionEvent.ACTION_UP, down, up, x, endY)
    }

    /** The page view's pages. */
    const val PAGES = "document.querySelectorAll('#docBody .docx-wrapper > section.docx')"

    /** The reader applies a zoom in the frame after it sets data-zoom. */
    const val ZOOM_SETTLED = "!document.documentElement.hasAttribute('data-zoom-pending')"

    /** The zoom in percent, as the reader shows it. */
    fun zoom(scenario: ActivityScenario<MainActivity>): String =
        js(scenario, "document.documentElement.dataset.zoom") as String

    fun pageCount(scenario: ActivityScenario<MainActivity>): Int =
        (js(scenario, "$PAGES.length") as Number).toInt()

    /**
     * Scrolls page [index] to the top of the document area and waits until
     * it is drawn: a page off screen is not (content-visibility: auto), and
     * the boxes reported inside it can mix zoomed and unzoomed units.
     */
    fun bringPageIntoView(scenario: ActivityScenario<MainActivity>, index: Int) {
        js(scenario, "$PAGES[$index].scrollIntoView({block: 'start'})")
        SystemClock.sleep(300)
    }

    /** Makes the WebView [dp] wide, as on a smaller screen, and waits for the new zoom. */
    fun narrow(scenario: ActivityScenario<MainActivity>, dp: Int) {
        val before = zoom(scenario)
        scenario.onActivity { activity ->
            val view = activity.webViewForTest!!
            view.layoutParams = view.layoutParams.apply {
                width = (dp * activity.resources.displayMetrics.density).roundToInt()
            }
        }
        waitFor(scenario, "document.documentElement.clientWidth <= $dp")
        waitFor(scenario, "document.documentElement.dataset.zoom !== '$before' && $ZOOM_SETTLED")
    }

    /** Runs [block] in landscape, once the page has its new zoom, then turns back. */
    fun landscape(scenario: ActivityScenario<MainActivity>, block: () -> Unit) {
        val before = zoom(scenario)
        scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
        try {
            waitFor(scenario, "window.innerWidth > window.innerHeight")
            waitFor(scenario, "document.documentElement.dataset.zoom !== '$before' && $ZOOM_SETTLED")
            block()
        } finally {
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            waitFor(scenario, "window.innerWidth < window.innerHeight")
        }
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
