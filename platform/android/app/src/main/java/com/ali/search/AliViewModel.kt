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
    val isImporting:   Boolean = false,
    val importError:   String? = null,
    // ── Crawl state ───────────────────────────────────────────────────────────
    val isFetching:         Boolean = false,
    val crawlStatus:        String  = "",
    val crawlLog:           List<String> = emptyList(),
    val pendingQueueSize:   Int     = 0,
    val pagesIndexedThisSession: Int = 0,
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

    fun stopCrawler() {
        val eng = engine ?: return
        crawlJob?.cancel()
        crawlJob = null
        viewModelScope.launch {
            withContext(Dispatchers.IO) { eng.crawlerStop() }
            _index.value = _index.value.copy(
                crawlerActive = false,
                crawlStatus   = "Stopped",
            )
        }
    }

    // ── Phase 1 + 2: Fetch a single URL and index it ──────────────────────────

    private val context: Context get() = getApplication<Application>().applicationContext
    private val robotsCache = mutableMapOf<String, Pair<Long, Set<String>>>() // host → (expiry, disallowed)
    private var crawlJob: Job? = null

    private fun addLog(msg: String) {
        val log = (_index.value.crawlLog + msg).takeLast(50) // keep last 50 entries
        _index.value = _index.value.copy(crawlLog = log, crawlStatus = msg)
    }

    fun fetchAndIndex(url: String) {
        val eng = engine ?: return
        viewModelScope.launch {
            _index.value = _index.value.copy(isFetching = true, crawlStatus = "Fetching $url…")
            addLog("→ Fetching: $url")

            val result = withContext(Dispatchers.IO) {
                AliAndroidFetcher.fetch(url, context)
            }

            when (result.code) {
                FetchCode.OK -> {
                    addLog("✓ Fetched ${result.body.size / 1024} KB (${result.httpStatus})")
                    val parsed = withContext(Dispatchers.IO) {
                        AliAndroidParser.parse(result.body, result.finalUrl.ifEmpty { url })
                    }
                    if (parsed.parseError != null) {
                        addLog("⚠ Parse warning: ${parsed.parseError}")
                    }
                    val title   = parsed.title.ifBlank { url }
                    val body    = parsed.bodyText
                    if (body.isBlank()) {
                        addLog("✗ No indexable text found")
                    } else {
                        val ok = withContext(Dispatchers.IO) {
                            eng.indexDocument(parsed.url, title, body).also { eng.flush() }
                        }
                        if (ok) {
                            addLog("✓ Indexed: \"$title\"")
                            _index.value = _index.value.copy(
                                pagesIndexedThisSession = _index.value.pagesIndexedThisSession + 1
                            )
                        } else {
                            addLog("✗ Index rejected document")
                        }
                    }
                    refreshIndexState()
                }
                FetchCode.NO_NETWORK          -> addLog("✗ No network connection")
                FetchCode.BLOCKED_ADDRESS     -> addLog("✗ Blocked address (private network)")
                FetchCode.RATE_LIMITED        -> addLog("✗ Rate limited (429) — try later")
                FetchCode.RESTRICTED          -> addLog("✗ Access denied (403)")
                FetchCode.AUTH_REQUIRED       -> addLog("✗ Authentication required")
                FetchCode.UNSUPPORTED_CONTENT -> addLog("✗ Non-text content: ${result.contentType}")
                FetchCode.TIMEOUT             -> addLog("✗ Timeout fetching $url")
                FetchCode.HTTP_ERROR          -> addLog("✗ HTTP ${result.httpStatus}")
                else                          -> addLog("✗ Error: ${result.errorDetail}")
            }

            _index.value = _index.value.copy(isFetching = false)
        }
    }

    // ── Phase 4: Autonomous discovery crawler ─────────────────────────────────

    // Per-host politeness: minimum gap between requests to the same host.
    private val lastFetchTime = mutableMapOf<String, Long>()
    private val HOST_DELAY_MS = 2_000L
    private val visitedUrls   = mutableSetOf<String>()
    private val pendingQueue  = ArrayDeque<String>()

    fun startCrawler(seedUrls: List<String>) {
        if (_index.value.crawlerActive) return
        val eng = engine ?: return

        seedUrls.forEach { url ->
            if (url.isNotBlank() && visitedUrls.add(url)) {
                pendingQueue.addLast(url)
            }
        }

        _index.value = _index.value.copy(
            crawlerActive = true,
            seedUrls      = seedUrls,
            crawlStatus   = "Starting…",
        )

        crawlJob = viewModelScope.launch(Dispatchers.IO) {
            addLog("Crawler started with ${pendingQueue.size} seed URL(s)")
            eng.crawlerStart()

            while (isActive && _index.value.crawlerActive && pendingQueue.isNotEmpty()) {
                // Resource check — pause if no network
                if (!AliAndroidFetcher.isNetworkAvailable(context)) {
                    withContext(Dispatchers.Main) { addLog("⏸ No network — waiting…") }
                    delay(10_000)
                    continue
                }

                val url  = pendingQueue.removeFirst()
                val host = try { java.net.URL(url).host } catch (e: Exception) { continue }

                // Per-host politeness delay
                val now     = System.currentTimeMillis()
                val lastReq = lastFetchTime[host] ?: 0L
                val wait    = HOST_DELAY_MS - (now - lastReq)
                if (wait > 0) delay(wait)
                lastFetchTime[host] = System.currentTimeMillis()

                // Robots.txt check
                if (!isAllowedByRobots(url, host)) {
                    withContext(Dispatchers.Main) { addLog("✗ robots.txt disallows: $url") }
                    continue
                }

                withContext(Dispatchers.Main) { addLog("→ Crawling: $url") }

                val result = AliAndroidFetcher.fetch(url, context)
                if (result.code != FetchCode.OK) {
                    withContext(Dispatchers.Main) { addLog("✗ ${result.code}: $url") }
                    if (result.code == FetchCode.RATE_LIMITED) delay(30_000)
                    continue
                }

                val parsed = AliAndroidParser.parse(
                    result.body,
                    result.finalUrl.ifEmpty { url }
                )

                if (parsed.bodyText.isNotBlank()) {
                    val ok = eng.indexDocument(parsed.url, parsed.title, parsed.bodyText)
                    if (ok) {
                        eng.flush()
                        withContext(Dispatchers.Main) {
                            addLog("✓ Indexed: \"${parsed.title.take(60)}\"")
                            _index.value = _index.value.copy(
                                pagesIndexedThisSession = _index.value.pagesIndexedThisSession + 1
                            )
                            refreshIndexState()
                        }
                    }
                }

                // Enqueue discovered links
                for (link in parsed.links) {
                    if (pendingQueue.size < 500 && visitedUrls.add(link)) {
                        pendingQueue.addLast(link)
                    }
                }

                withContext(Dispatchers.Main) {
                    _index.value = _index.value.copy(pendingQueueSize = pendingQueue.size)
                }
            }

            eng.crawlerStop()
            withContext(Dispatchers.Main) {
                _index.value = _index.value.copy(
                    crawlerActive   = false,
                    crawlStatus     = "Idle — ${_index.value.pagesIndexedThisSession} pages indexed",
                    pendingQueueSize = 0,
                )
                addLog("Crawler stopped")
                refreshIndexState()
            }
        }
    }

    // ── Robots.txt ────────────────────────────────────────────────────────────

    private suspend fun isAllowedByRobots(url: String, host: String): Boolean {
        val now = System.currentTimeMillis()
        val cached = robotsCache[host]
        val disallowed: Set<String>

        if (cached != null && now < cached.first) {
            disallowed = cached.second
        } else {
            // Fetch robots.txt — give it a short timeout, treat failure as allow-all.
            val scheme      = if (url.startsWith("https")) "https" else "http"
            val robotsUrl   = "$scheme://$host/robots.txt"
            val result      = try {
                AliAndroidFetcher.fetch(robotsUrl)
            } catch (e: Exception) {
                null
            }
            disallowed = if (result?.isSuccess == true) {
                parseRobotsTxt(String(result.body, Charsets.UTF_8))
            } else emptySet()
            // Cache for 1 hour.
            robotsCache[host] = Pair(now + 3_600_000L, disallowed)
        }

        val path = try { java.net.URL(url).path } catch (e: Exception) { "/" }
        return disallowed.none { path.startsWith(it) }
    }

    private fun parseRobotsTxt(text: String): Set<String> {
        val disallowed = mutableSetOf<String>()
        var inOurAgent = false
        for (rawLine in text.lines()) {
            val line = rawLine.trim()
            when {
                line.startsWith("User-agent:", ignoreCase = true) -> {
                    val agent = line.substringAfter(":").trim()
                    inOurAgent = agent == "*" || agent.lowercase().startsWith("alibot")
                }
                inOurAgent && line.startsWith("Disallow:", ignoreCase = true) -> {
                    val path = line.substringAfter(":").trim()
                    if (path.isNotEmpty()) disallowed.add(path)
                }
            }
        }
        return disallowed
    }
    /// Reads from the app's private files/import/ directory.
    /// Push files there with: adb shell run-as com.ali.search sh -c 'cat > files/import/corpus.tsv'
    fun importFromTsv(filename: String = "corpus.tsv") {
        val eng = engine ?: run {
            _index.value = _index.value.copy(importError = "Engine not ready")
            return
        }
        val app = getApplication<android.app.Application>()
        val filePath = java.io.File(app.filesDir, "import/$filename").absolutePath
        viewModelScope.launch {
            _index.value = _index.value.copy(isImporting = true, importError = null)
            val result = withContext(Dispatchers.IO) {
                var indexed = 0
                var skipped = 0
                try {
                    java.io.BufferedReader(java.io.FileReader(filePath)).use { reader ->
                        reader.lineSequence()
                            .filter { it.isNotBlank() && !it.startsWith("#") }
                            .forEach { line ->
                                val parts = line.split("\t", limit = 3)
                                if (parts.size >= 3) {
                                    if (eng.indexDocument(parts[0], parts[1], parts[2])) {
                                        indexed++
                                    } else {
                                        skipped++
                                    }
                                } else {
                                    skipped++
                                }
                            }
                    }
                    eng.flush()
                    "Indexed $indexed docs, skipped $skipped"
                } catch (e: Exception) {
                    "Error: ${e.message}"
                }
            }
            _index.value = _index.value.copy(isImporting = false, importError = result)
            refreshIndexState()
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
