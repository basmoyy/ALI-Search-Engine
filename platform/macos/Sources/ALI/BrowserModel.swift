// BrowserModel.swift — Tab, history and bookmark state.
// Swift 6 concurrency: engine is accessed only on bgQueue, so we
// capture it directly to avoid @MainActor isolation warnings.

import Foundation
import Combine

// Silence Swift 6 strict concurrency for DispatchQueue-based patterns.
// AliEngine is internally locked (NSLock), so cross-actor access is safe.
nonisolated(unsafe) private var _suppressWarning = false

// MARK: - Tab

final class BrowserTab: ObservableObject, Identifiable {
    let id = UUID()

    @Published var url:     URL?
    @Published var title:   String = "New Tab"
    @Published var isLoading = false
    @Published var canGoBack    = false
    @Published var canGoForward = false
    @Published var loadProgress: Double = 0

    // WebViewCoordinator uses this to receive commands from the model
    enum Command { case load(URL), goBack, goForward, reload, stopLoad }
    let commandSubject = PassthroughSubject<Command, Never>()

    func load(_ url: URL) {
        self.url = url
        commandSubject.send(.load(url))
    }
    func goBack()    { commandSubject.send(.goBack) }
    func goForward() { commandSubject.send(.goForward) }
    func reload()    { commandSubject.send(.reload) }
    func stopLoad()  { commandSubject.send(.stopLoad) }
}

// MARK: - HistoryItem

struct HistoryItem: Identifiable, Codable {
    let id:        UUID
    let url:       String
    let title:     String
    let visitedAt: Date

    init(url: String, title: String) {
        self.id        = UUID()
        self.url       = url
        self.title     = title
        self.visitedAt = Date()
    }
}

// MARK: - Bookmark

struct Bookmark: Identifiable, Codable {
    let id:        UUID
    var url:       String
    var title:     String
    let addedAt:   Date

    init(url: String, title: String) {
        self.id      = UUID()
        self.url     = url
        self.title   = title
        self.addedAt = Date()
    }
}

// MARK: - BrowserModel (central ViewModel)

@MainActor
final class BrowserModel: ObservableObject {

    // ── Tabs ──────────────────────────────────────────────────────────────────
    @Published var tabs: [BrowserTab] = []
    @Published var activeTabIndex: Int = 0

    var activeTab: BrowserTab? {
        guard tabs.indices.contains(activeTabIndex) else { return nil }
        return tabs[activeTabIndex]
    }

    // ── Search ────────────────────────────────────────────────────────────────
    @Published var searchResults:  [SearchResult]   = []
    @Published var grammarResults: [GrammarCandidate] = []
    @Published var isSearching     = false
    @Published var searchQuery     = ""
    @Published var searchError:    String? = nil

    // ── History & Bookmarks ───────────────────────────────────────────────────
    @Published var history:   [HistoryItem] = []
    @Published var bookmarks: [Bookmark]    = []

    // ── Engine stats ──────────────────────────────────────────────────────────
    @Published var stats = EngineStats()

    // ── Engine ────────────────────────────────────────────────────────────────
    private(set) var engine: AliEngine?
    private let bgQueue = DispatchQueue(label: "ali.engine", qos: .userInitiated)
    private var statsTimer: Timer?

    // MARK: Lifecycle

    init() {
        loadPersisted()
        openEngine()
        newTab()
        startStatsTimer()
    }

    private func openEngine() {
        let support = FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask).first!
        let aliDir = support.appendingPathComponent("ALI")
        let indexRoot = aliDir.appendingPathComponent("index").path
        let dbPath    = aliDir.appendingPathComponent("meta.db").path
        engine = AliEngine(indexRoot: indexRoot, dbPath: dbPath)
        engine?.updateDeviceConditions()
    }

    // MARK: Tabs

    func newTab(url: URL? = nil) {
        let tab = BrowserTab()
        if let url { tab.load(url) }
        tabs.append(tab)
        activeTabIndex = tabs.count - 1
    }

    func closeTab(id: UUID) {
        guard let idx = tabs.firstIndex(where: { $0.id == id }) else { return }
        tabs.remove(at: idx)
        if tabs.isEmpty { newTab() }
        activeTabIndex = min(activeTabIndex, tabs.count - 1)
    }

    func selectTab(_ tab: BrowserTab) {
        if let idx = tabs.firstIndex(where: { $0.id == tab.id }) {
            activeTabIndex = idx
        }
    }

    // MARK: Navigation

    func navigate(to rawURL: String) {
        let url = canonicalURL(rawURL)
        if let tab = activeTab { tab.load(url) }
        else { newTab(url: url) }
    }

    private func canonicalURL(_ raw: String) -> URL {
        let t = raw.trimmingCharacters(in: .whitespaces)
        if t.hasPrefix("http://") || t.hasPrefix("https://") {
            return URL(string: t) ?? URL(string: "https://en.wikipedia.org/wiki/" +
                                          t.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed)!)!
        }
        if t.contains(".") && !t.contains(" ") {
            return URL(string: "https://" + t) ?? URL(string: "about:blank")!
        }
        // Treat as a search query — open ALI search
        searchQuery = t
        performSearch(query: t)
        return URL(string: "about:blank")!
    }

    // MARK: History

    func recordVisit(url: String, title: String) {
        let item = HistoryItem(url: url, title: title.isEmpty ? url : title)
        history.insert(item, at: 0)
        if history.count > 1000 { history = Array(history.prefix(1000)) }
        savePersisted()
        // Feed URL into ALI discovery
        bgQueue.async { [weak self] in
            self?.engine?.crawlerEnqueue(url: url)
        }
    }

    // MARK: Bookmarks

    func addBookmark(url: String, title: String) {
        guard !bookmarks.contains(where: { $0.url == url }) else { return }
        bookmarks.insert(Bookmark(url: url, title: title), at: 0)
        savePersisted()
    }

    func removeBookmark(id: UUID) {
        bookmarks.removeAll { $0.id == id }
        savePersisted()
    }

    func isBookmarked(url: String) -> Bool {
        bookmarks.contains { $0.url == url }
    }

    // MARK: Search

    func performSearch(query: String) {
        guard !query.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        isSearching = true
        searchError = nil
        searchResults = []
        grammarResults = []

        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }

            // 1. Grammar candidates — immediate, no network needed
            let grammar = engine.grammarConstruct(query: query, maxResults: 5)

            // 2. Local BM25 search
            let local = engine.search(query: query, topK: 15)

            DispatchQueue.main.async {
                self.grammarResults = grammar
                self.searchResults  = local
                self.isSearching    = false

                // 3. If local results are sparse, trigger targeted discovery
                if local.count < 3 && !grammar.isEmpty {
                    self.discoverForQuery(query: query, candidates: grammar)
                }
            }
        }
    }

    /// Fetch grammar-constructed pages, index them, then refresh results.
    private func discoverForQuery(query: String, candidates: [GrammarCandidate]) {
        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }

            for candidate in candidates.prefix(3) {
                guard !candidate.url.isEmpty,
                      let url = URL(string: candidate.url) else { continue }

                // Lightweight HTTP fetch using URLSession (synchronous via semaphore)
                let sem = DispatchSemaphore(value: 0)
                var body = Data()
                var finalURL = candidate.url

                var req = URLRequest(url: url)
                req.timeoutInterval = 8
                req.setValue("ALIBot/1.0", forHTTPHeaderField: "User-Agent")

                URLSession.shared.dataTask(with: req) { data, resp, _ in
                    if let data { body = data }
                    if let r = resp as? HTTPURLResponse,
                       let u = r.url?.absoluteString { finalURL = u }
                    sem.signal()
                }.resume()
                sem.wait()

                guard !body.isEmpty,
                      let html = String(data: body, encoding: .utf8)
                               ?? String(data: body, encoding: .isoLatin1)
                else { continue }

                // Extract title and body text (simple heuristic extraction)
                let title   = extractTitle(html) ?? candidate.grammarName
                let text    = extractText(html)
                if text.count > 100 {
                    _ = engine.indexDocument(url: finalURL, title: title, body: text)
                }
            }
            _ = engine.flush()

            // Re-run search with newly indexed content
            let fresh = engine.search(query: query, topK: 15)
            DispatchQueue.main.async { [weak self] in
                if !fresh.isEmpty { self?.searchResults = fresh }
            }
        }
    }

    // MARK: Stats

    private func startStatsTimer() {
        statsTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
            self?.refreshStats()
        }
        refreshStats()
    }

    func refreshStats() {
        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }
            let s = engine.stats()
            DispatchQueue.main.async { self.stats = s }
        }
    }

    // MARK: Persistence

    private var historyURL: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ALI/history.json")
    }
    private var bookmarksURL: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ALI/bookmarks.json")
    }

    func savePersisted() {
        let enc = JSONEncoder(); enc.dateEncodingStrategy = .iso8601
        if let data = try? enc.encode(history) { try? data.write(to: historyURL) }
        if let data = try? enc.encode(bookmarks) { try? data.write(to: bookmarksURL) }
    }

    func loadPersisted() {
        let dec = JSONDecoder(); dec.dateDecodingStrategy = .iso8601
        if let data = try? Data(contentsOf: historyURL),
           let h    = try? dec.decode([HistoryItem].self, from: data) { history = h }
        if let data = try? Data(contentsOf: bookmarksURL),
           let b    = try? dec.decode([Bookmark].self, from: data) { bookmarks = b }
    }
}

// MARK: - Simple HTML extraction helpers

private func extractTitle(_ html: String) -> String? {
    let lower = html.lowercased()
    guard let ts = lower.range(of: "<title"),
          let te = lower.range(of: "</title>") else { return nil }
    let start = html.index(ts.upperBound, offsetBy: 0)
    // skip to ">"
    guard let gt = html[start...].firstIndex(of: ">") else { return nil }
    let content = String(html[html.index(after: gt)..<te.lowerBound])
    return content.trimmingCharacters(in: .whitespacesAndNewlines)
                  .replacingOccurrences(of: "\n", with: " ")
                  .prefix(200).description
}

private func extractText(_ html: String) -> String {
    var result = html
    // Remove scripts, styles, heads
    for tag in ["script", "style", "head", "nav", "footer"] {
        while let s = result.range(of: "<\(tag)", options: .caseInsensitive),
              let e = result.range(of: "</\(tag)>", options: .caseInsensitive,
                                   range: s.lowerBound..<result.endIndex) {
            result.removeSubrange(s.lowerBound...e.upperBound)
        }
    }
    // Strip tags
    var out = ""
    var inTag = false
    for c in result {
        if c == "<" { inTag = true }
        else if c == ">" { inTag = false; out += " " }
        else if !inTag { out.append(c) }
    }
    // Collapse whitespace
    return out.components(separatedBy: .whitespacesAndNewlines)
              .filter { !$0.isEmpty }
              .joined(separator: " ")
}
