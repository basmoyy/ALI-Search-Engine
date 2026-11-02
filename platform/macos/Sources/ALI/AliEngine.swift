// AliEngine.swift — Swift wrapper around the ALI C API (ali.h).
//
// This is the only file that touches the C API directly.
// Everything else in the app goes through this class.

import Foundation

// MARK: - Result types

struct SearchResult: Identifiable, Hashable {
    let id     = UUID()
    let rank:    Int
    let url:     String
    let title:   String
    let excerpt: String
    let score:   Float
}

struct GrammarCandidate: Identifiable {
    let id           = UUID()
    let grammarName:  String
    let url:          String
    let domain:       String
    let confidence:   Float
}

struct EngineStats {
    var docCount:     UInt64 = 0
    var segmentCount: UInt32 = 0
    var indexBytes:   UInt64 = 0
    var sourceCount:  UInt32 = 0
    var navEdgeCount: UInt32 = 0
    var grammarCount: UInt32 = 0
}

// MARK: - AliEngine

/// Thread-safe wrapper around the ALI C engine.
/// All blocking calls must be dispatched to a background queue by the caller.
final class AliEngine {

    // Opaque C handle
    private var handle: OpaquePointer?
    private let lock = NSLock()

    // Paths
    let indexRoot: String
    let dbPath:    String

    // MARK: Lifecycle

    init?(indexRoot: String, dbPath: String) {
        self.indexRoot = indexRoot
        self.dbPath    = dbPath

        // Ensure directories exist
        try? FileManager.default.createDirectory(
            atPath: indexRoot,
            withIntermediateDirectories: true)

        var eng: OpaquePointer?
        let rc = indexRoot.withCString { root in
            dbPath.withCString { db in
                ali_engine_open(root, db, &eng)
            }
        }
        guard rc == ALI_OK, let eng else { return nil }
        self.handle = eng
    }

    deinit { close() }

    func close() {
        lock.lock(); defer { lock.unlock() }
        if let h = handle { ali_engine_close(h) }
        handle = nil
    }

    // MARK: Search

    /// Synchronous BM25 search over the local index.
    /// Call off the main thread.
    func search(query: String, topK: Int = 15, timeoutMs: UInt32 = 2000) -> [SearchResult] {
        lock.lock(); defer { lock.unlock() }
        guard let h = handle else { return [] }

        var results: OpaquePointer?
        let rc = ali_search(h, query, UInt32(topK), timeoutMs, &results)
        guard rc == ALI_OK || rc == ALI_ERR_TIMEOUT, let results else { return [] }
        defer { ali_results_free(results) }

        let count = ali_results_count(results)
        var out: [SearchResult] = []
        out.reserveCapacity(Int(count))
        for i in 0..<count {
            var urlPtr:     UnsafePointer<CChar>? = nil
            var titlePtr:   UnsafePointer<CChar>? = nil
            var excerptPtr: UnsafePointer<CChar>? = nil
            var score: Float = 0
            ali_results_get(results, i, &urlPtr, &titlePtr, &excerptPtr, &score)
            out.append(SearchResult(
                rank:    Int(i) + 1,
                url:     urlPtr.map(String.init(cString:)) ?? "",
                title:   titlePtr.map(String.init(cString:)) ?? "",
                excerpt: excerptPtr.map(String.init(cString:)) ?? "",
                score:   score
            ))
        }
        return out
    }

    // MARK: Grammar-guided discovery

    /// Construct candidate URLs from the Grammar Registry.
    func grammarConstruct(query: String, maxResults: Int = 5) -> [GrammarCandidate] {
        lock.lock(); defer { lock.unlock() }
        guard let h = handle else { return [] }

        var resultsPtr: OpaquePointer?
        var count: UInt32 = 0
        let rc = ali_grammar_construct(h, query, UInt32(maxResults), &resultsPtr, &count)
        guard rc == ALI_OK, let rp = resultsPtr else { return [] }
        defer { ali_grammar_results_free(rp) }

        var out: [GrammarCandidate] = []
        for i in 0..<count {
            var name:   UnsafePointer<CChar>? = nil
            var url:    UnsafePointer<CChar>? = nil
            var domain: UnsafePointer<CChar>? = nil
            var conf: Float = 0
            ali_grammar_result_get(rp, i, &name, &url, &domain, &conf)
            out.append(GrammarCandidate(
                grammarName: name.map(String.init(cString:)) ?? "",
                url:         url.map(String.init(cString:)) ?? "",
                domain:      domain.map(String.init(cString:)) ?? "",
                confidence:  conf
            ))
        }
        return out
    }

    // MARK: Indexing

    func indexDocument(url: String, title: String, body: String) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard let h = handle else { return false }
        return ali_index_document(h, url, title, body) == ALI_OK
    }

    func flush() -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard let h = handle else { return false }
        return ali_index_flush(h) == ALI_OK
    }

    // MARK: Crawler

    func crawlerStart() { lock.lock(); defer { lock.unlock() }; if let h = handle { ali_crawler_start(h) } }
    func crawlerPause() { lock.lock(); defer { lock.unlock() }; if let h = handle { ali_crawler_pause(h) } }
    func crawlerStop()  { lock.lock(); defer { lock.unlock() }; if let h = handle { ali_crawler_stop(h) } }

    func crawlerEnqueue(url: String) {
        lock.lock(); defer { lock.unlock() }
        guard let h = handle else { return }
        ali_crawler_enqueue_url(h, url)
    }

    // MARK: Stats

    func stats() -> EngineStats {
        lock.lock(); defer { lock.unlock() }
        guard let h = handle else { return EngineStats() }
        var s = EngineStats()
        ali_stats_doc_count(h, &s.docCount)
        ali_stats_segment_count(h, &s.segmentCount)
        ali_stats_index_bytes(h, &s.indexBytes)
        ali_stats_source_count(h, &s.sourceCount)
        ali_stats_nav_edge_count(h, &s.navEdgeCount)
        ali_stats_grammar_count(h, &s.grammarCount)
        return s
    }

    // MARK: Resource conditions

    func updateDeviceConditions(
        networkAvailable: Bool = true,
        networkMetered: Bool = false,
        charging: Bool = true,
        batteryPct: Int = 100,
        thermal: Bool = false,
        memPressure: Bool = false,
        freeBytes: UInt64 = 10_000_000_000
    ) {
        lock.lock(); defer { lock.unlock() }
        guard let h = handle else { return }
        ali_update_device_conditions(
            h,
            networkAvailable ? 1 : 0,
            networkMetered   ? 1 : 0,
            charging         ? 1 : 0,
            Int32(batteryPct),
            thermal          ? 1 : 0,
            memPressure      ? 1 : 0,
            freeBytes
        )
    }
}
