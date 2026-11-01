package com.ali.search

// AliViewModel.kt — ViewModel bridging the ALI engine to the Compose UI.
//
// Threading rules (strictly enforced):
//   - _index / _search / _benchmark are only mutated on the MAIN dispatcher.
//   - Blocking work (engine calls, network) runs on Dispatchers.IO.
//   - The crawler loop runs on the MAIN dispatcher with IO calls suspended
//     via withContext(IO) — this ensures state mutations are always safe.

import android.app.Application
import android.content.Context
import android.os.Debug
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// ── State models ──────────────────────────────────────────────────────────────

data class SearchState(
    val query:     String                = "",
    val results:   List<AliEngine.Result> = emptyList(),
    val isLoading: Boolean               = false,
    val elapsedMs: Long                  = 0,
    val error:     String?               = null,
)

data class IndexState(
    val docCount:     Long          = 0L,
    val segmentCount: Int           = 0,
    val indexBytes:   Long          = 0L,
    // Single-URL fetch
    val isFetching:   Boolean       = false,
    // Crawler
    val crawlerActive:        Boolean       = false,
    val crawlLog:             List<String>  = emptyList(),
    val pendingQueueSize:     Int           = 0,
    val pagesIndexedSession:  Int           = 0,
    // TSV import (debug helper)
    val isImporting:  Boolean       = false,
    val importResult: String?       = null,
)

data class BenchmarkState(
    val isRunning: Boolean            = false,
    val results:   List<BenchmarkRun> = emptyList(),
    val error:     String?            = null,
)

data class BenchmarkRun(
    val label:      String,
    val docCount:   Long,
    val p50Ms:      Long,
    val p95Ms:      Long,
    val p99Ms:      Long,
    val rssDeltaKb: Long,
)

// ─────────────────────────────────────────────────────────────────────────────

class AliViewModel(application: Application) : AndroidViewModel(application) {

    private val ctx: Context get() = getApplication<Application>().applicationContext
    private val engine: AliEngine? get() = AliApplication.engine

    // ── State ─────────────────────────────────────────────────────────────────

    private val _search    = MutableStateFlow(SearchState())
    private val _index     = MutableStateFlow(IndexState())
    private val _benchmark = MutableStateFlow(BenchmarkState())

    val search:    StateFlow<SearchState>    = _search.asStateFlow()
    val index:     StateFlow<IndexState>     = _index.asStateFlow()
    val benchmark: StateFlow<BenchmarkState> = _benchmark.asStateFlow()

    // ── Log helper (MUST be called on main thread) ────────────────────────────

    private fun log(msg: String) {
        // Called on main — safe to mutate StateFlow.
        val newLog = (_index.value.crawlLog + msg).takeLast(60)
        _index.value = _index.value.copy(crawlLog = newLog)
    }

    // ── Index stats ───────────────────────────────────────────────────────────

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

    // ── Search ────────────────────────────────────────────────────────────────

    fun onQueryChange(q: String) {
        _search.value = _search.value.copy(query = q)
        if (q.isBlank()) { _search.value = _search.value.copy(results = emptyList()); return }
        executeSearch(q)
    }

    fun executeSearch(query: String = _search.value.query) {
        if (query.isBlank()) return
        val eng = engine ?: run { _search.value = _search.value.copy(error = "Engine not ready"); return }
        viewModelScope.launch {
            _search.value = _search.value.copy(isLoading = true, error = null)
            val t0 = System.currentTimeMillis()
            val results = withContext(Dispatchers.IO) { eng.search(query, topK = 10, timeoutMs = 2000) }
            _search.value = _search.value.copy(
                results   = results,
                isLoading = false,
                elapsedMs = System.currentTimeMillis() - t0,
            )
        }
    }

    // ── Phase 2: Fetch and index a single URL ─────────────────────────────────

    fun fetchAndIndex(url: String) {
        val eng = engine ?: return
        // Runs on main; suspends into IO for blocking calls.
        viewModelScope.launch {
            _index.value = _index.value.copy(isFetching = true)
            log("→ Fetching: $url")

            val result = withContext(Dispatchers.IO) { AliAndroidFetcher.fetch(url, ctx) }

            when (result.code) {
                FetchCode.OK -> {
                    log("✓ ${result.body.size / 1024} KB received (HTTP ${result.httpStatus})")
                    val parsed = withContext(Dispatchers.IO) {
                        AliAndroidParser.parse(result.body, result.finalUrl.ifEmpty { url })
                    }
                    if (parsed.bodyText.isBlank()) {
                        log("✗ No indexable text extracted")
                    } else {
                        val title = parsed.title.ifBlank { url }
                        val ok    = withContext(Dispatchers.IO) {
                            eng.indexDocument(parsed.url, title, parsed.bodyText)
                                .also { if (it) eng.flush() }
                        }
                        if (ok) {
                            log("✓ Indexed: \"${title.take(70)}\"")
                            _index.value = _index.value.copy(
                                pagesIndexedSession = _index.value.pagesIndexedSession + 1
                            )
                            refreshIndexState()
                        } else {
                            log("✗ Engine rejected document")
                        }
                    }
                }
                FetchCode.NO_NETWORK          -> log("✗ No network")
                FetchCode.BLOCKED_ADDRESS     -> log("✗ Blocked address (private network)")
                FetchCode.RATE_LIMITED        -> log("✗ Rate limited (HTTP 429)")
                FetchCode.RESTRICTED          -> log("✗ Access denied (HTTP 403)")
                FetchCode.AUTH_REQUIRED       -> log("✗ Authentication required")
                FetchCode.UNSUPPORTED_CONTENT -> log("✗ Non-text content: ${result.contentType}")
                FetchCode.TIMEOUT             -> log("✗ Timeout")
                FetchCode.HTTP_ERROR          -> log("✗ HTTP ${result.httpStatus}")
                else                          -> log("✗ Error: ${result.errorDetail}")
            }

            _index.value = _index.value.copy(isFetching = false)
        }
    }

    // ── Phase 4: Autonomous discovery crawler ─────────────────────────────────
    // Runs entirely on the MAIN dispatcher — only IO calls are suspended to IO.
    // This guarantees all _index mutations are on the correct thread.

    private var crawlJob: Job? = null

    // Session state — reset each time startCrawler is called.
    private val visitedUrls  = mutableSetOf<String>()
    private val pendingQueue = ArrayDeque<String>()
    private val hostLastFetch = mutableMapOf<String, Long>()
    private val robotsCache  = mutableMapOf<String, Pair<Long, Set<String>>>()
    private val HOST_DELAY_MS = 2_000L

    fun startCrawler(seedUrls: List<String>) {
        if (_index.value.crawlerActive) return
        val eng = engine ?: return

        // Reset session state.
        visitedUrls.clear()
        pendingQueue.clear()
        hostLastFetch.clear()

        seedUrls.filter { it.startsWith("http") }.forEach { url ->
            if (visitedUrls.add(url)) pendingQueue.addLast(url)
        }

        if (pendingQueue.isEmpty()) { log("✗ No valid seed URLs"); return }

        // Derive allowed domain from the first seed URL (stay on that site).
        val seedHost = try { java.net.URL(seedUrls.first()).host } catch (e: Exception) { "" }

        _index.value = _index.value.copy(
            crawlerActive       = true,
            pagesIndexedSession = 0,
            pendingQueueSize    = pendingQueue.size,
        )
        log("Crawler started — seed domain: $seedHost")

        crawlJob = viewModelScope.launch {  // default = Main
            while (isActive && pendingQueue.isNotEmpty()) {
                // Check network — pause on main, no state mutation needed.
                val hasNetwork = withContext(Dispatchers.IO) {
                    AliAndroidFetcher.isNetworkAvailable(ctx)
                }
                if (!hasNetwork) {
                    log("⏸ No network — waiting 10 s")
                    delay(10_000)
                    continue
                }

                val url  = pendingQueue.removeFirst()
                val host = try { java.net.URL(url).host } catch (e: Exception) { continue }

                // Politeness delay.
                val waited = System.currentTimeMillis() - (hostLastFetch[host] ?: 0L)
                if (waited < HOST_DELAY_MS) delay(HOST_DELAY_MS - waited)
                hostLastFetch[host] = System.currentTimeMillis()

                // Robots.txt check (IO).
                val allowed = withContext(Dispatchers.IO) { checkRobots(url, host) }
                if (!allowed) { log("✗ robots.txt: $url"); continue }

                log("→ $url")

                val result = withContext(Dispatchers.IO) { AliAndroidFetcher.fetch(url, ctx) }

                if (result.code == FetchCode.RATE_LIMITED) {
                    log("⏸ Rate limited — waiting 30 s")
                    // Put URL back for retry.
                    pendingQueue.addFirst(url)
                    delay(30_000)
                    continue
                }

                if (result.code != FetchCode.OK) {
                    log("✗ ${result.code.name.lowercase()}: $url")
                    continue
                }

                val parsed = withContext(Dispatchers.IO) {
                    AliAndroidParser.parse(result.body, result.finalUrl.ifEmpty { url })
                }

                if (parsed.bodyText.isNotBlank()) {
                    val title = parsed.title.ifBlank { url }
                    val ok    = withContext(Dispatchers.IO) {
                        eng.indexDocument(parsed.url, title, parsed.bodyText)
                            .also { if (it) eng.flush() }
                    }
                    if (ok) {
                        log("✓ \"${title.take(60)}\"")
                        _index.value = _index.value.copy(
                            pagesIndexedSession = _index.value.pagesIndexedSession + 1
                        )
                        refreshIndexState()
                    }
                }

                // Enqueue links that stay on the seed host (avoids crawling the whole web).
                var newLinks = 0
                for (link in parsed.links) {
                    val linkHost = try { java.net.URL(link).host } catch (e: Exception) { continue }
                    if (linkHost != seedHost) continue               // stay on seed domain
                    if (pendingQueue.size >= 200) break              // bounded queue
                    if (visitedUrls.add(link)) { pendingQueue.addLast(link); newLinks++ }
                }

                _index.value = _index.value.copy(pendingQueueSize = pendingQueue.size)
                if (newLinks > 0) log("  queued $newLinks new links (${pendingQueue.size} pending)")
            }

            // Done.
            withContext(Dispatchers.IO) { eng.crawlerStop() }
            _index.value = _index.value.copy(
                crawlerActive    = false,
                pendingQueueSize = 0,
            )
            log("Crawler finished — ${_index.value.pagesIndexedSession} pages indexed this session")
            refreshIndexState()
        }
    }

    fun stopCrawler() {
        crawlJob?.cancel()
        crawlJob = null
        viewModelScope.launch {
            withContext(Dispatchers.IO) { engine?.crawlerStop() }
            _index.value = _index.value.copy(crawlerActive = false, pendingQueueSize = 0)
            log("Crawler stopped by user")
        }
    }

    // ── Robots helpers (called from IO) ───────────────────────────────────────

    private fun checkRobots(url: String, host: String): Boolean {
        val now    = System.currentTimeMillis()
        val cached = robotsCache[host]
        val disallowed: Set<String>

        if (cached != null && now < cached.first) {
            disallowed = cached.second
        } else {
            val scheme    = if (url.startsWith("https")) "https" else "http"
            val r         = try { AliAndroidFetcher.fetch("$scheme://$host/robots.txt") } catch (e: Exception) { null }
            disallowed    = if (r?.isSuccess == true) parseRobots(String(r.body, Charsets.UTF_8)) else emptySet()
            robotsCache[host] = Pair(now + 3_600_000L, disallowed)
        }

        return try {
            val path = java.net.URL(url).path
            disallowed.none { path.startsWith(it) }
        } catch (e: Exception) { true }
    }

    private fun parseRobots(text: String): Set<String> {
        val out = mutableSetOf<String>()
        var ours = false
        for (line in text.lines()) {
            val l = line.trim()
            when {
                l.startsWith("User-agent:", ignoreCase = true) -> {
                    val a = l.substringAfter(":").trim()
                    ours = a == "*" || a.lowercase().startsWith("alibot")
                }
                ours && l.startsWith("Disallow:", ignoreCase = true) -> {
                    val p = l.substringAfter(":").trim()
                    if (p.isNotEmpty()) out.add(p)
                }
            }
        }
        return out
    }

    // ── TSV import (debug helper) ─────────────────────────────────────────────

    fun importFromTsv(filename: String = "corpus.tsv") {
        val eng = engine ?: run { _index.value = _index.value.copy(importResult = "Engine not ready"); return }
        val path = java.io.File(ctx.filesDir, "import/$filename").absolutePath
        viewModelScope.launch {
            _index.value = _index.value.copy(isImporting = true, importResult = null)
            val msg = withContext(Dispatchers.IO) {
                var ok = 0; var skip = 0
                try {
                    java.io.BufferedReader(java.io.FileReader(path)).use { r ->
                        r.lineSequence().filter { it.isNotBlank() && !it.startsWith("#") }.forEach { line ->
                            val p = line.split("\t", limit = 3)
                            if (p.size >= 3 && eng.indexDocument(p[0], p[1], p[2])) ok++ else skip++
                        }
                    }
                    eng.flush()
                    "Indexed $ok, skipped $skip"
                } catch (e: Exception) { "Error: ${e.message}" }
            }
            _index.value = _index.value.copy(isImporting = false, importResult = msg)
            refreshIndexState()
        }
    }

    // ── Benchmark ─────────────────────────────────────────────────────────────

    private val benchQueries = listOf(
        "search engine", "mobile device", "index posting", "block wand",
        "locality memory", "crawler robots", "bm25 ranking", "android kotlin",
        "segment manifest", "battery network",
    )

    fun runBenchmark() {
        val eng = engine ?: run { _benchmark.value = _benchmark.value.copy(error = "Engine not ready"); return }
        viewModelScope.launch {
            _benchmark.value = _benchmark.value.copy(isRunning = true, error = null)
            val rssBefore = pssKb()
            val lats = mutableListOf<Long>()
            withContext(Dispatchers.IO) {
                repeat(5) {
                    benchQueries.forEach { q ->
                        val t0 = System.currentTimeMillis()
                        eng.search(q, topK = 10, timeoutMs = 2000).size
                        lats.add(System.currentTimeMillis() - t0)
                    }
                }
            }
            val sorted = lats.sorted(); val n = sorted.size
            _benchmark.value = _benchmark.value.copy(
                isRunning = false,
                results   = _benchmark.value.results + BenchmarkRun(
                    label      = "device_warm",
                    docCount   = eng.docCount(),
                    p50Ms      = sorted[(n * 0.50).toInt().coerceAtMost(n - 1)],
                    p95Ms      = sorted[(n * 0.95).toInt().coerceAtMost(n - 1)],
                    p99Ms      = sorted[(n * 0.99).toInt().coerceAtMost(n - 1)],
                    rssDeltaKb = pssKb() - rssBefore,
                ),
            )
        }
    }

    private fun pssKb(): Long = try {
        Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }.totalPss.toLong()
    } catch (e: Exception) { 0L }

    override fun onCleared() { super.onCleared(); crawlJob?.cancel() }
}
