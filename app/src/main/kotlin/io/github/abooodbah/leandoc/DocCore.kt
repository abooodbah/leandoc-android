package io.github.abooodbah.leandoc

/** The desktop app's C++ core: classifies, sanitizes and reports on a document. */
object DocCore {
    init {
        System.loadLibrary("leandoc")
    }

    /**
     * Opens [document] with the core's limits, except the input size, which is
     * [maximumInputBytes]. Never returns null in practice; null means the
     * result object could not be built.
     */
    @JvmStatic
    external fun open(document: ByteArray, maximumInputBytes: Long): OpenedDocument?
}

/**
 * [report] is the core's JSON report as UTF-8. [packageBytes] is the sanitized
 * package, present only for documents the reader renders.
 */
class OpenedDocument(val report: ByteArray, val packageBytes: ByteArray?)
