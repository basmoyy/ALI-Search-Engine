package com.ali.search

/**
 * Kotlin wrapper around the ALI C engine via JNI.
 *
 * All methods are safe to call from any thread, but [search] should be
 * called off the main thread (use a coroutine or WorkManager).
 */
class AliEngine private constructor(private val handle: Long) {

    /** One search result. */
    data class Result(
        val url: String,
        val title: String,
        val excerpt: String,
        val score: Float,
    )

    /**
     * Execute a synchronous query.
     *
     * @param query     Raw query text.
     * @param topK      Maximum results (1–100).
     * @param timeoutMs Hard timeout in milliseconds.
     * @return List of results, possibly empty.
     */
    fun search(query: String, topK: Int = 10, timeoutMs: Int = 500): List<Result> {
        val raw: Array<String>? = nativeSearch(handle, query, topK, timeoutMs)
        return raw?.mapNotNull { row ->
            val parts = row.split('\t', limit = 4)
            if (parts.size < 4) return@mapNotNull null
            Result(
                url     = parts[0],
                title   = parts[1],
                excerpt = parts[2],
                score   = parts[3].toFloatOrNull() ?: 0f,
            )
        } ?: emptyList()
    }

    /**
     * Index a single document. Use [flush] when finished to publish the segment.
     */
    fun indexDocument(url: String, title: String, bodyText: String): Boolean {
        return nativeIndexDocument(handle, url, title, bodyText) == 0
    }

    /** Flush pending documents to a published segment. */
    fun flush(): Boolean = nativeFlush(handle) == 0

    /** Release native resources. Call from onDestroy or a coroutine scope cleanup. */
    fun close() {
        nativeClose(handle)
    }

    companion object {
        init {
            System.loadLibrary("ali_core")
        }

        /**
         * Open an ALI engine.
         *
         * @param indexRoot Absolute path to the index directory.
         * @param dbPath    Absolute path to the SQLite metadata database.
         * @return An [AliEngine] on success, or null if the engine failed to open.
         */
        fun open(indexRoot: String, dbPath: String): AliEngine? {
            val handle = nativeOpen(indexRoot, dbPath)
            return if (handle != 0L) AliEngine(handle) else null
        }

        // ── JNI declarations ─────────────────────────────────────────────────

        @JvmStatic private external fun nativeOpen(indexRoot: String, dbPath: String): Long
        @JvmStatic private external fun nativeClose(handle: Long)
        @JvmStatic private external fun nativeSearch(
            handle: Long, query: String, topK: Int, timeoutMs: Int
        ): Array<String>?
        @JvmStatic private external fun nativeIndexDocument(
            handle: Long, url: String, title: String, body: String
        ): Int
        @JvmStatic private external fun nativeFlush(handle: Long): Int
    }
}
