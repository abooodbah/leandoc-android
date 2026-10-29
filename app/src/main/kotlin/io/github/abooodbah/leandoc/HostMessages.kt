package io.github.abooodbah.leandoc

import org.json.JSONObject

/**
 * Messages from the app to the desktop reader page, in the shape reader.js
 * handles in handleHostMessage. They match what the Linux host sends, with one
 * tab (id 1) and no tab strip.
 */
object HostMessages {
    const val TAB = 1
    const val EDITOR_LABEL = "Open in another app"

    fun host(version: String): String = JSONObject()
        .put("type", "host")
        .put("platform", "android")
        .put("version", version)
        .put("editor", JSONObject().put("kind", "app").put("label", EDITOR_LABEL))
        .put("tabs", false)
        .toString()

    fun theme(theme: String, view: String): String =
        JSONObject().put("type", "theme").put("theme", theme).put("view", view).toString()

    fun empty(): String = """{"type":"empty","recent":[]}"""

    /** [report] is the core's JSON report, inserted as it is. */
    fun document(generation: Long, fileName: String, packageUrl: String, report: String): String =
        "{\"type\":\"document\",\"tab\":$TAB,\"generation\":$generation," +
            "\"fileName\":${JSONObject.quote(fileName)},\"displayPath\":${JSONObject.quote(fileName)}," +
            "\"packageUrl\":${JSONObject.quote(packageUrl)},\"report\":$report}"

    fun fallback(generation: Long, fileName: String, report: String): String =
        "{\"type\":\"fallback\",\"tab\":$TAB,\"generation\":$generation," +
            "\"fileName\":${JSONObject.quote(fileName)},\"displayPath\":${JSONObject.quote(fileName)}," +
            "\"report\":$report}"

    fun status(message: String, tone: String = "info"): String =
        JSONObject().put("type", "status").put("message", message).put("tone", tone).toString()

    /**
     * A report in the core's shape for a failure the app finds before the core
     * runs, such as a file that cannot be read or is over the size limit.
     */
    fun failureReport(code: String, message: String, sourceBytes: Long): String = JSONObject()
        .put("ok", false)
        .put("errorCode", code)
        .put("errorMessage", message)
        .put("kind", "not-a-document")
        .put("sourceBytes", sourceBytes)
        .put("features", org.json.JSONArray())
        .put("outline", org.json.JSONArray())
        .put(
            "properties",
            JSONObject().put("title", "").put("creator", "").put("rightToLeft", false),
        )
        .toString()
}
