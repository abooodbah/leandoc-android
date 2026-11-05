package io.github.abooodbah.leandoc

import org.junit.Assert.assertEquals
import org.junit.Test

/** The system bars take the toolbar colour (--surface) of LeanMark's themes. */
class ThemesTest {
    private fun bars(theme: String, systemIsDark: Boolean = false): Pair<String, Boolean> {
        val colors = Themes.colors(theme, systemIsDark)
        return "%06x".format(colors.surface and 0xFFFFFF) to colors.dark
    }

    @Test
    fun eachThemeColoursTheBarsWithItsToolbar() {
        assertEquals("ffffff" to false, bars("light", systemIsDark = true))
        assertEquals("1a1917" to true, bars("dark"))
        assertEquals("25282c" to true, bars("dim"))
        assertEquals("29241e" to true, bars("dusk"))
    }

    @Test
    fun systemFollowsTheSystem() {
        assertEquals("ffffff" to false, bars("system", systemIsDark = false))
        assertEquals("1a1917" to true, bars("system", systemIsDark = true))
        assertEquals("ffffff" to false, bars("sepia"))
    }

    @Test
    fun theFiveThemesInTheButtonsOrder() {
        assertEquals(listOf("system", "light", "dark", "dim", "dusk"), Themes.NAMES)
    }
}
