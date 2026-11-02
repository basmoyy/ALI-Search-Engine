// BrowserModel.swift — Central ViewModel for ALI browser and search.

import Foundation
import Combine

nonisolated(unsafe) private var _unused = false

// MARK: - Tab

final class BrowserTab: ObservableObject, Identifiable {
    let id = UUID()
    @Published var url:          URL?
    @Published var title:        String = "New Tab"
    @Published var isLoading     = false
    @Published var canGoBack     = false
    @Published var canGoForward  = false
    @Published var loadProgress: Double = 0

    enum Command { case load(URL), goBack, goForward, reload, stopLoad }
    let commandSubject = PassthroughSubject<Command, Never>()

    func load(_ url: URL) { self.url = url; commandSubject.send(.load(url)) }
    func goBack()    { commandSubject.send(.goBack) }
    func goForward() { commandSubject.send(.goForward) }
    func reload()    { commandSubject.send(.reload) }
    func stopLoad()  { commandSubject.send(.stopLoad) }
}

// MARK: - History / Bookmark

struct HistoryItem: Identifiable, Codable {
    let id: UUID; let url: String; let title: String; let visitedAt: Date
    init(url: String, title: String) {
        id = UUID(); self.url = url
        self.title = title.isEmpty ? url : title; visitedAt = Date()
    }
}

struct Bookmark: Identifiable, Codable {
    let id: UUID; var url: String; var title: String; let addedAt: Date
    init(url: String, title: String) {
        id = UUID(); self.url = url; self.title = title; addedAt = Date()
    }
}

// MARK: - SearchResultKind

/// Used in the UI to show the right presentation for different result types.
enum SearchResultKind {
    case knownSite(KnownSite)          // exact match from curated list
    case localIndex(SearchResult)      // from BM25 index
    case grammarDiscovery(GrammarCandidate)  // informational query grammar hit
}

// MARK: - BrowserModel

@MainActor
final class BrowserModel: ObservableObject {

    // ── Tabs
    @Published var tabs:            [BrowserTab] = []
    @Published var activeTabIndex:  Int = 0

    var activeTab: BrowserTab? {
        guard tabs.indices.contains(activeTabIndex) else { return nil }
        return tabs[activeTabIndex]
    }

    // ── Search state
    @Published var searchQuery       = ""
    @Published var isSearching       = false
    @Published var searchResultKinds: [SearchResultKind] = []

    // ── History & Bookmarks
    @Published var history:   [HistoryItem] = []
    @Published var bookmarks: [Bookmark]    = []

    // ── Engine stats
    @Published var stats = EngineStats()

    // ── Engine
    private(set) var engine: AliEngine?
    private let bgQueue = DispatchQueue(label: "ali.engine", qos: .userInitiated)
    private var statsTimer: Timer?
    private var hasSeeded = false

    // MARK: Lifecycle

    init() {
        loadPersisted()
        openEngine()
        newTab()
        startStatsTimer()
    }

    private func openEngine() {
        guard let support = FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask).first else { return }
        let aliDir    = support.appendingPathComponent("ALI")
        let indexRoot = aliDir.appendingPathComponent("index").path
        let dbPath    = aliDir.appendingPathComponent("meta.db").path
        engine = AliEngine(indexRoot: indexRoot, dbPath: dbPath)
        engine?.updateDeviceConditions()

        // Seed the index on first launch so searches return real content
        let seedFlag = aliDir.appendingPathComponent(".seeded_v2")
        if !FileManager.default.fileExists(atPath: seedFlag.path) {
            hasSeeded = false
            seedIndexInBackground(flagPath: seedFlag)
        } else {
            hasSeeded = true
        }
    }

    // MARK: - Index seeding
    // On first launch, fetch a small starter set of real pages so that common
    // searches return actual content immediately. This replaces the empty index
    // that previously caused every search to produce nothing.

    private let seedPages: [(url: String, title: String)] = [
        ("https://en.wikipedia.org/wiki/YouTube",   "YouTube — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Reddit",    "Reddit — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Discord_(software)", "Discord — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Japan",     "Japan — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Python_(programming_language)", "Python — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Rust_(programming_language)",   "Rust — Wikipedia"),
        ("https://en.wikipedia.org/wiki/GitHub",    "GitHub — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Netflix",   "Netflix — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Amazon_(company)", "Amazon — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Twitter",   "Twitter — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Instagram", "Instagram — Wikipedia"),
        ("https://en.wikipedia.org/wiki/BBC",       "BBC — Wikipedia"),
        ("https://en.wikipedia.org/wiki/NASA",      "NASA — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Spotify",   "Spotify — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Twitch_(service)", "Twitch — Wikipedia"),
        ("https://en.wikipedia.org/wiki/TikTok",    "TikTok — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Notion_(productivity_software)", "Notion — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Stack_Overflow", "Stack Overflow — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Khan_Academy",   "Khan Academy — Wikipedia"),
        ("https://en.wikipedia.org/wiki/Running",        "Running — Wikipedia"),
    ]

    @Published var seedingProgress: Double = 0
    @Published var isSeedingIndex  = false

    private func seedIndexInBackground(flagPath: URL) {
        isSeedingIndex = true
        let pages = seedPages
        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }
            let total = Double(pages.count)
            for (i, page) in pages.enumerated() {
                let html = self.fetchHTML(page.url)
                if !html.isEmpty {
                    let text  = extractText(html)
                    let title = extractTitle(html) ?? page.title
                    if text.count > 100 {
                        _ = engine.indexDocument(url: page.url, title: title, body: text)
                    }
                }
                let progress = Double(i + 1) / total
                DispatchQueue.main.async { [weak self] in self?.seedingProgress = progress }
            }
            _ = engine.flush()
            // Mark seeded
            try? "seeded".write(to: flagPath, atomically: true, encoding: .utf8)
            DispatchQueue.main.async { [weak self] in
                self?.isSeedingIndex   = false
                self?.hasSeeded        = true
                self?.seedingProgress  = 1.0
            }
        }
    }

    private func fetchHTML(_ urlString: String) -> String {
        guard let url = URL(string: urlString) else { return "" }
        var req = URLRequest(url: url, timeoutInterval: 8)
        req.setValue("ALIBot/1.0", forHTTPHeaderField: "User-Agent")
        let sem = DispatchSemaphore(value: 0)
        var result = ""
        URLSession.shared.dataTask(with: req) { data, _, _ in
            if let data { result = String(data: data, encoding: .utf8) ?? "" }
            sem.signal()
        }.resume()
        sem.wait()
        return result
    }

    // MARK: - Tabs

    func newTab(url: URL? = nil) {
        let tab = BrowserTab()
        tabs.append(tab)
        activeTabIndex = tabs.count - 1
        if let url { tab.load(url) }
    }

    func closeTab(id: UUID) {
        guard let idx = tabs.firstIndex(where: { $0.id == id }) else { return }
        WebViewPool.shared.remove(idx < tabs.count ? tabs[idx].id : id)
        tabs.remove(at: idx)
        if tabs.isEmpty { newTab() }
        activeTabIndex = min(activeTabIndex, tabs.count - 1)
    }

    func selectTab(_ tab: BrowserTab) {
        if let idx = tabs.firstIndex(where: { $0.id == tab.id }) {
            activeTabIndex = idx
        }
    }

    // MARK: - Navigation

    func navigate(to url: URL) {
        guard let tab = activeTab else { newTab(url: url); return }
        tab.load(url)
    }

    // MARK: - History

    func recordVisit(url: String, title: String) {
        guard url != "about:blank", !url.isEmpty else { return }
        let item = HistoryItem(url: url, title: title)
        history.insert(item, at: 0)
        if history.count > 1000 { history = Array(history.prefix(1000)) }
        savePersisted()
        bgQueue.async { [weak self] in self?.engine?.crawlerEnqueue(url: url) }
    }

    // MARK: - Bookmarks

    func addBookmark(url: String, title: String) {
        guard !bookmarks.contains(where: { $0.url == url }) else { return }
        bookmarks.insert(Bookmark(url: url, title: title), at: 0)
        savePersisted()
    }
    func removeBookmark(id: UUID) { bookmarks.removeAll { $0.id == id }; savePersisted() }
    func isBookmarked(url: String) -> Bool { bookmarks.contains { $0.url == url } }

    // MARK: - Search

    func performSearch(query: String) {
        let q = query.trimmingCharacters(in: .whitespaces)
        guard !q.isEmpty else { return }
        searchQuery       = q
        isSearching       = true
        searchResultKinds = []

        // 1. Known sites — immediate, curated, no network
        let sites = KnownSites.match(query: q)
        let siteResults = sites.prefix(3).map { SearchResultKind.knownSite($0) }

        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }

            // 2. BM25 local index search
            let local = engine.search(query: q, topK: 20)

            // 3. Grammar candidates — only for multi-word informational queries
            let words = q.split(separator: " ").count
            let grammar: [GrammarCandidate] = (words >= 2)
                ? engine.grammarConstruct(query: q, maxResults: 3)
                : []

            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                var results: [SearchResultKind] = []
                results.append(contentsOf: siteResults)

                // Deduplicate local results against known sites
                let siteURLs = Set(sites.map { $0.url.lowercased() })
                for r in local {
                    // Skip if it's just a Wikipedia/Britannica page about the exact query
                    // when a known site already matched — that's not useful
                    if !siteURLs.isEmpty && isEncyclopedicAbout(r.url, query: q) { continue }
                    results.append(.localIndex(r))
                }
                for g in grammar { results.append(.grammarDiscovery(g)) }

                self.searchResultKinds = results
                self.isSearching       = false

                // Trigger background discovery if results are sparse
                if local.isEmpty && !siteURLs.isEmpty {
                    self.discoverForKnownSites(sites: Array(sites.prefix(2)))
                } else if local.count < 5 && words >= 2 && !grammar.isEmpty {
                    self.discoverForQuery(query: q, candidates: grammar)
                }
            }
        }
    }

    /// Returns true if a URL is an encyclopedic page specifically about `query`
    /// (e.g. `en.wikipedia.org/wiki/Youtube` for the query "youtube").
    private func isEncyclopedicAbout(_ url: String, query: String) -> Bool {
        let lower = url.lowercased()
        let q     = query.lowercased().replacingOccurrences(of: " ", with: "_")
        // e.g. wikipedia.org/wiki/Youtube when query = "youtube"
        return lower.contains("wikipedia.org/wiki/\(q)") ||
               lower.contains("britannica.com/search?query=\(q)")
    }

    // MARK: - Background discovery

    /// Fetch the landing pages of known sites to build up real index content.
    private func discoverForKnownSites(sites: [KnownSite]) {
        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }
            for site in sites {
                let html = self.fetchHTML(site.url)
                guard !html.isEmpty else { continue }
                let title = extractTitle(html) ?? site.name
                let text  = extractText(html)
                if text.count > 100 {
                    _ = engine.indexDocument(url: site.url, title: title, body: text)
                }
            }
            _ = engine.flush()
        }
    }

    private func discoverForQuery(query: String, candidates: [GrammarCandidate]) {
        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }
            for c in candidates.prefix(2) {
                guard let url = URL(string: c.url) else { continue }
                var req = URLRequest(url: url, timeoutInterval: 8)
                req.setValue("ALIBot/1.0", forHTTPHeaderField: "User-Agent")
                let sem = DispatchSemaphore(value: 0)
                var html = ""
                URLSession.shared.dataTask(with: req) { data, _, _ in
                    if let data { html = String(data: data, encoding: .utf8) ?? "" }
                    sem.signal()
                }.resume()
                sem.wait()
                guard !html.isEmpty else { continue }
                let title = extractTitle(html) ?? c.grammarName
                let text  = extractText(html)
                if text.count > 100 { _ = engine.indexDocument(url: c.url, title: title, body: text) }
            }
            _ = engine.flush()
            let fresh = engine.search(query: query, topK: 15)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                if !fresh.isEmpty {
                    let extra = fresh.map { SearchResultKind.localIndex($0) }
                    // Append without removing known site results
                    var existing = self.searchResultKinds.filter {
                        if case .localIndex = $0 { return false }
                        return true
                    }
                    existing.append(contentsOf: extra)
                    self.searchResultKinds = existing
                }
            }
        }
    }

    // MARK: - Stats

    private func startStatsTimer() {
        statsTimer = Timer.scheduledTimer(withTimeInterval: 8, repeats: true) { [weak self] _ in
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

    // MARK: - Persistence

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
        try? enc.encode(history).write(to: historyURL)
        try? enc.encode(bookmarks).write(to: bookmarksURL)
    }
    func loadPersisted() {
        let dec = JSONDecoder(); dec.dateDecodingStrategy = .iso8601
        if let d = try? Data(contentsOf: historyURL),
           let h = try? dec.decode([HistoryItem].self, from: d) { history = h }
        if let d = try? Data(contentsOf: bookmarksURL),
           let b = try? dec.decode([Bookmark].self, from: d)    { bookmarks = b }
    }
}

// MARK: - HTML extraction

func extractTitle(_ html: String) -> String? {
    if let t = extractTagText(html, tag: "title"), !t.isEmpty { return t }
    if let t = extractTagText(html, tag: "h1"),    !t.isEmpty { return String(t.prefix(120)) }
    return nil
}

private func extractTagText(_ html: String, tag: String) -> String? {
    let lower = html.lowercased()
    guard let ts = lower.range(of: "<\(tag)"),
          let te = lower.range(of: "</\(tag)>") else { return nil }
    guard let gt = html[ts.upperBound...].firstIndex(of: ">") else { return nil }
    let content = String(html[html.index(after: gt)..<te.lowerBound])
    return content.trimmingCharacters(in: .whitespacesAndNewlines)
                  .replacingOccurrences(of: "\n", with: " ")
                  .prefix(200).description
}

func extractText(_ html: String) -> String {
    var paragraphs: [String] = []
    var cursor = html.startIndex
    while cursor < html.endIndex {
        guard let pS = html.range(of: "<p", options: .caseInsensitive, range: cursor..<html.endIndex),
              let gt = html.range(of: ">", range: pS.upperBound..<html.endIndex),
              let pE = html.range(of: "</p>", options: .caseInsensitive, range: gt.upperBound..<html.endIndex)
        else { break }
        let raw   = String(html[gt.upperBound..<pE.lowerBound])
        let clean = stripAllTags(raw)
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .components(separatedBy: .whitespacesAndNewlines)
            .filter { !$0.isEmpty }.joined(separator: " ")
        if clean.count > 40 && clean.contains(" ") { paragraphs.append(clean) }
        cursor = pE.upperBound
    }
    if !paragraphs.isEmpty { return paragraphs.prefix(5).joined(separator: " ") }
    return stripAllTags(removeBoilerplate(html))
        .components(separatedBy: .whitespacesAndNewlines)
        .filter { !$0.isEmpty }.joined(separator: " ")
}

private func stripAllTags(_ s: String) -> String {
    var out = ""; var in_ = false
    for c in s {
        if c == "<" { in_ = true }
        else if c == ">" { in_ = false; out += " " }
        else if !in_ { out.append(c) }
    }
    return out
}

private func removeBoilerplate(_ html: String) -> String {
    var r = html
    for tag in ["script","style","head","nav","footer","header","aside","form"] {
        while let s = r.range(of: "<\(tag)", options: .caseInsensitive),
              let e = r.range(of: "</\(tag)>", options: .caseInsensitive,
                              range: s.lowerBound..<r.endIndex) {
            r.removeSubrange(s.lowerBound...e.upperBound)
        }
    }
    return r
}
