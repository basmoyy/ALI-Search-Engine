// ContentView.swift — ALI browser and search interface.

import SwiftUI
import WebKit

// MARK: - FocusField helper

enum FocusField: Hashable { case addressBar }

struct ContentView: View {
    @StateObject private var model = BrowserModel()
    @State private var addressBarText = ""
    @State private var showSearch    = false
    @State private var showHistory   = false
    @State private var showBookmarks = false
    @State private var showStats     = false
    @FocusState private var focusedField: FocusField?

    var body: some View {
        VStack(spacing: 0) {
            TabBarView(model: model, showSearch: $showSearch, addressBarText: $addressBarText)
            Divider()
            ToolbarView(
                model:          model,
                addressBarText: $addressBarText,
                showSearch:     $showSearch,
                showHistory:    $showHistory,
                showBookmarks:  $showBookmarks,
                showStats:      $showStats,
                focusedField:   $focusedField
            )
            Divider()

            ZStack {
                // Browser always present (keeps WKWebViews alive)
                if let tab = model.activeTab {
                    BrowserPane(tab: tab, model: model)
                        .opacity(showSearch ? 0 : 1)
                        .allowsHitTesting(!showSearch)
                }
                // Search overlay
                if showSearch {
                    SearchResultsView(
                        model:          model,
                        addressBarText: $addressBarText,
                        showSearch:     $showSearch
                    )
                }
                // Seeding banner (bottom right)
                if model.isSeedingIndex {
                    VStack {
                        Spacer()
                        HStack(spacing: 8) {
                            ProgressView()
                            Text(model.seedingStatus)
                                .font(.system(size: 11))
                                .foregroundColor(.secondary)
                            ProgressView(value: model.seedingProgress)
                                .frame(width: 80)
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 8)
                        .background(.regularMaterial)
                        .cornerRadius(8)
                        .padding(12)
                    }
                    .frame(maxWidth: .infinity, alignment: .trailing)
                    .allowsHitTesting(false)
                }
            }
        }
        .sheet(isPresented: $showHistory)   { HistoryView(model: model) }
        .sheet(isPresented: $showBookmarks) { BookmarksView(model: model) }
        .sheet(isPresented: $showStats)     { StatsView(model: model) }
        .frame(minWidth: 960, minHeight: 640)
        .background(Color(NSColor.windowBackgroundColor))
        // ── Sync address bar with active tab ──────────────────────────────────
        .onChange(of: model.activeTabIndex) { _ in
            if !showSearch { syncAddressBar() }
        }
        .onChange(of: model.activeTab?.url) { _ in
            if !showSearch { syncAddressBar() }
        }
        // ── Keyboard shortcuts ────────────────────────────────────────────────
        .onAppear { setupKeyboardShortcuts() }
    }

    private func syncAddressBar() {
        let url = model.activeTab?.url?.absoluteString ?? ""
        if url != "about:blank" && !url.isEmpty { addressBarText = url }
    }

    private func setupKeyboardShortcuts() {
        // NSEvent monitor for shortcuts that SwiftUI .keyboardShortcut can't capture
        // when focus is inside a WKWebView.
        NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [self] event in
            let cmd   = event.modifierFlags.contains(.command)
            let shift = event.modifierFlags.contains(.shift)
            let key   = event.charactersIgnoringModifiers ?? ""

            if cmd {
                switch key {
                case "t":
                    DispatchQueue.main.async { model.newTab(); showSearch = false; addressBarText = "" }
                    return nil
                case "w":
                    DispatchQueue.main.async {
                        if let tab = model.activeTab {
                            model.closedTabURLs.append(contentsOf: [tab.url].compactMap { $0 })
                            model.closeTab(id: tab.id)
                        }
                    }
                    return nil
                case "l":
                    DispatchQueue.main.async { focusedField = .addressBar }
                    return nil
                case "r":
                    DispatchQueue.main.async { model.activeTab?.reload() }
                    return nil
                case "[":
                    DispatchQueue.main.async { model.activeTab?.goBack() }
                    return nil
                case "]":
                    DispatchQueue.main.async { model.activeTab?.goForward() }
                    return nil
                case "T" where shift:
                    DispatchQueue.main.async {
                        if let url = model.closedTabURLs.popLast() { model.newTab(url: url) }
                    }
                    return nil
                default: break
                }
            }
            return event
        }
    }
}

// MARK: - Tab Bar

struct TabBarView: View {
    @ObservedObject var model: BrowserModel
    @Binding var showSearch:     Bool
    @Binding var addressBarText: String

    var body: some View {
        HStack(spacing: 0) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 1) {
                    ForEach(Array(model.tabs.enumerated()), id: \.element.id) { idx, tab in
                        TabCell(tab: tab, isActive: idx == model.activeTabIndex) {
                            withAnimation(.easeInOut(duration: 0.1)) {
                                model.activeTabIndex = idx
                                showSearch = false
                                if let url = tab.url?.absoluteString, url != "about:blank" {
                                    addressBarText = url
                                } else {
                                    addressBarText = ""
                                }
                            }
                        } onClose: {
                            model.closedTabURLs.append(contentsOf: [tab.url].compactMap { $0 })
                            model.closeTab(id: tab.id)
                        }
                    }
                }
                .padding(.horizontal, 6)
            }
            Button {
                model.newTab(); showSearch = false; addressBarText = ""
            } label: {
                Image(systemName: "plus")
                    .font(.system(size: 12, weight: .medium))
                    .frame(width: 30, height: 30)
            }
            .buttonStyle(.plain)
            .padding(.trailing, 6)
            .help("New Tab (⌘T)")
        }
        .frame(height: 36)
        .background(Color(NSColor.windowBackgroundColor))
    }
}

struct TabCell: View {
    @ObservedObject var tab: BrowserTab
    let isActive: Bool
    let onSelect: () -> Void
    let onClose:  () -> Void

    var body: some View {
        HStack(spacing: 5) {
            if tab.isLoading {
                ProgressView().scaleEffect(0.5).frame(width: 14, height: 14)
            } else {
                Image(systemName: "globe")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            }
            Text(tab.title.prefix(26))
                .font(.system(size: 12))
                .lineLimit(1)
                .frame(maxWidth: 165, alignment: .leading)
            Button {
                onClose()
            } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 9, weight: .bold))
                    .frame(width: 16, height: 16)
            }
            .buttonStyle(.plain)
            .opacity(0.5)
        }
        .padding(.horizontal, 8)
        .frame(height: 32)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isActive
                      ? Color(NSColor.selectedContentBackgroundColor).opacity(0.2)
                      : Color.clear)
        )
        .contentShape(Rectangle())
        .onTapGesture { onSelect() }
        .padding(.vertical, 2)
    }
}

// MARK: - Toolbar

struct ToolbarView: View {
    @ObservedObject var model: BrowserModel
    @Binding var addressBarText: String
    @Binding var showSearch:     Bool
    @Binding var showHistory:    Bool
    @Binding var showBookmarks:  Bool
    @Binding var showStats:      Bool
    @FocusState.Binding var focusedField: FocusField?

    var body: some View {
        HStack(spacing: 5) {
            // Back
            Button { model.activeTab?.goBack() } label: {
                Image(systemName: "chevron.left").font(.system(size: 13, weight: .medium))
            }
            .disabled(!(model.activeTab?.canGoBack ?? false))
            .help("Back (⌘[)")

            // Forward
            Button { model.activeTab?.goForward() } label: {
                Image(systemName: "chevron.right").font(.system(size: 13, weight: .medium))
            }
            .disabled(!(model.activeTab?.canGoForward ?? false))
            .help("Forward (⌘])")

            // Reload / Stop
            Button {
                if model.activeTab?.isLoading == true { model.activeTab?.stopLoad() }
                else                                   { model.activeTab?.reload()  }
            } label: {
                Image(systemName: model.activeTab?.isLoading == true
                      ? "xmark" : "arrow.clockwise")
                    .font(.system(size: 13, weight: .medium))
            }
            .help("Reload (⌘R)")

            // Address / search bar
            AddressBar(
                text:         $addressBarText,
                isLoading:    model.activeTab?.isLoading ?? false,
                progress:     model.activeTab?.loadProgress ?? 0,
                focusedField: $focusedField
            ) { committed in
                commitInput(committed)
            }

            // Bookmark
            Button {
                let url   = model.activeTab?.url?.absoluteString ?? ""
                let title = model.activeTab?.title ?? url
                guard !url.isEmpty, url != "about:blank" else { return }
                if let bm = model.bookmarks.first(where: { $0.url == url }) {
                    model.removeBookmark(id: bm.id)
                } else {
                    model.addBookmark(url: url, title: title)
                }
            } label: {
                let url = model.activeTab?.url?.absoluteString ?? ""
                Image(systemName: model.isBookmarked(url: url) ? "star.fill" : "star")
                    .font(.system(size: 13))
                    .foregroundColor(model.isBookmarked(url: url) ? .yellow : .secondary)
            }
            .help("Bookmark")

            Button { showHistory.toggle()   } label: { Image(systemName: "clock").font(.system(size: 13)) }
                .help("History")
            Button { showBookmarks.toggle() } label: { Image(systemName: "bookmark").font(.system(size: 13)) }
                .help("Bookmarks")
            Button { showStats.toggle()     } label: { Image(systemName: "chart.bar").font(.system(size: 13)) }
                .help("Index stats")
        }
        .buttonStyle(.plain)
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
    }

    private func commitInput(_ raw: String) {
        let text = raw.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty else { return }

        if text.hasPrefix("http://") || text.hasPrefix("https://") {
            if let u = URL(string: text) {
                showSearch = false; addressBarText = text; model.navigate(to: u)
            }
            return
        }
        // Domain heuristic: single token, contains dot, no spaces
        if !text.contains(" "), text.contains("."),
           !text.hasPrefix("."), !text.hasSuffix("."),
           text.components(separatedBy: ".").allSatisfy({ !$0.isEmpty }) {
            let urlStr = "https://\(text)"
            if let u = URL(string: urlStr) {
                showSearch = false; addressBarText = urlStr; model.navigate(to: u)
                return
            }
        }
        // Everything else → search
        showSearch = true; addressBarText = text; model.performSearch(query: text)
    }
}

// MARK: - Address Bar

struct AddressBar: View {
    @Binding var text:  String
    let isLoading:      Bool
    let progress:       Double
    @FocusState.Binding var focusedField: FocusField?
    let onSubmit:       (String) -> Void

    var body: some View {
        ZStack(alignment: .leading) {
            if isLoading && progress > 0 && progress < 1 {
                GeometryReader { geo in
                    Rectangle()
                        .fill(Color.accentColor.opacity(0.12))
                        .frame(width: geo.size.width * progress)
                        .animation(.easeInOut(duration: 0.25), value: progress)
                }
                .cornerRadius(6)
            }
            TextField("Search or enter address", text: $text)
                .textFieldStyle(.plain)
                .font(.system(size: 13))
                .padding(.horizontal, 10)
                .focused($focusedField, equals: .addressBar)
                .onSubmit { onSubmit(text) }
                .onExitCommand {
                    focusedField = nil
                }
        }
        .frame(height: 28)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(NSColor.textBackgroundColor))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(focusedField == .addressBar
                                ? Color.accentColor : Color.gray.opacity(0.3),
                                lineWidth: focusedField == .addressBar ? 1.5 : 0.5)
                )
        )
    }
}

// MARK: - Browser Pane

struct BrowserPane: View {
    @ObservedObject var tab:   BrowserTab
    @ObservedObject var model: BrowserModel

    var body: some View {
        ZStack(alignment: .top) {
            BrowserContentView(tab: tab, model: model)
                .ignoresSafeArea()
            if tab.isLoading && tab.loadProgress > 0 && tab.loadProgress < 1 {
                ProgressView(value: tab.loadProgress)
                    .progressViewStyle(.linear)
                    .frame(height: 2)
            }
        }
    }
}

// MARK: - Search Results

struct SearchResultsView: View {
    @ObservedObject var model: BrowserModel
    @Binding var addressBarText: String
    @Binding var showSearch: Bool

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                // Seeding in-progress notice
                if model.isSeedingIndex && model.searchResultKinds.isEmpty {
                    HStack(spacing: 10) {
                        ProgressView().scaleEffect(0.7)
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Building index in background…")
                                .font(.system(size: 13))
                                .foregroundColor(.secondary)
                            Text("Results will improve as more pages are indexed.")
                                .font(.system(size: 11))
                                .foregroundColor(.secondary.opacity(0.7))
                        }
                    }
                    .padding(16)
                }

                if model.isSearching && model.searchResultKinds.isEmpty {
                    HStack(spacing: 10) {
                        ProgressView().scaleEffect(0.7)
                        Text("Searching local index…")
                            .font(.system(size: 13)).foregroundColor(.secondary)
                    }
                    .padding(16)
                }

                ForEach(Array(model.searchResultKinds.enumerated()), id: \.offset) { _, kind in
                    switch kind {
                    case .knownSite(let site):
                        KnownSiteRow(site: site) { nav(site.url) }
                    case .localIndex(let r):
                        LocalIndexRow(result: r) { nav(r.url) }
                    case .grammarDiscovery(let g):
                        GrammarRow(candidate: g) { nav(g.url) }
                    }
                }

                if !model.isSearching && model.searchResultKinds.isEmpty {
                    emptyState
                }
            }
            .padding(.horizontal, 24)
            .padding(.vertical, 16)
        }
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var emptyState: some View {
        VStack(spacing: 14) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 36)).foregroundColor(.secondary.opacity(0.4))
            Text("No results for \"\(model.searchQuery)\"")
                .font(.system(size: 15, weight: .medium))
                .foregroundColor(.secondary)
            Text(model.isSeedingIndex
                 ? "Index is still building. Try again in a moment."
                 : "ALI is discovering relevant pages in the background.")
                .font(.system(size: 12)).foregroundColor(.secondary.opacity(0.7))
        }
        .frame(maxWidth: .infinity).padding(60)
    }

    private func nav(_ urlStr: String) {
        guard let u = URL(string: urlStr) else { return }
        showSearch = false; addressBarText = urlStr; model.navigate(to: u)
    }
}

// MARK: - Result rows

struct KnownSiteRow: View {
    let site:   KnownSite
    let onOpen: () -> Void

    var body: some View {
        Button(action: onOpen) {
            HStack(spacing: 14) {
                ZStack {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(catColor(site.category).opacity(0.15))
                        .frame(width: 40, height: 40)
                    Text(String(site.name.prefix(2)).uppercased())
                        .font(.system(size: 14, weight: .bold))
                        .foregroundColor(catColor(site.category))
                }
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Text(site.name)
                            .font(.system(size: 15, weight: .semibold))
                            .foregroundColor(.primary)
                        Text(site.category.uppercased())
                            .font(.system(size: 9, weight: .bold))
                            .foregroundColor(.secondary)
                            .padding(.horizontal, 5).padding(.vertical, 1)
                            .background(Color.secondary.opacity(0.1))
                            .cornerRadius(3)
                    }
                    Text(site.url)
                        .font(.system(size: 12)).foregroundColor(.accentColor)
                    Text(site.description)
                        .font(.system(size: 12)).foregroundColor(.secondary)
                        .lineLimit(1)
                }
                Spacer()
                Image(systemName: "arrow.up.right")
                    .font(.system(size: 12)).foregroundColor(.secondary)
            }
            .padding(.vertical, 10).padding(.horizontal, 10).contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .background(Color(NSColor.controlBackgroundColor).opacity(0.6))
        .cornerRadius(10)
        .padding(.vertical, 3)
    }

    private func catColor(_ c: String) -> Color {
        switch c {
        case "social":     return .blue
        case "video":      return .red
        case "news":       return .orange
        case "tech":       return .green
        case "reference":  return .purple
        case "science":    return .cyan
        case "education":  return .indigo
        case "health":     return .pink
        case "entertainment": return Color(red: 0.9, green: 0.7, blue: 0)
        case "travel":     return .mint
        case "shopping":   return Color(red: 0.8, green: 0.4, blue: 0)
        default:           return .gray
        }
    }
}

struct LocalIndexRow: View {
    let result:  SearchResult
    let onOpen:  () -> Void

    var body: some View {
        Button(action: onOpen) {
            VStack(alignment: .leading, spacing: 4) {
                Text(result.title.isEmpty ? result.url : result.title)
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(.accentColor)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text(result.url)
                    .font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
                if !result.excerpt.isEmpty {
                    Text(result.excerpt)
                        .font(.system(size: 12))
                        .foregroundColor(.primary.opacity(0.8))
                        .lineLimit(3)
                }
            }
            .padding(.vertical, 10).contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .overlay(Divider().opacity(0.2), alignment: .bottom)
    }
}

struct GrammarRow: View {
    let candidate: GrammarCandidate
    let onOpen:    () -> Void

    var body: some View {
        Button(action: onOpen) {
            HStack(spacing: 10) {
                Image(systemName: "doc.text.magnifyingglass")
                    .font(.system(size: 13)).foregroundColor(.secondary)
                VStack(alignment: .leading, spacing: 2) {
                    Text(candidate.grammarName)
                        .font(.system(size: 13, weight: .medium)).foregroundColor(.primary)
                    Text(candidate.url)
                        .font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
                }
                Spacer()
                Text("source")
                    .font(.system(size: 10)).foregroundColor(.secondary)
                    .padding(.horizontal, 5).padding(.vertical, 2)
                    .background(Color.secondary.opacity(0.1)).cornerRadius(4)
            }
            .padding(.vertical, 7).contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .overlay(Divider().opacity(0.15), alignment: .bottom)
    }
}

// MARK: - History

struct HistoryView: View {
    @ObservedObject var model: BrowserModel
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationView {
            List(model.history.prefix(300)) { item in
                VStack(alignment: .leading, spacing: 2) {
                    Text(item.title.prefix(80))
                        .font(.system(size: 13, weight: .medium)).lineLimit(1)
                    Text(item.url)
                        .font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
                    Text(item.visitedAt.formatted(date: .abbreviated, time: .shortened))
                        .font(.system(size: 10)).foregroundColor(.secondary.opacity(0.6))
                }
                .padding(.vertical, 2)
            }
            .navigationTitle("History")
            .toolbar {
                ToolbarItem(placement: .primaryAction) { Button("Done") { dismiss() } }
                ToolbarItem(placement: .destructiveAction) {
                    Button("Clear") { model.history = []; model.savePersisted() }
                }
            }
        }
        .frame(width: 540, height: 520)
    }
}

// MARK: - Bookmarks

struct BookmarksView: View {
    @ObservedObject var model: BrowserModel
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationView {
            List {
                ForEach(model.bookmarks) { bm in
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(bm.title.prefix(60))
                                .font(.system(size: 13, weight: .medium)).lineLimit(1)
                            Text(bm.url)
                                .font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
                        }
                        Spacer()
                        Button("Open") {
                            if let u = URL(string: bm.url) { model.navigate(to: u) }
                            dismiss()
                        }
                        .buttonStyle(.bordered).controlSize(.small)
                    }
                    .padding(.vertical, 2)
                }
                .onDelete { idx in idx.forEach { model.removeBookmark(id: model.bookmarks[$0].id) } }
            }
            .navigationTitle("Bookmarks")
            .toolbar { ToolbarItem(placement: .primaryAction) { Button("Done") { dismiss() } } }
        }
        .frame(width: 540, height: 420)
    }
}

// MARK: - Stats

struct StatsView: View {
    @ObservedObject var model: BrowserModel
    @Environment(\.dismiss) var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("ALI Engine").font(.title2.bold())

            if model.isSeedingIndex {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        ProgressView()
                        Text(model.seedingStatus).font(.system(size: 12))
                    }
                    ProgressView(value: model.seedingProgress)
                }
                .padding(10)
                .background(Color.accentColor.opacity(0.05))
                .cornerRadius(8)
            }

            Grid(alignment: .leading, horizontalSpacing: 32, verticalSpacing: 12) {
                GridRow {
                    statCell("Documents", "\(model.stats.docCount)")
                    statCell("Segments",  "\(model.stats.segmentCount)")
                }
                GridRow {
                    statCell("Index size", ByteCountFormatter.string(
                        fromByteCount: Int64(model.stats.indexBytes), countStyle: .file))
                    statCell("Sources", "\(model.stats.sourceCount)")
                }
                GridRow {
                    statCell("Nav edges",   "\(model.stats.navEdgeCount)")
                    statCell("Grammars",    "\(model.stats.grammarCount)")
                }
                GridRow {
                    statCell("History",   "\(model.history.count)")
                    statCell("Bookmarks", "\(model.bookmarks.count)")
                }
            }
            Divider()
            HStack {
                Button("Refresh") { model.refreshStats() }
                Spacer()
                Button("Done") { dismiss() }.buttonStyle(.borderedProminent)
            }
        }
        .padding(24)
        .frame(width: 380)
    }

    private func statCell(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.system(size: 11)).foregroundColor(.secondary)
            Text(value).font(.system(size: 15, weight: .semibold)).monospacedDigit()
        }
    }
}
