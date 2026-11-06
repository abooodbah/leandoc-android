package io.github.abooodbah.leandoc

import org.json.JSONException
import org.json.JSONObject

/**
 * A message the reader page posts to the app, as JSON with a "type". The
 * names and fields are the desktop reader's. Tab and recent-file messages
 * are ignored: this host shows one document and keeps no history.
 */
sealed interface ReaderCommand {
    data object Ready : ReaderCommand
    data object Open : ReaderCommand
    data object Reload : ReaderCommand
    data object OpenInEditor : ReaderCommand
    data class SetTheme(val theme: String) : ReaderCommand
    data class SetView(val view: String) : ReaderCommand

    /** The page's colour: "paper" (white, as printed) or "theme". */
    data class SetPageColor(val pageColor: String) : ReaderCommand
    data class Link(val href: String) : ReaderCommand
    data class Rendered(val generation: Long, val pages: Long, val ms: Long) : ReaderCommand
    data class RenderFailed(val generation: Long, val message: String) : ReaderCommand

    /** How long a reader action took to reach the screen, from android-host.js. */
    data class Timing(val action: String, val ms: Long) : ReaderCommand

    /** A sheet (android-host.js) opened or closed; back closes an open one. */
    data class Sheet(val open: Boolean) : ReaderCommand

    companion object {
        private const val MAX_LINK = 8192
        private val TIMED_ACTIONS = setOf("find", "zoom", "theme", "resize")

        fun parse(text: String): ReaderCommand? {
            val message = try {
                JSONObject(text)
            } catch (error: JSONException) {
                return null
            }
            return when (message.optString("type")) {
                "ready" -> Ready
                "open" -> Open
                "reload" -> Reload
                "openInEditor" -> OpenInEditor
                "setTheme" -> when (val theme = message.optString("theme")) {
                    in Themes.NAMES -> SetTheme(theme)
                    else -> null
                }
                "setView" -> when (val view = message.optString("view")) {
                    "page", "reading" -> SetView(view)
                    else -> null
                }
                "setPageColor" -> when (val color = message.optString("pageColor")) {
                    "paper", "theme" -> SetPageColor(color)
                    else -> null
                }
                "link" -> message.optString("href").takeIf { it.isNotEmpty() && it.length <= MAX_LINK }
                    ?.let { Link(it) }
                "rendered" -> generationOf(message)?.let {
                    Rendered(it, message.optLong("pages", 0), message.optLong("ms", -1))
                }
                "renderFailed" -> generationOf(message)?.let {
                    RenderFailed(it, message.optString("message").take(500))
                }
                "sheet" -> (message.opt("open") as? Boolean)?.let { Sheet(it) }
                "timing" -> {
                    val action = message.optString("action")
                    val ms = message.opt("ms")
                    if (action in TIMED_ACTIONS && ms is Number && ms.toLong() >= 0) Timing(action, ms.toLong()) else null
                }
                else -> null
            }
        }

        private fun generationOf(message: JSONObject): Long? =
            if (message.has("generation")) message.optLong("generation", -1).takeIf { it >= 0 } else null
    }
}
