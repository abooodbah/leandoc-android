package io.github.abooodbah.leandoc

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.FileNotFoundException
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap

/**
 * Serves the desktop app's test documents through pipes, as many real
 * providers do, so the app cannot rely on seeking or on a file path. The path
 * is the fixture path, for example features/styles.docx. Names ending in
 * "-nosize" report no size; "huge.docx" streams 65 MiB of zeros, as a broken
 * download would.
 */
class FixtureProvider : ContentProvider() {

    override fun onCreate() = true

    override fun getType(uri: Uri): String = when (uri.path.orEmpty().substringAfterLast('.').substringBefore('-')) {
        "docx" -> "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        "dotx" -> "application/vnd.openxmlformats-officedocument.wordprocessingml.template"
        "docm" -> "application/vnd.ms-word.document.macroenabled.12"
        "dotm" -> "application/vnd.ms-word.template.macroenabled.12"
        "doc" -> "application/msword"
        else -> "application/octet-stream"
    }

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?,
    ): Cursor {
        val path = pathOf(uri)
        val columns = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        val cursor = MatrixCursor(columns)
        cursor.addRow(
            columns.map { column ->
                when (column) {
                    OpenableColumns.DISPLAY_NAME -> path.substringAfterLast('/').replace("-nosize", "")
                    OpenableColumns.SIZE -> if (path.contains("-nosize")) null else sizeOf(path)
                    else -> null
                }
            }.toTypedArray(),
        )
        return cursor
    }

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        if (mode != "r") throw SecurityException("read only")
        val path = pathOf(uri)
        sizeOf(path) // Throws for unknown names before a pipe is made.
        val (read, write) = ParcelFileDescriptor.createReliablePipe()
        written.remove(path)
        Thread {
            var total = 0L
            ParcelFileDescriptor.AutoCloseOutputStream(write).use { out ->
                try {
                    write(path) { chunk, length ->
                        out.write(chunk, 0, length)
                        total += length
                    }
                } catch (closed: IOException) {
                    // The reader stopped early, which is what the size limit should do.
                }
            }
            written[path] = total
        }.start()
        return read
    }

    private fun pathOf(uri: Uri): String = uri.path?.removePrefix("/") ?: throw FileNotFoundException("$uri")

    private fun sizeOf(path: String): Long {
        val clean = path.replace("-nosize", "")
        if (clean == "huge.docx") return HUGE_BYTES
        // Assets may be compressed in the APK, so count instead of asking.
        var total = 0L
        write(clean) { _, length -> total += length }
        return total
    }

    private fun write(path: String, out: (ByteArray, Int) -> Unit) {
        val clean = path.replace("-nosize", "")
        if (clean == "huge.docx") {
            val zeros = ByteArray(64 * 1024)
            var sent = 0L
            while (sent < HUGE_BYTES) {
                out(zeros, zeros.size)
                sent += zeros.size
            }
            return
        }
        requireNotNull(context).assets.open("fixtures/$clean").use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                out(buffer, read)
            }
        }
    }

    /** "written": how many bytes the last stream of [arg] delivered, or -1 while it runs. */
    override fun call(method: String, arg: String?, extras: Bundle?): Bundle? =
        if (method == "written" && arg != null) Bundle().apply { putLong("bytes", written[arg] ?: -1) } else null

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?) = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?) = 0

    companion object {
        const val AUTHORITY = "io.github.abooodbah.leandoc.fixtures"

        /** One MiB past the app's 64 MiB limit. */
        const val HUGE_BYTES = 65L * 1024 * 1024

        private val written = ConcurrentHashMap<String, Long>()

        fun uri(path: String): Uri = Uri.parse("content://$AUTHORITY/$path")
    }
}
