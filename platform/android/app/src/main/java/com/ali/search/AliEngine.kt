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

    // ── Crawler control ───────────────────────────────────────────────────────

    /**
     * Start the background crawler. Returns immediately; work runs on internal
     * threads inside the C++ core. Use [AliCrawlerWorker] for Android lifecycle
     * management — do not call this from the UI thread directly.
     */
    fun crawlerStart(): Boolean  = nativeCrawlerStart(handle)  == 0

    /** Gracefully pause the crawler (completes in-flight requests). */
    fun crawlerPause(): Boolean  = nativeCrawlerPause(handle)  == 0

    /** Stop the crawler and wait for all threads to exit. */
    fun crawlerStop(): Boolean   = nativeCrawlerStop(handle)   == 0

    /**
     * Enqueue a URL for crawling. The URL is persisted in SQLite so it
     * survives process death and will be crawled when conditions permit.
     */
    fun crawlerEnqueue(url: String): Boolean =
        nativeCrawlerEnqueue(handle, url) == 0

    /**
     * Push current device conditions to the native engine.
     * Call this before crawling and periodically during long crawl sessions.
     */
    fun updateDeviceConditions(
        networkAvailable: Boolean,
        networkMetered: Boolean,
        charging: Boolean,
        batteryLevelPct: Int,
        thermalThrottling: Boolean,
        memoryPressure: Boolean,
        freeStorageBytes: Long,
    ): Boolean = nativeUpdateDeviceConditions(
        handle,
        if (networkAvailable) 1 else 0,
        if (networkMetered) 1 else 0,
        if (charging) 1 else 0,
        batteryLevelPct,
        if (thermalThrottling) 1 else 0,
        if (memoryPressure) 1 else 0,
        freeStorageBytes,
    ) == 0

    // ── Statistics ────────────────────────────────────────────────────────────

    /** Total documents indexed since the engine was opened. */
    fun docCount(): Long {
        val out = LongArray(1)
        nativeDocCount(handle, out)
        return out[0]
    }

    /** Number of active (non-tombstoned) segments. */
    fun segmentCount(): Int {
        val out = IntArray(1)
        nativeSegmentCount(handle, out)
        return out[0]
    }

    /** Total on-disk size of the index in bytes. */
    fun indexBytes(): Long {
        val out = LongArray(1)
        nativeIndexBytes(handle, out)
        return out[0]
    }

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
        @JvmStatic private external fun nativeCrawlerStart(handle: Long): Int
        @JvmStatic private external fun nativeCrawlerPause(handle: Long): Int
        @JvmStatic private external fun nativeCrawlerStop(handle: Long): Int
        @JvmStatic private external fun nativeCrawlerEnqueue(handle: Long, url: String): Int
        @JvmStatic private external fun nativeUpdateDeviceConditions(
            handle: Long,
            networkAvailable: Int, networkMetered: Int, charging: Int,
            batteryLevelPct: Int, thermalThrottling: Int,
            memoryPressure: Int, freeStorageBytes: Long,
        ): Int
        @JvmStatic private external fun nativeDocCount(handle: Long, out: LongArray): Int
        @JvmStatic private external fun nativeSegmentCount(handle: Long, out: IntArray): Int
        @JvmStatic private external fun nativeIndexBytes(handle: Long, out: LongArray): Int
    }
}
