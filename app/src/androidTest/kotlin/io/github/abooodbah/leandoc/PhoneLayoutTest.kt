package io.github.abooodbah.leandoc

import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.roundToInt

/**
 * On a phone a page must fit the screen width when a document opens, and the
 * reader's controls must take no more than two rows. In v0.1.0 the default
 * zoom stopped at 50%, which leaves a Letter page wider than a phone, and the
 * desktop toolbar wrapped to three rows under the file name.
 */
@RunWith(AndroidJUnit4::class)
class PhoneLayoutTest {

    @Before
    fun needsAWebViewThatRunsThePage() {
        assumeTrue(
            "docx-preview needs WebView ${WebViewSupport.MINIMUM_MAJOR} or later",
            Reader.webViewMajor >= WebViewSupport.MINIMUM_MAJOR,
        )
    }

    @Test
    fun aLetterPageFitsThisPhone() = open("features/styles.docx") { scenario ->
        assertEquals("", Reader.js(scenario, PAGE_PROBLEMS))
    }

    @Test
    fun anA4PageFitsThisPhone() = open("store/library-renovation-report.docx") { scenario ->
        assertEquals("", Reader.js(scenario, PAGE_PROBLEMS))
    }

    @Test
    fun aPageFitsASmallPhone() = open("features/styles.docx") { scenario ->
        narrow(scenario, 320)
        Reader.waitFor(scenario, "document.documentElement.dataset.zoom !== undefined")
        Reader.waitFor(scenario, "($PAGE_PROBLEMS) === ''", timeoutMs = 10_000)
    }

    @Test
    fun theControlsTakeTwoRowsOnThisPhone() = open("features/styles.docx") { scenario ->
        assertEquals("", Reader.js(scenario, controlProblems(maxRows = 2)))
    }

    @Test
    fun theControlsTakeTwoRowsOnANarrowerPhone() = open("features/styles.docx") { scenario ->
        narrow(scenario, 360)
        assertEquals("", Reader.js(scenario, controlProblems(maxRows = 2)))
    }

    @Test
    fun theControlsStayOnScreenOnASmallPhone() = open("features/styles.docx") { scenario ->
        narrow(scenario, 320)
        assertEquals("", Reader.js(scenario, controlProblems(maxRows = 3)))
    }

    @Test
    fun theControlsTakeTwoRowsInLandscape() = open("features/styles.docx") { scenario ->
        narrow(scenario, 640)
        assertEquals("", Reader.js(scenario, controlProblems(maxRows = 2)))
    }

    private fun open(name: String, block: (ActivityScenario<MainActivity>) -> Unit) =
        Reader.open(name).use { scenario ->
            Reader.waitForDocument(scenario)
            block(scenario)
        }

    /** Makes the WebView [dp] wide, as on a smaller screen, and waits for the page to follow. */
    private fun narrow(scenario: ActivityScenario<MainActivity>, dp: Int) {
        scenario.onActivity { activity ->
            val view = activity.webViewForTest!!
            view.layoutParams = view.layoutParams.apply {
                width = (dp * activity.resources.displayMetrics.density).roundToInt()
            }
        }
        Reader.waitFor(scenario, "document.documentElement.clientWidth <= $dp")
    }

    private companion object {
        /** An empty string when the first page is fully inside the document area. */
        const val PAGE_PROBLEMS = """(function () {
            var page = document.querySelector('.docx-wrapper > section.docx');
            var area = document.getElementById('docScroller');
            if (!page || !area) return 'no page';
            var box = page.getBoundingClientRect();
            var frame = area.getBoundingClientRect();
            // Chromium before 128 reports boxes inside a CSS-zoomed element in
            // its unzoomed units: the page keeps its full width at any zoom.
            // Those numbers times the zoom are where it is on screen.
            var factor = parseFloat(page.parentElement.style.zoom) || 1;
            var scale = factor !== 1 && Math.abs(box.width - page.offsetWidth) < 1 ? factor : 1;
            var left = box.left * scale;
            var right = box.right * scale;
            if (left < frame.left - 0.5 || right > frame.right + 0.5) {
              return 'the page spans ' + Math.round(left) + ' to ' + Math.round(right) +
                ' in an area from ' + Math.round(frame.left) + ' to ' + Math.round(frame.right) +
                ' at zoom ' + document.documentElement.dataset.zoom + '%';
            }
            if (area.scrollWidth > area.clientWidth + 0.5) return 'the document area scrolls sideways';
            return '';
          })()"""

        /** An empty string when every control is on screen and they take at most [maxRows] rows. */
        fun controlProblems(maxRows: Int) = """(function () {
            var width = document.documentElement.clientWidth;
            var ids = ['fileName', 'findInput', 'viewPage', 'viewReading', 'zoomOut', 'zoomValue',
              'zoomIn', 'themeButton', 'editorButton', 'openButton'];
            var problems = [];
            var rows = [];
            ids.forEach(function (id) {
              var element = document.getElementById(id);
              var box = element.getBoundingClientRect();
              if (element.hidden || getComputedStyle(element).display === 'none' || box.width < 1) {
                if (id !== 'editorButton') problems.push(id + ' is hidden');
                return;
              }
              if (box.left < -0.5 || box.right > width + 0.5) {
                problems.push(id + ' spans ' + Math.round(box.left) + ' to ' + Math.round(box.right) + ' of ' + width);
              }
              var middle = (box.top + box.bottom) / 2;
              if (!rows.some(function (row) { return Math.abs(row - middle) < 12; })) rows.push(middle);
            });
            if (rows.length > $maxRows) problems.push('the controls take ' + rows.length + ' rows');
            if (document.documentElement.scrollWidth > width + 0.5) problems.push('the page scrolls sideways');
            return problems.join('; ');
          })()"""
    }
}
