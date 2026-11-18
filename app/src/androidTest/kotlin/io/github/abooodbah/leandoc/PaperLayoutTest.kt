package io.github.abooodbah.leandoc

import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The page layout of LeanDoc desktop 0.3.0 and 0.3.1 on a phone, at the zoom
 * that fits a page to the screen: tables and figures that Word floats at the
 * top or foot of a page sit there across both columns with no text under
 * them, two-column papers keep their columns and their references' hanging
 * indent, a contents line keeps its dot leader to a page number at its
 * stop, wide tables stay inside the page and their text inside its cells, a line or two past a
 * page Word started continues on that page, and Strict OOXML lists keep
 * their numbers.
 *
 * The documents are the ones the desktop's reader tests build
 * (fixtures/reader/, from scripts/desktop-test-documents.mjs), each opened
 * through ACTION_VIEW with a content:// URI. Each page is brought into view
 * before it is measured.
 */
@RunWith(AndroidJUnit4::class)
class PaperLayoutTest {

    @Before
    fun needsAWebViewThatRunsThePage() {
        assumeTrue(
            "docx-preview needs WebView ${WebViewSupport.MINIMUM_MAJOR} or later",
            Reader.webViewMajor >= WebViewSupport.MINIMUM_MAJOR,
        )
    }

    @Test
    fun aFloatingTableSitsAtTheTopOfItsPageAcrossBothColumns() = open(PAPER) { scenario ->
        assertFloat(scenario, TABLE_BOX, "top")
        assertEquals(
            "the box and its anchor heading are on the same page",
            pageOfFloat(scenario, TABLE_BOX),
            probe(scenario, "L.pageOfText('II. Anchor Heading')"),
        )
    }

    @Test
    fun aFloatingFigureSitsAtTheFootOfItsPage() = open(PAPER) { scenario ->
        assertFloat(scenario, FIGURE_BOX, "bottom")
    }

    // Word gives the paper 4 pages. Android draws Times New Roman in the
    // phone's own serif font, which is wider, and the paper takes 5.
    @Test
    fun aTwoColumnPaperKeepsItsColumns() = open(PAPER) { scenario ->
        assertColumns(scenario, PAPER_TEXT, sections = 3)
    }

    @Test
    fun twoColumnSectionsKeepTheirColumns() = open(SECTIONS) { scenario ->
        assertColumns(scenario, listOf("S2 para", "S5 para"), sections = 2)
    }

    @Test
    fun referencesKeepTheirHangingIndentInTwoColumns() = open(PAPER) { scenario ->
        var references = 0
        for (index in 0 until Reader.pageCount(scenario)) {
            Reader.bringPageIntoView(scenario, index)
            assertEquals("", probe(scenario, "L.hanging($index, 20)"))
            references += (probe(scenario, "L.hanging($index, 20, true)") as Number).toInt()
        }
        assertEquals("every reference is shown once", 40, references)
    }

    // Word ends the page number at the dotted stop, 450 pt from the margin.
    // In the phone's serif font the entry's text alone nearly reaches the
    // stop; on the emulator the line wraps, and the reader, which sizes the
    // tab before the wrap, leaves the number at 383 pt, at 100% too. Only
    // the leader and the number after it are checked here.
    @Test
    fun aContentsLineKeepsItsDotLeaderToThePageNumber() = open(CONTENTS) { scenario ->
        Reader.bringPageIntoView(scenario, 0)
        assertEquals("", probe(scenario, "L.leader(0)"))
    }

    @Test
    fun wideTablesStayInsideThePageAndTheirCells() {
        for (name in listOf(TABLES, PAPER, SECTIONS)) {
            open(name) { scenario ->
                assertTrue("$name has tables", (probe(scenario, "L.tableCount()") as Number).toInt() > 0)
                assertTables(scenario)
            }
        }
    }

    @Test
    fun aLineOrTwoPastAPageWordStartedContinuesOnThatPage() {
        open(FIGURE_SECTION) { scenario ->
            assertEquals("Word gives 2 pages", "Alpha line 1.|Alpha line 73.", probe(scenario, "L.starts()"))
        }
        open(MARGIN_SECTIONS) { scenario ->
            assertEquals(
                "Word gives 3 pages",
                "Title line 1.|Alpha line 61.|Alpha line 129.",
                probe(scenario, "L.starts()"),
            )
        }
    }

    @Test
    fun aStrictOoxmlListKeepsItsNumbers() = open(STRICT_LISTS) { scenario ->
        assertEquals("", probe(scenario, "L.listItem('Listed item')"))
    }

    @Test
    fun theLayoutHoldsOnASmallPhone() = open(PAPER) { scenario ->
        Reader.narrow(scenario, 320)
        assertFloat(scenario, TABLE_BOX, "top")
        assertFloat(scenario, FIGURE_BOX, "bottom")
        assertColumns(scenario, PAPER_TEXT, sections = 3)
        assertTables(scenario)
    }

    @Test
    fun theLayoutHoldsInLandscapeAndAfterTurningBack() = open(PAPER) { scenario ->
        val portrait = Reader.zoom(scenario)
        Reader.landscape(scenario) {
            assertFloat(scenario, TABLE_BOX, "top")
            assertFloat(scenario, FIGURE_BOX, "bottom")
            assertColumns(scenario, PAPER_TEXT, sections = 3)
            assertTables(scenario)
        }
        Reader.waitFor(scenario, "document.documentElement.dataset.zoom === '$portrait' && ${Reader.ZOOM_SETTLED}")
        assertFloat(scenario, TABLE_BOX, "top")
        assertColumns(scenario, PAPER_TEXT, sections = 3)
    }

    @Test
    fun theFloatingTableStaysInPlaceAfterScrollingByTouch() = open(PAPER) { scenario ->
        val page = pageOfFloat(scenario, TABLE_BOX)
        Reader.waitFor(scenario, "${Reader.SCROLL_TOP} === 0")
        for (attempt in 1..8) {
            if (probe(scenario, "($PAGE_IN_VIEW)($page)") == true) break
            Reader.swipeUp(scenario)
            Thread.sleep(500)
        }
        assertEquals("the page with the box came into view by touch", true, probe(scenario, "($PAGE_IN_VIEW)($page)"))
        Thread.sleep(300)
        assertEquals("", probe(scenario, "L.float($page, '$TABLE_BOX', 'top')"))
    }

    @Test
    fun aPaperCutShortShowsTheCoresMessage() {
        Reader.open("$PAPER-cut").use { scenario ->
            Reader.waitForState(scenario)
            assertEquals("This document could not be opened", Reader.text(scenario, "#stateEyebrow"))
            assertEquals("two-column-floats.docx", Reader.text(scenario, "#stateTitle"))
        }
    }

    @Test
    fun theLongDocumentKeepsItsPages() {
        Reader.open("features/long-300-pages.docx").use { scenario ->
            Reader.waitForDocument(scenario, timeoutMs = 120_000)
            assertEquals(300, Reader.pageCount(scenario))
            Reader.swipeUp(scenario)
            Reader.waitFor(scenario, "${Reader.SCROLL_TOP} > 100")
        }
    }

    private fun open(name: String, block: (ActivityScenario<MainActivity>) -> Unit) =
        Reader.open(name).use { scenario ->
            Reader.waitForDocument(scenario)
            Reader.waitFor(scenario, Reader.ZOOM_SETTLED)
            assertEquals("page", Reader.js(scenario, "document.documentElement.dataset.view"))
            block(scenario)
        }

    private fun probe(scenario: ActivityScenario<MainActivity>, expression: String): Any? =
        Reader.js(scenario, "(function () { var L = $PROBES; return $expression; })()")

    private fun pageOfFloat(scenario: ActivityScenario<MainActivity>, text: String): Int {
        val page = (probe(scenario, "L.pageOfFloat('$text')") as Number).toInt()
        assertTrue("one floating box holds \"$text\" (${-1 - page} found)", page >= 0)
        return page
    }

    private fun assertFloat(scenario: ActivityScenario<MainActivity>, text: String, edge: String) {
        val page = pageOfFloat(scenario, text)
        Reader.bringPageIntoView(scenario, page)
        assertEquals("", probe(scenario, "L.float($page, '$text', '$edge')"))
    }

    /** Every line in one column, and at least [sections] column sections with text in both. */
    private fun assertColumns(scenario: ActivityScenario<MainActivity>, prefixes: List<String>, sections: Int) {
        val list = prefixes.joinToString(",", "[", "]") { "'$it'" }
        var both = 0
        for (index in 0 until Reader.pageCount(scenario)) {
            Reader.bringPageIntoView(scenario, index)
            assertEquals("", probe(scenario, "L.columns($index, $list)"))
            both += (probe(scenario, "L.columns($index, $list, true)") as Number).toInt()
        }
        assertTrue("$both column sections hold text in both columns; want $sections or more", both >= sections)
    }

    private fun assertTables(scenario: ActivityScenario<MainActivity>) {
        for (index in 0 until Reader.pageCount(scenario)) {
            Reader.bringPageIntoView(scenario, index)
            assertEquals("", probe(scenario, "L.tables($index)"))
        }
    }

    private companion object {
        const val PAPER = "reader/two-column-floats.docx"
        const val SECTIONS = "reader/two-column-sections.docx"
        const val TABLES = "reader/wide-tables.docx"
        const val CONTENTS = "reader/contents-leaders.docx"
        const val FIGURE_SECTION = "reader/figure-section.docx"
        const val MARGIN_SECTIONS = "reader/margin-sections.docx"
        const val STRICT_LISTS = "reader/strict-lists.docx"

        /** The text of the table Word floats to the top of page 2, and of the figure at the foot of page 3. */
        const val TABLE_BOX = "Floatbox heron"
        const val FIGURE_BOX = "Figurebox wren"
        val PAPER_TEXT = listOf("Lead para", "Tail para", "[")

        /** Whether page `index` is at least partly on screen. */
        const val PAGE_IN_VIEW = """function (index) {
            var page = document.querySelectorAll('#docBody .docx-wrapper > section.docx')[index];
            var area = document.getElementById('docScroller').getBoundingClientRect();
            var box = page.getBoundingClientRect();
            var factor = parseFloat(page.parentElement.style.zoom) || 1;
            var scale = factor !== 1 && Math.abs(box.width - page.offsetWidth) < 1 ? factor : 1;
            return box.top * scale < area.bottom && box.bottom * scale > area.top;
          }"""

        /** Measures of the page layout, each for one page, in that page's own units. */
        const val PROBES = """{
              pages: function () {
                return Array.prototype.slice.call(document.querySelectorAll('#docBody .docx-wrapper > section.docx'));
              },
              scale: function (page) {
                return page.getBoundingClientRect().width / page.offsetWidth;
              },
              textArea: function (page) {
                var box = page.getBoundingClientRect();
                var s = this.scale(page);
                var style = getComputedStyle(page);
                return {
                  top: box.top + parseFloat(style.paddingTop) * s,
                  bottom: box.top + (page.offsetHeight - parseFloat(style.paddingBottom)) * s,
                  left: box.left + parseFloat(style.paddingLeft) * s,
                  right: box.right - parseFloat(style.paddingRight) * s,
                  scale: s
                };
              },
              lines: function (node) {
                return Array.prototype.filter.call(node.getClientRects(), function (r) { return r.width > 0.5 && r.height > 0.5; });
              },
              quote: function (node) {
                return '"' + node.textContent.trim().slice(0, 30) + '"';
              },
              pageOfText: function (text) {
                var pages = this.pages();
                for (var i = 0; i < pages.length; i++) {
                  if (Array.prototype.some.call(pages[i].querySelectorAll('article p'), function (p) { return p.textContent.trim().indexOf(text) === 0; })) return i;
                }
                return -1;
              },
              pageOfFloat: function (text) {
                var found = [];
                this.pages().forEach(function (page, index) {
                  Array.prototype.forEach.call(page.querySelectorAll('.ld-float'), function (box) {
                    if (box.textContent.indexOf(text) >= 0) found.push(index);
                  });
                });
                return found.length === 1 ? found[0] : -1 - found.length;
              },
              // A box floated to the top or foot of page `index`, across both columns,
              // with no text under it.
              float: function (index, text, edge) {
                var self = this;
                var page = this.pages()[index];
                var box = Array.prototype.filter.call(page.querySelectorAll('.ld-float'), function (b) { return b.textContent.indexOf(text) >= 0; })[0];
                if (!box) return 'no floating box holds "' + text + '" on page ' + (index + 1);
                if (box.parentElement !== page) return 'the box with "' + text + '" is inside a column, not on the page';
                var area = this.textArea(page);
                var s = area.scale;
                var r = box.getBoundingClientRect();
                if (edge === 'top' && Math.abs(r.top - area.top) / s > 1) return 'the box starts ' + Math.round((r.top - area.top) / s) + ' px below the top margin of page ' + (index + 1);
                if (edge === 'bottom' && Math.abs(r.bottom - area.bottom) / s > 1) return 'the box ends ' + Math.round((area.bottom - r.bottom) / s) + ' px above the bottom margin of page ' + (index + 1);
                var width = (area.right - area.left) / s;
                if (Math.abs(r.width / s - width) > 2) return 'the box is ' + Math.round(r.width / s) + ' px wide; both columns are ' + Math.round(width);
                if (r.left < area.left - 1 || r.right > area.right + 1) return 'the box spans x ' + Math.round(r.left) + ' to ' + Math.round(r.right) + ', past the text area ' + Math.round(area.left) + ' to ' + Math.round(area.right);
                var problem = '';
                Array.prototype.some.call(page.querySelectorAll('article p'), function (p) {
                  if (p.closest('.ld-float') || !/\S/.test(p.textContent)) return false;
                  return self.lines(p).some(function (line) {
                    var under = line.bottom > r.top + 0.5 && line.top < r.bottom - 0.5;
                    if (under) problem = self.quote(p) + ' runs under the box with "' + text + '" on page ' + (index + 1);
                    return under;
                  });
                });
                return problem;
              },
              // On page `index`: every line of the matching paragraphs in two-column
              // sections sits in one column. Returns "" or a problem, and with
              // `count` the number of sections that hold text in both columns.
              columns: function (index, prefixes, count) {
                var self = this;
                var problems = [];
                var both = 0;
                var page = this.pages()[index];
                Array.prototype.forEach.call(page.querySelectorAll('article'), function (article) {
                  if (parseInt(getComputedStyle(article).columnCount, 10) !== 2) return;
                  var box = article.getBoundingClientRect();
                  var middle = (box.left + box.right) / 2;
                  var left = 0, right = 0;
                  Array.prototype.forEach.call(article.querySelectorAll('p'), function (p) {
                    if (p.closest('.ld-float')) return;
                    var text = p.textContent.trim();
                    if (!prefixes.some(function (prefix) { return text.indexOf(prefix) === 0; })) return;
                    self.lines(p).forEach(function (line) {
                      if (line.right <= middle + 1 && line.left >= box.left - 1) left++;
                      else if (line.left >= middle - 1 && line.right <= box.right + 1) right++;
                      else problems.push('page ' + (index + 1) + ': a line of ' + self.quote(p) + ' spans x ' + Math.round(line.left) + ' to ' + Math.round(line.right) + ' across the columns ' + Math.round(box.left) + ' | ' + Math.round(middle) + ' | ' + Math.round(box.right));
                    });
                  });
                  if (left && right) both++;
                });
                return count ? both : problems.slice(0, 2).join('; ');
              },
              // On page `index`: each reference's text starts at its hanging indent
              // after "[n]" and a tab, on its first line and on the lines after it.
              // Returns "" or a problem, and with `count` the number of references.
              hanging: function (index, indentPt, count) {
                var self = this;
                var page = this.pages()[index];
                var s = this.scale(page);
                var seen = 0;
                var problem = '';
                Array.prototype.some.call(page.querySelectorAll('article p'), function (p) {
                  var match = /^\[(\d+)\]/.exec(p.textContent);
                  if (!match) return false;
                  seen++;
                  var tab = p.querySelector('span.ld-tab');
                  if (!tab) { problem = '[' + match[1] + '] has no sized tab'; return true; }
                  var first = p.querySelector('span').firstChild;
                  var number = document.createRange();
                  number.setStart(first, 0);
                  number.setEnd(first, first.textContent.length);
                  var after = document.createRange();
                  after.setStartAfter(tab);
                  after.setEnd(p, p.childNodes.length);
                  var starts = [];
                  Array.prototype.forEach.call(after.getClientRects(), function (r) {
                    if (r.width > 0 && !starts.some(function (l) { return Math.abs(l.top - r.top) < 2 * s; })) starts.push({ top: r.top, left: r.left });
                  });
                  var numberBox = number.getBoundingClientRect();
                  starts.sort(function (a, b) { return a.top - b.top; });
                  return starts.some(function (line, at) {
                    // The first line's tab is sized from where the number ends, which
                    // depends on the font: it must only not run into the number.
                    if (at === 0) {
                      if (line.left < numberBox.right + 0.5 * s) { problem = '[' + match[1] + '] text starts inside its number'; return true; }
                      return false;
                    }
                    var indent = (line.left - numberBox.left) / s * 0.75;
                    if (Math.abs(indent - indentPt) > 1.5) { problem = '[' + match[1] + '] line ' + (at + 1) + ' starts ' + indent.toFixed(1) + ' pt after its number; the hanging indent is ' + indentPt + ' pt'; return true; }
                    return false;
                  });
                });
                return count ? seen : problem;
              },
              // The contents line: a dot leader, then the page number on the
            // line where the leader ends, inside the text area. With `stopPt`,
            // the number also ends at that right tab stop, in points from the
            // left margin. How long the leader is depends on the font:
            // Android draws Times New Roman in the phone's own serif font,
            // which is wider.
            leader: function (stopPt) {
              var leader = document.querySelector('#docBody span.ld-tab-leader');
              if (!leader) return 'no tab leader';
              if (leader.textContent.indexOf('.') < 0) return 'the leader is "' + leader.textContent.slice(0, 10) + '"';
              var page = leader.closest('section.docx');
              var s = this.scale(page);
              var area = this.textArea(page);
              var number = leader.nextSibling;
              if (!number || !/[0-9]/.test(number.textContent)) return 'no page number after the leader';
              var range = document.createRange();
              range.selectNodeContents(number);
              var box = range.getBoundingClientRect();
              var dots = this.lines(leader);
              if (!dots.length) return 'the leader is not drawn';
              if (Math.abs(dots[dots.length - 1].bottom - box.bottom) > 3 * s) return 'the page number is not on the line where the leader ends';
              if (box.right > area.right + 1) return 'the page number ends past the text area';
              var end = (box.right - area.left) / s * 0.75;
              if (stopPt && Math.abs(end - stopPt) > 1.5) return 'the page number ends ' + end.toFixed(1) + ' pt from the margin; the stop is at ' + stopPt + ' pt';
              return '';
            },
            // On page `index`: every table inside the text area, every cell's text
              // inside its cell.
              tables: function (index) {
                var self = this;
                var page = this.pages()[index];
                var area = this.textArea(page);
                var problem = '';
                Array.prototype.some.call(page.querySelectorAll('table'), function (table) {
                  var t = table.getBoundingClientRect();
                  if (t.left < area.left - 1 || t.right > area.right + 1) { problem = 'a table on page ' + (index + 1) + ' spans x ' + Math.round(t.left) + ' to ' + Math.round(t.right) + ', past the text area ' + Math.round(area.left) + ' to ' + Math.round(area.right); return true; }
                  return Array.prototype.some.call(table.querySelectorAll('td, th'), function (cell) {
                    var c = cell.getBoundingClientRect();
                    var walker = document.createTreeWalker(cell, NodeFilter.SHOW_TEXT);
                    var range = document.createRange();
                    for (var node = walker.nextNode(); node; node = walker.nextNode()) {
                      if (!/\S/.test(node.textContent)) continue;
                      range.selectNodeContents(node);
                      var bad = Array.prototype.some.call(range.getClientRects(), function (r) {
                        return r.width > 0.5 && (r.left < c.left - 1 || r.right > c.right + 1 || r.top < c.top - 1 || r.bottom > c.bottom + 1);
                      });
                      if (bad) { problem = '"' + node.textContent.trim().slice(0, 30) + '" runs past its cell on page ' + (index + 1); return true; }
                    }
                    return false;
                  });
                });
                return problem;
              },
              tableCount: function () {
                return document.querySelectorAll('#docBody section.docx table').length;
              },
              // The first paragraph of each page, as "Title line 1.|Alpha line 61.".
              starts: function () {
                return this.pages().map(function (page) {
                  var p = page.querySelector('article p');
                  return p ? p.textContent.trim() : '';
                }).join('|');
              },
              listItem: function (text) {
                var p = Array.prototype.filter.call(document.querySelectorAll('#docBody article p'), function (q) { return q.textContent.trim().indexOf(text) === 0; })[0];
                if (!p) return 'no paragraph "' + text + '"';
                var display = getComputedStyle(p).display;
                return display === 'list-item' ? '' : '"' + text + '" shows no number: display ' + display;
              }
            }"""
    }
}
