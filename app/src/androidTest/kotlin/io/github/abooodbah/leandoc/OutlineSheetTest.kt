package io.github.abooodbah.leandoc

import android.content.pm.ActivityInfo
import android.view.KeyEvent
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The section highlight on a phone. The desktop page hides its outline pane
 * below 760 px, so on a phone in portrait there was no outline and no
 * highlight at all. The outline now opens as a sheet; the reader marks the
 * current section there (aria-current="location") and keeps it in view.
 */
@RunWith(AndroidJUnit4::class)
class OutlineSheetTest {

    @Before
    fun needsAWebViewThatRunsThePage() {
        assumeTrue(
            "docx-preview needs WebView ${WebViewSupport.MINIMUM_MAJOR} or later",
            Reader.webViewMajor >= WebViewSupport.MINIMUM_MAJOR,
        )
    }

    @Test
    fun onAPhoneTheOutlineOpensAsASheet() = openLong { scenario ->
        assertEquals(true, Reader.js(scenario, "$PHONE && $SHOWN('outlineButton')"))
        assertEquals("the pane is hidden on a phone", false, Reader.js(scenario, "$SHOWN('outline')"))
        Reader.js(scenario, "document.getElementById('outlineButton').click()")
        Reader.waitFor(scenario, "$SHOWN('outline')")
        assertEquals(true, Reader.js(scenario, "!document.getElementById('outlineSheet').hidden"))
        assertTrue(
            (Reader.js(scenario, "document.querySelectorAll('#outlineSheet #outlineList a').length") as Number).toInt() > 300,
        )
        assertEquals("Part 1", Reader.js(scenario, CURRENT_TEXT))
    }

    @Test
    fun theCurrentSectionIsHighlightedAndInViewInTheSheet() = openLong { scenario ->
        // Scrolled with the sheet closed, then opened: the entry is in view.
        jumpTo(scenario, 0.4)
        Reader.js(scenario, "document.getElementById('outlineButton').click()")
        Reader.waitFor(scenario, "$SHOWN('outline')")
        Reader.waitFor(scenario, "($CURRENT_PROBLEMS) === ''", timeoutMs = 10_000)
        // Scrolled with the sheet open: the highlight follows and stays in view.
        val before = Reader.js(scenario, CURRENT_TEXT)
        jumpTo(scenario, 0.75)
        Reader.waitFor(scenario, "($CURRENT_TEXT) !== ${org.json.JSONObject.quote(before as String)}")
        Reader.waitFor(scenario, "($CURRENT_PROBLEMS) === ''", timeoutMs = 10_000)
    }

    @Test
    fun aFlickByTouchMovesTheHighlight() = openLong { scenario ->
        repeat(3) { Reader.swipeUp(scenario) }
        Reader.waitFor(scenario, "${Reader.SCROLL_TOP} > 400")
        Reader.waitFor(scenario, "($CURRENT_PROBLEMS) === ''", timeoutMs = 10_000)
        assertFalse("the highlight left the first entry", Reader.js(scenario, CURRENT_TEXT) == "Part 1")
    }

    @Test
    fun tappingAnEntryJumpsThereAndClosesTheSheet() = openLong { scenario ->
        Reader.js(scenario, "document.getElementById('outlineButton').click()")
        Reader.waitFor(scenario, "$SHOWN('outline')")
        Reader.js(
            scenario,
            "Array.prototype.find.call(document.querySelectorAll('#outlineList a'), function (a) { return a.textContent === 'Section 150'; }).click()",
        )
        Reader.waitFor(scenario, "document.getElementById('outlineSheet').hidden")
        assertEquals("Section 150", Reader.js(scenario, CURRENT_TEXT))
        val top = Reader.js(scenario, TARGET_TOP) as Number
        assertTrue("Section 150 is at the top of the document area: $top", top.toInt() in -40..60)
    }

    @Test
    fun backClosesTheSheetAndKeepsTheDocument() = openLong { scenario ->
        Reader.js(scenario, "document.getElementById('outlineButton').click()")
        Reader.waitFor(scenario, "$SHOWN('outline')")
        Focus.ensure()
        Reader.instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        Reader.waitFor(scenario, "document.getElementById('outlineSheet').hidden")
        scenario.onActivity { assertFalse("back closed the sheet, not the app", it.isFinishing) }
    }

    @Test
    fun inLandscapeTheOutlineIsAPaneWithTheCurrentEntryInView() = openLong { scenario ->
        jumpTo(scenario, 0.5)
        Reader.waitFor(scenario, "($CURRENT_TEXT) !== 'Part 1'")
        scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
        try {
            Reader.waitFor(scenario, "window.innerWidth > 760")
            Reader.waitFor(scenario, "$SHOWN('outline') && !document.getElementById('outline').closest('#outlineSheet')")
            Reader.waitFor(scenario, "($CURRENT_PROBLEMS) === ''", timeoutMs = 10_000)
            assertEquals(false, Reader.js(scenario, "$SHOWN('outlineButton')"))
        } finally {
            scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
        }
        Reader.waitFor(scenario, "$PHONE && $SHOWN('outlineButton') && !$SHOWN('outline')")
    }

    @Test
    fun aDocumentWithoutHeadingsHasNoOutlineButton() {
        Reader.open("kinds/template.dotm", "application/vnd.ms-word.template.macroenabled.12").use { scenario ->
            Reader.waitForDocument(scenario)
            assertEquals(false, Reader.js(scenario, "$SHOWN('outlineButton')"))
        }
    }

    private fun openLong(block: (ActivityScenario<MainActivity>) -> Unit) =
        Reader.open("features/long-300-pages.docx").use { scenario ->
            Reader.waitForDocument(scenario, timeoutMs = 120_000)
            block(scenario)
        }

    /** Scrolls the document to [ratio] of its height in one jump, as a scrollbar drag or Find does. */
    private fun jumpTo(scenario: ActivityScenario<MainActivity>, ratio: Double) {
        Reader.js(scenario, "(function () { var s = document.getElementById('docScroller'); s.scrollTop = (s.scrollHeight - s.clientHeight) * $ratio; })()")
        Reader.waitFor(scenario, "${Reader.SCROLL_TOP} > 0")
    }

    private companion object {
        const val PHONE = "(document.documentElement.clientWidth <= 760)"

        /** True when the element with the id takes space on screen. */
        const val SHOWN = "(function (id) { var e = document.getElementById(id); " +
            "return !!e && e.getClientRects().length > 0 && e.getBoundingClientRect().width > 0; })"

        /**
         * Where the current entry's heading is on screen, from the top of the
         * document area. Chromium before 128 reports boxes inside a
         * CSS-zoomed element in its unzoomed units; times the zoom they are
         * where the heading is.
         */
        const val TARGET_TOP = """(function () {
            var link = document.querySelector('#outlineList a[aria-current="location"]');
            if (!link) return null;
            var target = document.querySelector('[data-ld-target="' + link.dataset.index + '"]');
            var wrapper = document.querySelector('#docBody .docx-wrapper');
            var factor = parseFloat(wrapper.style.zoom) || 1;
            var box = target.getBoundingClientRect();
            var scale = factor !== 1 && Math.abs(box.width - target.offsetWidth) < 1 ? factor : 1;
            return Math.round(box.top * scale - document.getElementById('docScroller').getBoundingClientRect().top);
          })()"""

        const val CURRENT_TEXT =
            "(function () { var a = document.querySelector('#outlineList a[aria-current=\"location\"]'); return a ? a.textContent : null; })()"

        /**
         * An empty string when exactly one entry is current, its heading is the
         * last one at or above a quarter of the document area (the reader's
         * rule), and the entry is inside the visible part of the list.
         */
        const val CURRENT_PROBLEMS = """(function () {
            var links = document.querySelectorAll('#outlineList a[aria-current="location"]');
            if (links.length !== 1) return links.length + ' current entries';
            var link = links[0];
            var scroller = document.getElementById('docScroller');
            var frame = scroller.getBoundingClientRect();
            var line = frame.top + Math.max(48, scroller.clientHeight * 0.25);
            var all = document.querySelectorAll('#outlineList a');
            var expected = null;
            for (var i = 0; i < all.length; i++) {
              var target = document.querySelector('[data-ld-target="' + all[i].dataset.index + '"]');
              if (!target) continue;
              // The reader's own rule, with the same boxes it reads.
              if (target.getBoundingClientRect().top <= line) expected = all[i]; else break;
            }
            if (scroller.scrollTop <= 1) expected = all[0];
            if (expected && expected !== link) return 'current is ' + link.textContent + ', the window shows ' + expected.textContent;
            var list = document.getElementById('outlineList');
            var box = list.getBoundingClientRect();
            var entry = link.getBoundingClientRect();
            if (box.height > 0 && (entry.top < box.top - 1 || entry.bottom > box.bottom + 1)) {
              return link.textContent + ' spans ' + Math.round(entry.top) + ' to ' + Math.round(entry.bottom) +
                ' outside the list from ' + Math.round(box.top) + ' to ' + Math.round(box.bottom);
            }
            return '';
          })()"""
    }
}
