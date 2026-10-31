package com.ali.search

// AliViewModel.kt — ViewModel bridging the ALI engine to the Compose UI.
//
// All engine calls that block (search, index, benchmark) run on
// Dispatchers.IO so the main thread is never blocked.
// The ViewModel survives configuration changes (rotation, dark mode toggle).
//
// State is exposed as StateFlow so Compose can observe it efficiently.
// No business logic lives in the UI layer — this ViewModel is the
// single source of truth for all screen state.

import android.app.ActivityManager
import android.app.Application
import android.os.Debug
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// ── UI state models ───────────────────────────────────────────────────────────

data class SearchState(
    val query:    String             = "",
    val results:  List<AliEngine.Result> = emptyList(),
    val isLoading: Boolean           = false,
    val elapsedMs: Long              = 0,
    val blocksRead: Int              = 0,
    val error:    String?            = null,
)

data class IndexState(
    val docCount:      Long   = 0L,
    val segmentCount:  Int    = 0,
    val indexBytes:    Long   = 0L,
    val crawlerActive: Boolean = false,
    val seedUrls:      List<String> = emptyList(),
)

data class BenchmarkState(
    val isRunning:    Boolean          = false,
    val results:      List<BenchmarkRun> = emptyList(),
    val currentRssKb: Long            = 0L,
    val error:        String?         = null,
)

data class BenchmarkRun(
    val label:         String,
    val docCount:      Long,
    val p50Ms:         Long,
    val p95Ms:         Long,
    val p99Ms:         Long,
    val rssDeltaKb:    Long,
    val avgBlocksRead: Int,
    val avgCandidates: Int,
)

// ── ViewModel ─────────────────────────────────────────────────────────────────

class AliViewModel(application: Application) : AndroidViewModel(application) {

    private val engine: AliEngine?
        get() = AliApplication.engine

    // ── Search state ─────────────────────────────────────────────────────────

    private val _search = MutableStateFlow(SearchState())
    val search: StateFlow<SearchState> = _search.asStateFlow()

    fun onQueryChange(query: String) {
        _search.value = _search.value.copy(query = query)
        if (query.isBlank()) {
            _search.value = _search.value.copy(results = emptyList(), error = null)
            return
        }
        // Trigger search on every keystroke (debounce handled by Compose LaunchedEffect).
        executeSearch(query)
    }

    fun executeSearch(query: String = _search.value.query) {
        if (query.isBlank()) return
        val eng = engine ?: run {
            _search.value = _search.value.copy(error = "Engine not ready")
            return
        }
        viewModelScope.launch {
            _search.value = _search.value.copy(isLoading = true, error = null)
            val t0 = System.currentTimeMillis()
            val results = withContext(Dispatchers.IO) {
                eng.search(query, topK = 10, timeoutMs = 2000)
            }
            val elapsed = System.currentTimeMillis() - t0
            _search.value = _search.value.copy(
                results   = results,
                isLoading = false,
                elapsedMs = elapsed,
            )
        }
    }

    // ── Index state ───────────────────────────────────────────────────────────

    private val _index = MutableStateFlow(IndexState())
    val index: StateFlow<IndexState> = _index.asStateFlow()

    fun refreshIndexState() {
        val eng = engine ?: return
        viewModelScope.launch {
            val (docs, segs, bytes) = withContext(Dispatchers.IO) {
                Triple(eng.docCount(), eng.segmentCount(), eng.indexBytes())
            }
            _index.value = _index.value.copy(
                docCount     = docs,
                segmentCount = segs,
                indexBytes   = bytes,
            )
        }
    }

    fun startCrawler(seedUrls: List<String>) {
        val eng = engine ?: return
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                seedUrls.forEach { eng.crawlerEnqueue(it) }
                eng.crawlerStart()
            }
            _index.value = _index.value.copy(
                crawlerActive = true,
                seedUrls = seedUrls,
            )
        }
    }

    fun stopCrawler() {
        val eng = engine ?: return
        viewModelScope.launch {
            withContext(Dispatchers.IO) { eng.crawlerStop() }
            _index.value = _index.value.copy(crawlerActive = false)
        }
    }

    // ── Benchmark state ───────────────────────────────────────────────────────

    private val _benchmark = MutableStateFlow(BenchmarkState())
    val benchmark: StateFlow<BenchmarkState> = _benchmark.asStateFlow()

    private val benchQueries = listOf(
        "search engine", "mobile device", "index posting",
        "block wand", "locality memory", "crawler robots",
        "bm25 ranking", "android kotlin", "segment manifest",
        "battery network",
    )

    fun runBenchmark() {
        val eng = engine ?: run {
            _benchmark.value = _benchmark.value.copy(error = "Engine not ready")
            return
        }
        viewModelScope.launch {
            _benchmark.value = _benchmark.value.copy(isRunning = true, error = null)

            val rssBefore = currentRssKb()
            val latencies = mutableListOf<Long>()
            var totalBlocks = 0L
            var totalCandidates = 0L

            withContext(Dispatchers.IO) {
                // 5 warm iterations across all queries.
                repeat(5) {
                    for (q in benchQueries) {
                        val t0 = System.currentTimeMillis()
                        val results = eng.search(q, topK = 10, timeoutMs = 2000)
                        latencies.add(System.currentTimeMillis() - t0)
                        results.size // access to avoid dead-code elimination
                    }
                }
            }

            val rssAfter  = currentRssKb()
            val sorted    = latencies.sorted()
            val n         = sorted.size
            val p50       = sorted[(n * 0.50).toInt().coerceAtMost(n - 1)]
            val p95       = sorted[(n * 0.95).toInt().coerceAtMost(n - 1)]
            val p99       = sorted[(n * 0.99).toInt().coerceAtMost(n - 1)]

            val run = BenchmarkRun(
                label         = "device_warm",
                docCount      = eng.docCount(),
                p50Ms         = p50,
                p95Ms         = p95,
                p99Ms         = p99,
                rssDeltaKb    = rssAfter - rssBefore,
                avgBlocksRead = 0,  // populated by native metrics in future
                avgCandidates = 0,
            )
            _benchmark.value = _benchmark.value.copy(
                isRunning     = false,
                results       = _benchmark.value.results + run,
                currentRssKb  = rssAfter,
            )
        }
    }

    // ── Memory measurement ───────────────────────────────────────────────────

    private fun currentRssKb(): Long {
        return try {
            val info = Debug.MemoryInfo()
            Debug.getMemoryInfo(info)
            // getTotalPss() is the best proxy for actual memory use on Android.
            info.totalPss.toLong()
        } catch (e: Exception) {
            0L
        }
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    override fun onCleared() {
        super.onCleared()
        // Do NOT close the engine here — AliApplication owns its lifetime.
    }
}
