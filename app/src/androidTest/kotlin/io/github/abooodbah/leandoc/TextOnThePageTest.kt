package io.github.abooodbah.leandoc

import android.content.pm.ActivityInfo
import android.os.SystemClock
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.roundToInt

/**
 * At the zoom that fits a page to a phone, every line stays on its page.
 * The reader splits pages at 100% and gives each section of a split page
 * the height it was measured at; drawn smaller, lines and table rows take a
 * little more room, and the last line of a column or the last row of a
 * table went to a column beside the page, cut off at the page's edge.
 *
 * The documents are the ones the desktop's reader tests build
 * (fixtures/reader/, from scripts/desktop-test-documents.mjs). Each test
 * opens one through ACTION_VIEW with a content:// URI, then brings each
 * page into view, as scrolling does, before it looks at the page.
 */
@RunWith(AndroidJUnit4::class)
class TextOnThePageTest {

    @Before
    fun needsAWebViewThatRunsThePage() {
        assumeTrue(
            "docx-preview needs WebView ${WebViewSupport.MINIMUM_MAJOR} or later",
            Reader.webViewMajor >= WebViewSupport.MINIMUM_MAJOR,
        )
    }

    @Test
    fun aTwoColumnPaperKeepsEveryLineOnThisPhone() = open(PAPER) { scenario ->
        everyPageKeepsItsText(scenario)
    }

    @Test
    fun aTwoColumnPaperKeepsEveryLineOnANarrowerPhone() = open(PAPER) { scenario ->
        narrow(scenario, 360)
        everyPageKeepsItsText(scenario)
    }

    @Test
    fun aTwoColumnPaperKeepsEveryLineOnASmallPhone() = open(PAPER) { scenario ->
        narrow(scenario, 320)
        everyPageKeepsItsText(scenario)
    }

    @Test
    fun aTwoColumnPaperKeepsEveryLineInLandscape() = open(PAPER) { scenario ->
        landscape(scenario) {
            everyPageKeepsItsText(scenario)
            narrow(scenario, 600)
            everyPageKeepsItsText(scenario)
        }
    }

    @Test
    fun wideTablesKeepEveryRowOnThisPhone() = open(TABLES) { scenario ->
        everyPageKeepsItsText(scenario)
    }

    @Test
    fun wideTablesKeepEveryRowOnASmallPhone() = open(TABLES) { scenario ->
        narrow(scenario, 320)
        everyPageKeepsItsText(scenario)
    }

    @Test
    fun wideTablesKeepEveryRowInLandscape() = open(TABLES) { scenario ->
        landscape(scenario) {
            everyPageKeepsItsText(scenario)
            narrow(scenario, 600)
            everyPageKeepsItsText(scenario)
        }
    }

    @Test
    fun twoColumnSectionsKeepEveryLineOnThisPhone() = open(SECTIONS) { scenario ->
        everyPageKeepsItsText(scenario)
    }

    @Test
    fun aContentsPageKeepsEveryLineOnThisPhone() = open(CONTENTS) { scenario ->
        everyPageKeepsItsText(scenario)
    }

    @Test
    fun everyLineStaysAfterZoomSteps() = open(TABLES) { scenario ->
        // 50%, 67% and 50% again: wider than the screen, so the page scrolls sideways.
        for (button in listOf("zoomIn", "zoomIn", "zoomOut")) {
            val before = zoom(scenario)
            Reader.js(scenario, "document.getElementById('$button').click()")
            Reader.waitFor(scenario, "document.documentElement.dataset.zoom !== '$before' && " + ZOOM_SETTLED)
            everyPageKeepsItsText(scenario, fitsTheWidth = false)
        }
    }

    @Test
    fun atFullSizeEveryPageKeepsItsPaperHeight() = open(TABLES) { scenario ->
        // From the fit zoom up to 100%: a section grown to hold its text at
        // a smaller zoom gets back the height the reader measured.
        while (zoom(scenario) != "100") {
            val before = zoom(scenario)
            Reader.js(scenario, "document.getElementById('zoomIn').click()")
            Reader.waitFor(scenario, "document.documentElement.dataset.zoom !== '$before' && " + ZOOM_SETTLED)
        }
        everyPageKeepsItsText(scenario, fitsTheWidth = false)
        assertEquals("", Reader.js(scenario, PAGES_TALLER_THAN_PAPER))
    }

    @Test
    fun theLinesInViewStayOnTheirPagesAfterScrollingByTouch() = open(PAPER) { scenario ->
        repeat(3) {
            Reader.swipeUp(scenario)
            SystemClock.sleep(600)
            val shown = Reader.js(scenario, PAGES_IN_VIEW) as String
            assertTrue("a page is in view", shown.isNotEmpty())
            for (index in shown.split(',').map { it.toInt() }) {
                assertPageKeepsItsText(scenario, index)
            }
        }
        assertTrue("the paper scrolled", (Reader.js(scenario, Reader.SCROLL_TOP) as Number).toDouble() > 100)
    }

    private fun open(name: String, block: (ActivityScenario<MainActivity>) -> Unit) =
        Reader.open(name).use { scenario ->
            Reader.waitForDocument(scenario)
            Reader.waitFor(scenario, ZOOM_SETTLED)
            assertEquals("page", Reader.js(scenario, "document.documentElement.dataset.view"))
            block(scenario)
        }

    private fun zoom(scenario: ActivityScenario<MainActivity>): String =
        Reader.js(scenario, "document.documentElement.dataset.zoom") as String

    /** Brings each page into view in turn and checks that its text is on it. */
    private fun everyPageKeepsItsText(scenario: ActivityScenario<MainActivity>, fitsTheWidth: Boolean = true) {
        val count = (Reader.js(scenario, "$PAGES.length") as Number).toInt()
        assertTrue("the document has pages", count > 0)
        for (index in 0 until count) {
            Reader.js(scenario, "$PAGES[$index].scrollIntoView({block: 'start'})")
            assertPageKeepsItsText(scenario, index)
        }
        if (fitsTheWidth) {
            assertEquals(
                "the document area scrolls sideways",
                true,
                Reader.js(scenario, "(function(){var a=document.getElementById('docScroller');return a.scrollWidth <= a.clientWidth + 0.5})()"),
            )
        }
    }

    private fun assertPageKeepsItsText(scenario: ActivityScenario<MainActivity>, index: Int) {
        // The app looks at a page once it comes near the screen, a frame or
        // two after scrolling; a page that keeps text off it fails here.
        val deadline = SystemClock.uptimeMillis() + 3_000
        var problem: String
        do {
            SystemClock.sleep(150)
            problem = Reader.js(scenario, "($TEXT_OFF_PAGE)($index)") as String
        } while (problem.isNotEmpty() && SystemClock.uptimeMillis() < deadline)
        if (problem.isNotEmpty()) fail(problem)
    }

    /** Makes the WebView [dp] wide, as on a smaller screen, and waits for the new zoom. */
    private fun narrow(scenario: ActivityScenario<MainActivity>, dp: Int) {
        val before = zoom(scenario)
        scenario.onActivity { activity ->
            val view = activity.webViewForTest!!
            view.layoutParams = view.layoutParams.apply {
                width = (dp * activity.resources.displayMetrics.density).roundToInt()
            }
        }
        Reader.waitFor(scenario, "document.documentElement.clientWidth <= $dp")
        Reader.waitFor(scenario, "document.documentElement.dataset.zoom !== '$before' && $ZOOM_SETTLED")
    }

    private fun landscape(scenario: ActivityScenario<MainActivity>, block: () -> Unit) {
        val before = zoom(scenario)
        scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
        try {
            Reader.waitFor(scenario, "window.innerWidth > window.innerHeight")
            Reader.waitFor(scenario, "document.documentElement.dataset.zoom !== '$before' && $ZOOM_SETTLED")
            block()
        } finally {
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            Reader.waitFor(scenario, "window.innerWidth < window.innerHeight")
        }
    }

    private companion object {
        const val PAPER = "reader/two-column-floats.docx"
        const val TABLES = "reader/wide-tables.docx"
        const val SECTIONS = "reader/two-column-sections.docx"
        const val CONTENTS = "reader/contents-leaders.docx"

        const val PAGES = "document.querySelectorAll('#docBody .docx-wrapper > section.docx')"

        /** The reader applies a zoom in the frame after it sets data-zoom. */
        const val ZOOM_SETTLED = "!document.documentElement.hasAttribute('data-zoom-pending')"

        /** An empty string when every page is as tall as its paper. */
        const val PAGES_TALLER_THAN_PAPER = """(function () {
            var taller = [];
            $PAGES.forEach(function (page, index) {
              var paper = parseFloat(getComputedStyle(page).minHeight);
              if (page.offsetHeight > paper + 1) taller.push('page ' + (index + 1) + ' is ' + page.offsetHeight + ' px tall, its paper ' + paper);
            });
            return taller.join('; ');
          })()"""

        /** The indexes of the pages on screen, as "0,1". */
        const val PAGES_IN_VIEW = """(function () {
            var area = document.getElementById('docScroller').getBoundingClientRect();
            var shown = [];
            $PAGES.forEach(function (page, index) {
              var box = page.getBoundingClientRect();
              var factor = parseFloat(page.parentElement.style.zoom) || 1;
              var scale = factor !== 1 && Math.abs(box.width - page.offsetWidth) < 1 ? factor : 1;
              if (box.bottom * scale > area.top && box.top * scale < area.bottom) shown.push(index);
            });
            return shown.join(',');
          })()"""

        /**
         * An empty string when every piece of text on the page with the given
         * index is drawn inside the page; otherwise the first piece that is
         * not. The page clips at its edge, so text past it is not shown.
         * Boxes are compared with the page's own, in the same units, which
         * holds on WebView before 128 too.
         */
        const val TEXT_OFF_PAGE = """function (index) {
            var page = $PAGES[index];
            if (!page) return 'no page ' + (index + 1);
            var box = page.getBoundingClientRect();
            var walker = document.createTreeWalker(page, NodeFilter.SHOW_TEXT);
            var range = document.createRange();
            for (var node = walker.nextNode(); node; node = walker.nextNode()) {
              if (!/\S/.test(node.textContent)) continue;
              range.selectNodeContents(node);
              var rects = range.getClientRects();
              for (var i = 0; i < rects.length; i++) {
                var r = rects[i];
                if (r.width < 0.5 || r.height < 0.5) continue;
                if (r.left < box.left - 0.5 || r.right > box.right + 0.5 ||
                    r.top < box.top - 0.5 || r.bottom > box.bottom + 0.5) {
                  return 'page ' + (index + 1) + ' at zoom ' + document.documentElement.dataset.zoom +
                    '%: "' + node.textContent.trim().slice(0, 40) + '" is drawn at x ' + Math.round(r.left) +
                    ' to ' + Math.round(r.right) + ', y ' + Math.round(r.top) + ' to ' + Math.round(r.bottom) +
                    ', past the page at x ' + Math.round(box.left) + ' to ' + Math.round(box.right) +
                    ', y ' + Math.round(box.top) + ' to ' + Math.round(box.bottom);
                }
              }
            }
            return '';
          }"""
    }
}
