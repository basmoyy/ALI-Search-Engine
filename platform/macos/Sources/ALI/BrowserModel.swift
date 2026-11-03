// BrowserModel.swift — Central ViewModel for ALI browser and search.

import Foundation
import Combine

nonisolated(unsafe) private var _unused = false

// MARK: - Tab

final class BrowserTab: ObservableObject, Identifiable {
    let id = UUID()
    @Published var url:         URL?
    @Published var title:       String = "New Tab"
    @Published var isLoading    = false
    @Published var canGoBack    = false
    @Published var canGoForward = false
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

enum SearchResultKind {
    case knownSite(KnownSite)
    case localIndex(SearchResult)
    case grammarDiscovery(GrammarCandidate)
}

// MARK: - BrowserModel

@MainActor
final class BrowserModel: ObservableObject {

    @Published var tabs:           [BrowserTab] = []
    @Published var activeTabIndex: Int = 0

    var activeTab: BrowserTab? {
        guard tabs.indices.contains(activeTabIndex) else { return nil }
        return tabs[activeTabIndex]
    }

    @Published var searchQuery        = ""
    @Published var isSearching        = false
    @Published var searchResultKinds: [SearchResultKind] = []

    @Published var history:   [HistoryItem] = []
    @Published var bookmarks: [Bookmark]    = []
    @Published var stats      = EngineStats()

    @Published var isSeedingIndex   = false
    @Published var seedingProgress: Double = 0
    @Published var seedingStatus    = ""

    private(set) var engine: AliEngine?
    private let bgQueue  = DispatchQueue(label: "ali.engine",    qos: .userInitiated)
    private let seedQueue = DispatchQueue(label: "ali.seeding",  qos: .background)
    private var statsTimer: Timer?

    // MARK: - Lifecycle

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

        // Seed on first launch (or after index wipe)
        let flag = aliDir.appendingPathComponent(".seeded_v3")
        if !FileManager.default.fileExists(atPath: flag.path) {
            seedIndexInBackground(flagPath: flag)
        }
    }

    // MARK: - Seed list
    // Uses the Wikipedia REST summary API which returns clean plain text —
    // no raw MediaWiki markup, no citation templates, no HTML boilerplate.
    // Format: https://en.wikipedia.org/api/rest_v1/page/summary/{topic}

    private struct SeedEntry {
        let topic:    String   // Wikipedia article slug
        let category: String   // for tagging
    }

    private let seedTopics: [SeedEntry] = [
        // Countries — most commonly searched
        .init(topic: "India",         category: "country"),
        .init(topic: "Japan",         category: "country"),
        .init(topic: "United_States", category: "country"),
        .init(topic: "United_Kingdom", category: "country"),
        .init(topic: "China",         category: "country"),
        .init(topic: "France",        category: "country"),
        .init(topic: "Germany",       category: "country"),
        .init(topic: "Brazil",        category: "country"),
        .init(topic: "Australia",     category: "country"),
        .init(topic: "Canada",        category: "country"),
        .init(topic: "Russia",        category: "country"),
        .init(topic: "South_Korea",   category: "country"),
        .init(topic: "Italy",         category: "country"),
        .init(topic: "Mexico",        category: "country"),
        // Major websites / services
        .init(topic: "YouTube",            category: "website"),
        .init(topic: "Reddit",             category: "website"),
        .init(topic: "Discord_(software)", category: "website"),
        .init(topic: "GitHub",             category: "website"),
        .init(topic: "Netflix",            category: "website"),
        .init(topic: "Wikipedia",          category: "website"),
        .init(topic: "Amazon_(company)",   category: "website"),
        .init(topic: "Twitter",            category: "website"),
        .init(topic: "Instagram",          category: "website"),
        .init(topic: "TikTok",             category: "website"),
        .init(topic: "Twitch_(service)",   category: "website"),
        .init(topic: "Spotify",            category: "website"),
        .init(topic: "LinkedIn",           category: "website"),
        // Technology
        .init(topic: "Python_(programming_language)", category: "tech"),
        .init(topic: "Rust_(programming_language)",   category: "tech"),
        .init(topic: "JavaScript",         category: "tech"),
        .init(topic: "Artificial_intelligence", category: "tech"),
        .init(topic: "Machine_learning",   category: "tech"),
        .init(topic: "Internet",           category: "tech"),
        .init(topic: "Search_engine",      category: "tech"),
        .init(topic: "Web_browser",        category: "tech"),
        .init(topic: "Smartphone",         category: "tech"),
        .init(topic: "Open-source_software", category: "tech"),
        // Science
        .init(topic: "NASA",               category: "science"),
        .init(topic: "Climate_change",     category: "science"),
        .init(topic: "Evolution",          category: "science"),
        .init(topic: "Quantum_mechanics",  category: "science"),
        .init(topic: "Black_hole",         category: "science"),
        .init(topic: "Human_body",         category: "science"),
        // Sports & activity
        .init(topic: "Running",            category: "sport"),
        .init(topic: "Football",           category: "sport"),
        .init(topic: "Cricket",            category: "sport"),
        .init(topic: "Olympic_Games",      category: "sport"),
        // Culture & history
        .init(topic: "World_War_II",       category: "history"),
        .init(topic: "Renaissance",        category: "history"),
        .init(topic: "Buddhism",           category: "culture"),
        .init(topic: "Islam",              category: "culture"),
        .init(topic: "Music",              category: "culture"),
        .init(topic: "Film",               category: "culture"),
        // Economy
        .init(topic: "Economy",            category: "economy"),
        .init(topic: "Stock_market",       category: "economy"),
        .init(topic: "Cryptocurrency",     category: "economy"),
    ]

    private func seedIndexInBackground(flagPath: URL) {
        isSeedingIndex  = true
        seedingProgress = 0
        seedingStatus   = "Starting…"

        let topics = seedTopics
        seedQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }
            var indexed = 0
            let total   = Double(topics.count)

            for (i, entry) in topics.enumerated() {
                let progress = Double(i) / total
                DispatchQueue.main.async { [weak self] in
                    self?.seedingProgress = progress
                    self?.seedingStatus   = "Indexing \(entry.topic.replacingOccurrences(of: "_", with: " "))…"
                }

                // Use Wikipedia REST summary API — returns clean plain text
                guard let (url, title, text) = self.fetchWikiSummary(entry.topic) else { continue }
                if text.count > 50 {
                    _ = engine.indexDocument(url: url, title: title, body: text)
                    indexed += 1
                }
                // Also fetch the full intro section for richer indexing
                if let fullText = self.fetchWikiIntro(entry.topic), fullText.count > text.count {
                    _ = engine.indexDocument(url: url + "#full", title: title, body: fullText)
                }
            }
            _ = engine.flush()

            try? "seeded".write(to: flagPath, atomically: true, encoding: .utf8)
            DispatchQueue.main.async { [weak self] in
                self?.isSeedingIndex   = false
                self?.seedingProgress  = 1.0
                self?.seedingStatus    = "Ready — \(indexed) articles indexed"
            }
        }
    }

    // Wikipedia REST summary endpoint — clean plain text, no markup
    private func fetchWikiSummary(_ topic: String) -> (url: String, title: String, text: String)? {
        let encoded = topic.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? topic
        let apiURL  = "https://en.wikipedia.org/api/rest_v1/page/summary/\(encoded)"
        guard let url = URL(string: apiURL) else { return nil }
        var req = URLRequest(url: url, timeoutInterval: 8)
        req.setValue("ALIBot/1.0 (local indexer)", forHTTPHeaderField: "User-Agent")
        req.setValue("application/json", forHTTPHeaderField: "Accept")

        var result: (url: String, title: String, text: String)? = nil
        let sem = DispatchSemaphore(value: 0)
        URLSession.shared.dataTask(with: req) { data, _, _ in
            defer { sem.signal() }
            guard let data,
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let extract = json["extract"] as? String,
                  let title   = json["title"]   as? String,
                  let pageURL = (json["content_urls"] as? [String: Any])?["desktop"] as? [String: Any],
                  let pageStr = pageURL["page"]  as? String
            else { return }
            result = (url: pageStr, title: title, text: extract)
        }.resume()
        sem.wait()
        return result
    }

    // Wikipedia REST mobile-sections — fuller introduction paragraph
    private func fetchWikiIntro(_ topic: String) -> String? {
        let encoded = topic.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? topic
        let apiURL  = "https://en.wikipedia.org/api/rest_v1/page/mobile-sections-lead/\(encoded)"
        guard let url = URL(string: apiURL) else { return nil }
        var req = URLRequest(url: url, timeoutInterval: 8)
        req.setValue("ALIBot/1.0", forHTTPHeaderField: "User-Agent")
        req.setValue("application/json", forHTTPHeaderField: "Accept")

        var text: String? = nil
        let sem = DispatchSemaphore(value: 0)
        URLSession.shared.dataTask(with: req) { data, _, _ in
            defer { sem.signal() }
            guard let data,
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let sections = json["sections"] as? [[String: Any]],
                  let first = sections.first,
                  let html = first["text"] as? String
            else { return }
            text = extractText(html)
        }.resume()
        sem.wait()
        return text
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
        WebViewPool.shared.remove(tabs[idx].id)
        tabs.remove(at: idx)
        if tabs.isEmpty { newTab() }
        activeTabIndex = min(activeTabIndex, tabs.count - 1)
    }

    func selectTab(_ tab: BrowserTab) {
        if let idx = tabs.firstIndex(where: { $0.id == tab.id }) {
            activeTabIndex = idx
        }
    }

    var closedTabURLs: [URL] = []   // for ⌘⇧T

    // MARK: - Navigation

    func navigate(to url: URL) {
        guard let tab = activeTab else { newTab(url: url); return }
        tab.load(url)
    }

    // MARK: - History

    func recordVisit(url: String, title: String) {
        guard url != "about:blank", !url.isEmpty else { return }
        history.insert(HistoryItem(url: url, title: title), at: 0)
        if history.count > 2000 { history = Array(history.prefix(2000)) }
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

        // Known sites — immediate, no I/O
        let sites       = KnownSites.match(query: q)
        let siteResults = sites.prefix(2).map { SearchResultKind.knownSite($0) }

        bgQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }

            // BM25 search on local index
            let local = engine.search(query: q, topK: 20)

            // Grammar only for multi-word informational queries
            let isMultiWord = q.split(separator: " ").count >= 2
            let grammar: [GrammarCandidate] = isMultiWord
                ? engine.grammarConstruct(query: q, maxResults: 3)
                : []

            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                var results: [SearchResultKind] = Array(siteResults)

                let siteURLSet = Set(sites.map { $0.url.lowercased() })
                for r in local {
                    // Filter out pure navigation pages that duplicate known site cards
                    if !siteURLSet.isEmpty,
                       isNavigationPageFor(r.url, query: q) { continue }
                    results.append(.localIndex(r))
                }
                for g in grammar { results.append(.grammarDiscovery(g)) }

                self.searchResultKinds = results
                self.isSearching       = false

                // Trigger background discovery if index is sparse for this query
                if local.isEmpty {
                    self.discoverForQuery(q, sites: Array(sites.prefix(1)))
                }
            }
        }
    }

    private func isNavigationPageFor(_ url: String, query: String) -> Bool {
        let lower = url.lowercased()
        let q     = query.lowercased().trimmingCharacters(in: .whitespaces)
        // Suppress e.g. en.wikipedia.org/wiki/Discord when KnownSites already shows Discord
        let slug  = q.replacingOccurrences(of: " ", with: "_")
        return lower.contains("wikipedia.org/wiki/\(slug)") ||
               lower.contains("britannica.com/search?query=\(q.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? q)")
    }

    // MARK: - Background discovery

    private func discoverForQuery(_ query: String, sites: [KnownSite]) {
        seedQueue.async { [weak self] in
            guard let self, let engine = self.engine else { return }

            // Try to fetch from a known site if available
            for site in sites {
                guard let url = URL(string: site.url) else { continue }
                var req = URLRequest(url: url, timeoutInterval: 8)
                req.setValue("ALIBot/1.0", forHTTPHeaderField: "User-Agent")
                let sem = DispatchSemaphore(value: 0)
                var html = ""
                URLSession.shared.dataTask(with: req) { data, _, _ in
                    if let data { html = String(data: data, encoding: .utf8) ?? "" }
                    sem.signal()
                }.resume()
                sem.wait()
                if !html.isEmpty {
                    let title = extractTitleFunc(html) ?? site.name
                    let text  = extractText(html)
                    if text.count > 100 {
                        _ = engine.indexDocument(url: site.url, title: title, body: text)
                    }
                }
            }

            // Also try the Wikipedia summary for the query topic
            if let (url, title, text) = self.fetchWikiSummary(
                    query.replacingOccurrences(of: " ", with: "_")) {
                _ = engine.indexDocument(url: url, title: title, body: text)
            }

            _ = engine.flush()

            let fresh = engine.search(query: query, topK: 15)
            DispatchQueue.main.async { [weak self] in
                guard let self, !fresh.isEmpty else { return }
                var updated = self.searchResultKinds.filter {
                    if case .localIndex = $0 { return false }; return true
                }
                updated.append(contentsOf: fresh.map { .localIndex($0) })
                self.searchResultKinds = updated
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

// MARK: - HTML extraction helpers (module-level, reusable)

func extractTitleFunc(_ html: String) -> String? {
    let lower = html.lowercased()
    if let ts = lower.range(of: "<title"),
       let te = lower.range(of: "</title>"),
       let gt = html[ts.upperBound...].firstIndex(of: ">") {
        let s = String(html[html.index(after: gt)..<te.lowerBound])
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if !s.isEmpty { return String(s.prefix(200)) }
    }
    return nil
}

func extractText(_ html: String) -> String {
    var r = html
    for tag in ["script","style","head","nav","footer","header","aside","form"] {
        while let s = r.range(of: "<\(tag)", options: .caseInsensitive),
              let e = r.range(of: "</\(tag)>", options: .caseInsensitive,
                              range: s.lowerBound..<r.endIndex) {
            r.removeSubrange(s.lowerBound...e.upperBound)
        }
    }
    // Prefer <p> content
    var paras: [String] = []
    var cur = r.startIndex
    while cur < r.endIndex,
          let pS = r.range(of: "<p", options: .caseInsensitive, range: cur..<r.endIndex),
          let gt = r.range(of: ">", range: pS.upperBound..<r.endIndex),
          let pE = r.range(of: "</p>", options: .caseInsensitive, range: gt.upperBound..<r.endIndex) {
        let raw   = String(r[gt.upperBound..<pE.lowerBound])
        let clean = stripTags(raw)
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .components(separatedBy: .whitespacesAndNewlines)
            .filter { !$0.isEmpty }.joined(separator: " ")
        if clean.count > 40 { paras.append(clean) }
        cur = pE.upperBound
    }
    if !paras.isEmpty { return paras.prefix(6).joined(separator: " ") }
    return stripTags(r).components(separatedBy: .whitespacesAndNewlines)
                       .filter { !$0.isEmpty }.joined(separator: " ")
}

private func stripTags(_ s: String) -> String {
    var out = ""; var in_ = false
    for c in s {
        if c == "<" { in_ = true }
        else if c == ">" { in_ = false; out += " " }
        else if !in_ { out.append(c) }
    }
    return out
}
