package io.github.abooodbah.leandoc

/**
 * What a tapped external link in a document does. The page follows links
 * inside the document itself and only posts links marked external.
 */
sealed interface LinkAction {
    /** Handed to another app, such as the browser or mail app. */
    data class External(val uri: String) : LinkAction
    data class Refuse(val message: String) : LinkAction
    data object Ignore : LinkAction

    companion object {
        private val scheme = Regex("^([A-Za-z][A-Za-z0-9+.-]*):")

        fun forHref(href: String): LinkAction {
            val trimmed = href.trim()
            if (trimmed.isEmpty() || trimmed.startsWith("#")) return Ignore
            val match = scheme.find(trimmed)
            if (match != null) {
                return when (match.groupValues[1].lowercase()) {
                    "http", "https", "mailto" -> External(trimmed)
                    else -> Refuse("That link scheme is blocked for safety.")
                }
            }
            if (trimmed.startsWith("//")) {
                return Refuse("That link scheme is blocked for safety.")
            }
            // A shared file comes without access to the folder around it, so a
            // relative link has nothing to resolve against.
            return Refuse(
                "LeanDoc can't follow links to other files on Android. " +
                    "Open that file from your file manager.",
            )
        }
    }
}
