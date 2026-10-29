package io.github.abooodbah.leandoc

import java.io.ByteArrayOutputStream
import java.io.InputStream

/**
 * Reads a document into memory. The desktop core accepts 256 MiB; on a phone
 * the file, the core's copy and the sanitized package all live in one app
 * heap, so the limit here is 64 MiB.
 */
object DocumentReader {
    const val MAX_BYTES = 64L * 1024 * 1024

    const val TOO_LARGE = "This file is larger than LeanDoc's 64 MB limit."

    class TooLargeException : Exception(TOO_LARGE)

    /**
     * Reads all of [input]. Stops one byte past [limit] instead of reading the
     * rest, so a huge stream with no declared size costs at most the limit.
     */
    fun readLimited(input: InputStream, limit: Long = MAX_BYTES, sizeHint: Long = -1): ByteArray {
        if (sizeHint > limit) throw TooLargeException()
        val initial = if (sizeHint in 0..limit) sizeHint.toInt() else 64 * 1024
        val out = ByteArrayOutputStream(maxOf(initial, 32))
        val buffer = ByteArray(64 * 1024)
        var total = 0L
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            total += read
            if (total > limit) throw TooLargeException()
            out.write(buffer, 0, read)
        }
        return out.toByteArray()
    }
}
