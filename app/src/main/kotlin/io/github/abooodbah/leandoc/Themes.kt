package io.github.abooodbah.leandoc

/**
 * LeanMark's five themes. [Colors.surface] is the reader's --surface, the
 * toolbar's colour, which the window and the system bars take so they meet
 * the toolbar without a seam.
 */
object Themes {
    val NAMES = listOf("system", "light", "dark", "dim", "dusk")

    class Colors(val surface: Int, val dark: Boolean)

    private val light = Colors(0xFFFFFFFF.toInt(), dark = false)
    private val dark = Colors(0xFF1A1917.toInt(), dark = true)
    private val dim = Colors(0xFF25282C.toInt(), dark = true)
    private val dusk = Colors(0xFF29241E.toInt(), dark = true)

    /** The colours [theme] shows; System follows the system's dark setting. */
    fun colors(theme: String, systemIsDark: Boolean): Colors = when (theme) {
        "light" -> light
        "dark" -> dark
        "dim" -> dim
        "dusk" -> dusk
        else -> if (systemIsDark) dark else light
    }
}
