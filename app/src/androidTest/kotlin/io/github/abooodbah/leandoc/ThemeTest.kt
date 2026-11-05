package io.github.abooodbah.leandoc

import android.content.res.Configuration
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * LeanMark's five themes with LeanMark 0.6.0's colours, the theme button's
 * dot and name, the settings sheet, and the current section's look in each
 * theme. LeanMark's values are from its assets/reader.css.
 */
@RunWith(AndroidJUnit4::class)
class ThemeTest {

    @Before
    fun needsAWebViewThatRunsThePage() {
        assumeTrue(
            "docx-preview needs WebView ${WebViewSupport.MINIMUM_MAJOR} or later",
            Reader.webViewMajor >= WebViewSupport.MINIMUM_MAJOR,
        )
        clearTheme()
    }

    @After
    fun backToTheSystemTheme() = clearTheme()

    private fun clearTheme() {
        Reader.context.getSharedPreferences("reader", 0).edit().clear().commit()
    }

    @Test
    fun theThemeButtonStepsThroughTheFiveThemes() = open { scenario ->
        val system = if (systemIsDark()) "Dark" else "Light"
        assertEquals(listOf("system", "Theme: System ($system). Activate to change theme."), button(scenario))
        val seen = mutableListOf<List<String>>()
        repeat(5) {
            Reader.js(scenario, "document.getElementById('themeButton').click()")
            seen += button(scenario)
        }
        assertEquals(
            listOf(
                listOf("light", "Theme: Light. Activate to change theme."),
                listOf("dark", "Theme: Dark. Activate to change theme."),
                listOf("dim", "Theme: Dim. Activate to change theme."),
                listOf("dusk", "Theme: Dusk. Activate to change theme."),
                listOf("system", "Theme: System ($system). Activate to change theme."),
            ),
            seen,
        )
    }

    @Test
    fun eachThemeHasLeanMarksColoursAndItsDot() = open { scenario ->
        for ((theme, expected) in TOKENS) {
            choose(scenario, theme)
            val actual = Reader.js(scenario, "JSON.stringify($READ_TOKENS)") as String
            val values = JSONObject(actual)
            for ((token, value) in expected) {
                assertEquals("$theme $token in $actual", value, values.getString(token))
            }
            assertEquals("$theme dot", DOTS[theme], Reader.js(scenario, DOT_COLOR))
        }
    }

    @Test
    fun theCurrentSectionLooksLikeLeanMarksInEveryTheme() = open("features/long-300-pages.docx") { scenario ->
        // A section (level 2) rather than the first part (level 1, bold in
        // the list whether current or not).
        Reader.js(scenario, "(function () { var s = document.getElementById('docScroller'); s.scrollTop = (s.scrollHeight - s.clientHeight) * 0.4; })()")
        Reader.waitFor(scenario, "(document.querySelector('#outlineList a[aria-current=\"location\"]') || {dataset: {}}).dataset.level === '2'")
        for ((theme, expected) in TOKENS) {
            choose(scenario, theme)
            Reader.js(scenario, "document.getElementById('outlineButton').click()")
            Reader.waitFor(scenario, "!document.getElementById('outlineSheet').hidden")
            val look = JSONObject(Reader.js(scenario, CURRENT_LOOK) as String)
            Reader.js(scenario, "document.querySelector('#outlineSheet .android-sheet-close').click()")
            assertEquals("$theme background", rgb(expected.getValue("--surface-muted")), look.getString("background"))
            assertEquals("$theme text", rgb(expected.getValue("--text-primary")), look.getString("color"))
            assertTrue("$theme bar: ${look.getString("bar")}", look.getString("bar").startsWith(rgb(expected.getValue("--link")) + " 2px 0px 0px 0px inset"))
            assertEquals("$theme weight: the current entry keeps its own", look.getString("ownWeight"), look.getString("weight"))
        }
    }

    @Test
    fun aThemeIsRememberedAndColoursTheSystemBars() {
        open { scenario ->
            choose(scenario, "dusk")
            waitForBars(scenario, 0xFF29241E.toInt())
        }
        open { scenario ->
            assertEquals("dusk", Reader.js(scenario, "document.documentElement.getAttribute('data-theme')"))
            waitForBars(scenario, 0xFF29241E.toInt())
        }
    }

    @Test
    fun theSettingsSheetChangesTheViewAndTheZoom() = open { scenario ->
        Reader.js(scenario, "document.getElementById('settingsButton').click()")
        Reader.waitFor(scenario, "!document.getElementById('settingsSheet').hidden")
        assertEquals(true, Reader.js(scenario, "document.querySelector('input[name=\"settings-view\"][value=\"page\"]').checked"))
        Reader.js(scenario, "document.querySelector('input[name=\"settings-view\"][value=\"reading\"]').click()")
        Reader.waitFor(scenario, "document.documentElement.dataset.view === 'reading'")
        Reader.waitForDocument(scenario)
        Reader.js(scenario, "document.querySelector('input[name=\"settings-view\"][value=\"page\"]').click()")
        Reader.waitFor(scenario, "document.documentElement.dataset.view === 'page'")
        Reader.waitForDocument(scenario)
        val before = (Reader.js(scenario, "Number(document.documentElement.dataset.zoom)") as Number).toInt()
        Reader.js(scenario, "document.getElementById('settingsZoomIn').click()")
        Reader.waitFor(scenario, "Number(document.documentElement.dataset.zoom) > $before")
        Reader.waitFor(scenario, "document.getElementById('settingsZoomValue').textContent === document.documentElement.dataset.zoom + '%'")
        Reader.js(scenario, "document.getElementById('settingsZoomFit').click()")
        Reader.waitFor(scenario, "document.documentElement.dataset.zoomMode === 'fit'")
        Reader.waitFor(scenario, "document.getElementById('settingsZoomFit').textContent === '100%'")
    }

    @Test
    fun tabStaysInsideAnOpenSheet() = open { scenario ->
        Reader.js(scenario, "document.getElementById('settingsButton').click()")
        Reader.waitFor(scenario, "!document.getElementById('settingsSheet').hidden")
        Focus.ensure()
        // The sheet was opened from a script, so the WebView itself does not
        // have Android's focus yet; the first Tab would only give it that.
        // Focus it, then put the page's focus back in the sheet, as a tap
        // on the settings button leaves it.
        scenario.onActivity { it.webViewForTest!!.requestFocus() }
        Reader.js(scenario, "document.querySelector('#settingsSheet input[name=\"settings-theme\"]:checked').focus()")
        Reader.waitFor(scenario, "document.activeElement && document.activeElement.name === 'settings-theme'")
        val outside = mutableListOf<String>()
        repeat(12) { step ->
            Reader.instrumentation.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_TAB)
            val where = Reader.js(
                scenario,
                "(function () { var a = document.activeElement; return a && a.closest('#settingsSheet') ? '' : (a ? (a.id || a.tagName) : 'none'); })()",
            ) as String
            if (where.isNotEmpty()) outside += "Tab ${step + 1}: $where"
        }
        assertEquals("focus left the sheet", emptyList<String>(), outside)
    }

    @Test
    fun withoutADocumentTheSheetOffersTheThemeOnly() {
        val launch = Reader.context.packageManager.getLaunchIntentForPackage(Reader.context.packageName)!!
        ActivityScenario.launch<MainActivity>(launch).use { scenario ->
            Reader.waitForState(scenario)
            Reader.js(scenario, "document.getElementById('settingsButton').click()")
            Reader.waitFor(scenario, "!document.getElementById('settingsSheet').hidden")
            assertEquals(false, Reader.js(scenario, "document.querySelector('input[name=\"settings-theme\"][value=\"dim\"]').disabled"))
            assertEquals(true, Reader.js(scenario, "document.querySelector('input[name=\"settings-view\"][value=\"reading\"]').disabled"))
            assertEquals(true, Reader.js(scenario, "document.getElementById('settingsZoomIn').disabled"))
            Reader.js(scenario, "document.querySelector('input[name=\"settings-theme\"][value=\"dim\"]').click()")
            Reader.waitFor(scenario, "document.documentElement.getAttribute('data-theme') === 'dim'")
        }
    }

    private fun open(name: String = "features/styles.docx", block: (ActivityScenario<MainActivity>) -> Unit) =
        Reader.open(name).use { scenario ->
            Reader.waitForDocument(scenario, timeoutMs = 120_000)
            block(scenario)
        }

    /** Picks [theme] in the settings sheet, as a reader taps it, and closes the sheet. */
    private fun choose(scenario: ActivityScenario<MainActivity>, theme: String) {
        Reader.js(scenario, "document.getElementById('settingsButton').click()")
        Reader.waitFor(scenario, "!document.getElementById('settingsSheet').hidden")
        Reader.js(scenario, "document.querySelector('input[name=\"settings-theme\"][value=\"$theme\"]').click()")
        val attribute = if (theme == "system") "null" else "'$theme'"
        Reader.waitFor(scenario, "document.documentElement.getAttribute('data-theme') === $attribute")
        Reader.js(scenario, "document.querySelector('#settingsSheet .android-sheet-close').click()")
        Reader.waitFor(scenario, "document.getElementById('settingsSheet').hidden")
    }

    /** The status and navigation bars take the theme's toolbar colour. */
    private fun waitForBars(scenario: ActivityScenario<MainActivity>, color: Int) {
        val deadline = android.os.SystemClock.uptimeMillis() + 10_000
        var bars = listOf(0, 0)
        while (android.os.SystemClock.uptimeMillis() < deadline) {
            scenario.onActivity { bars = listOf(it.window.statusBarColor, it.window.navigationBarColor) }
            if (bars == listOf(color, color)) return
            android.os.SystemClock.sleep(100)
        }
        assertEquals(listOf(color, color), bars)
    }

    private fun button(scenario: ActivityScenario<MainActivity>): List<String> = listOf(
        Reader.js(scenario, "document.getElementById('themeSwatch').dataset.swatch") as String,
        Reader.js(scenario, "document.getElementById('themeButton').getAttribute('aria-label')") as String,
    )

    private fun systemIsDark(): Boolean =
        Reader.context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES

    private fun rgb(hex: String): String {
        val value = hex.removePrefix("#").toInt(16)
        return "rgb(${value shr 16 and 255}, ${value shr 8 and 255}, ${value and 255})"
    }

    private companion object {
        /** LeanMark 0.6.0's tokens, assets/reader.css, for the tokens LeanDoc shares. */
        val TOKENS = linkedMapOf(
            "light" to mapOf(
                "--canvas" to "#faf9f7", "--surface" to "#ffffff", "--surface-muted" to "#f4f2ee",
                "--border" to "#e8e4dc", "--text-primary" to "#1a1917", "--text-secondary" to "#5a554c",
                "--text-disabled" to "#a7a195", "--ring" to "#3b82f6", "--link" to "#1d4ed8",
                "--link-hover" to "#1e40af", "--selection" to "#cbdcff",
            ),
            "dark" to mapOf(
                "--canvas" to "#0e0d0b", "--surface" to "#1a1917", "--surface-muted" to "#2b2924",
                "--border" to "#2b2924", "--text-primary" to "#faf9f7", "--text-secondary" to "#d5cfc3",
                "--text-disabled" to "#716b5f", "--ring" to "#3b82f6", "--link" to "#60a5fa",
                "--link-hover" to "#93c5fd", "--selection" to "#23468c",
            ),
            "dim" to mapOf(
                "--canvas" to "#1e2023", "--surface" to "#25282c", "--surface-muted" to "#2d3035",
                "--border" to "#393d43", "--text-primary" to "#c8ccd2", "--text-secondary" to "#9ea4ac",
                "--text-disabled" to "#6c727a", "--ring" to "#6c9be0", "--link" to "#86aee6",
                "--link-hover" to "#a6c4ee", "--selection" to "#34486a",
            ),
            "dusk" to mapOf(
                "--canvas" to "#221e19", "--surface" to "#29241e", "--surface-muted" to "#322c25",
                "--border" to "#3f3830", "--text-primary" to "#d4c8b4", "--text-secondary" to "#a99c87",
                "--text-disabled" to "#776c5c", "--ring" to "#c99a62", "--link" to "#e0a86e",
                "--link-hover" to "#ebc08f", "--selection" to "#4d3f2c",
            ),
        )

        /** LeanMark's dot colours, as computed backgrounds. */
        val DOTS = mapOf(
            "light" to "rgb(255, 255, 255)",
            "dark" to "rgb(0, 0, 0)",
            "dim" to "rgb(127, 138, 153)",
            "dusk" to "rgb(201, 143, 79)",
        )

        /**
         * Each token as a lowercase hex colour. The tokens chain through
         * var(), so each is resolved by painting a probe with it.
         */
        const val READ_TOKENS = """(function () {
            var names = ['--canvas', '--surface', '--surface-muted', '--border', '--text-primary',
              '--text-secondary', '--text-disabled', '--ring', '--link', '--link-hover', '--selection'];
            // A custom property's computed value has its var() references
            // already replaced, so each is the theme's own colour.
            var style = getComputedStyle(document.documentElement);
            var out = { theme: document.documentElement.getAttribute('data-theme') };
            names.forEach(function (name) {
              out[name] = style.getPropertyValue(name).trim().toLowerCase();
            });
            return out;
          })()"""

        const val DOT_COLOR = "getComputedStyle(document.getElementById('themeSwatch')).backgroundColor"

        const val CURRENT_LOOK = """JSON.stringify((function () {
            var link = document.querySelector('#outlineList a[aria-current="location"]');
            var style = getComputedStyle(link);
            var same = document.querySelector('#outlineList a[data-level="' + link.dataset.level + '"]:not([aria-current])');
            return { background: style.backgroundColor, color: style.color, bar: style.boxShadow,
              weight: style.fontWeight, ownWeight: getComputedStyle(same).fontWeight };
          })())"""
    }
}
