// ContentView.swift — ALI browser and search interface.

import SwiftUI

struct ContentView: View {
    @StateObject private var model = BrowserModel()
    @State private var addressBarText = ""
    @State private var showSearch    = false
    @State private var showHistory   = false
    @State private var showBookmarks = false
    @State private var showStats     = false

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
                showStats:      $showStats
            )
            Divider()
            ZStack {
                // Browser view — always present, hidden when search is shown
                if let tab = model.activeTab {
                    BrowserPane(tab: tab, model: model)
                        .opacity(showSearch ? 0 : 1)
                        // Keep it in the view hierarchy even during search
                        // so the WKWebView doesn't get deallocated
                }
                // Search results overlay
                if showSearch {
                    SearchResultsView(
                        model:          model,
                        addressBarText: $addressBarText,
                        showSearch:     $showSearch
                    )
                }
                // Seeding progress banner
                if model.isSeedingIndex {
                    VStack {
                        Spacer()
                        HStack(spacing: 10) {
                            ProgressView()
                            Text("Building initial index… \(Int(model.seedingProgress * 100))%")
                                .font(.system(size: 12))
                                .foregroundColor(.secondary)
                        }
                        .padding(10)
                        .background(.regularMaterial)
                        .cornerRadius(8)
                        .padding(.bottom, 12)
                    }
                }
            }
        }
        .sheet(isPresented: $showHistory)   { HistoryView(model: model) }
        .sheet(isPresented: $showBookmarks) { BookmarksView(model: model) }
        .sheet(isPresented: $showStats)     { StatsView(model: model) }
        .frame(minWidth: 960, minHeight: 640)
        .background(Color(NSColor.windowBackgroundColor))
        .onChange(of: model.activeTabIndex) { _ in
            if !showSearch { syncAddressBar() }
        }
        .onChange(of: model.activeTab?.url) { _ in
            if !showSearch { syncAddressBar() }
        }
    }

    private func syncAddressBar() {
        let url = model.activeTab?.url?.absoluteString ?? ""
        if url != "about:blank" { addressBarText = url }
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
                        TabCell(
                            tab:      tab,
                            isActive: idx == model.activeTabIndex
                        ) {
                            // Select tab and leave search mode
                            model.activeTabIndex = idx
                            showSearch = false
                            if let url = tab.url?.absoluteString, url != "about:blank" {
                                addressBarText = url
                            }
                        } onClose: {
                            model.closeTab(id: tab.id)
                        }
                    }
                }
                .padding(.horizontal, 6)
            }
            // New tab button
            Button {
                model.newTab()
                showSearch     = false
                addressBarText = ""
            } label: {
                Image(systemName: "plus")
                    .font(.system(size: 12, weight: .medium))
                    .frame(width: 30, height: 30)
            }
            .buttonStyle(.plain)
            .padding(.trailing, 6)
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
                ProgressView().scaleEffect(0.5).frame(width: 13, height: 13)
            } else {
                Image(systemName: "globe")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            }
            Text(tab.title.prefix(24))
                .font(.system(size: 12))
                .lineLimit(1)
                .frame(maxWidth: 160, alignment: .leading)
            Button {
                onClose()
            } label: {
                Image(systemName: "xmark").font(.system(size: 9, weight: .bold))
                    .frame(width: 14, height: 14)
            }
            .buttonStyle(.plain)
            .opacity(0.5)
        }
        .padding(.horizontal, 8)
        .frame(height: 32)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isActive
                    ? Color(NSColor.selectedContentBackgroundColor).opacity(0.18)
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

    var body: some View {
        HStack(spacing: 5) {
            // ── Nav buttons ───────────────────────────────────────────────────
            Button { model.activeTab?.goBack() } label: {
                Image(systemName: "chevron.left").font(.system(size: 13, weight: .medium))
            }
            .disabled(!(model.activeTab?.canGoBack ?? false))
            .keyboardShortcut("[", modifiers: .command)

            Button { model.activeTab?.goForward() } label: {
                Image(systemName: "chevron.right").font(.system(size: 13, weight: .medium))
            }
            .disabled(!(model.activeTab?.canGoForward ?? false))
            .keyboardShortcut("]", modifiers: .command)

            Button {
                if model.activeTab?.isLoading == true {
                    model.activeTab?.stopLoad()
                } else {
                    model.activeTab?.reload()
                }
            } label: {
                Image(systemName: model.activeTab?.isLoading == true
                      ? "xmark" : "arrow.clockwise")
                    .font(.system(size: 13, weight: .medium))
            }
            .keyboardShortcut("r", modifiers: .command)

            // ── Address / search bar ──────────────────────────────────────────
            AddressBar(
                text:      $addressBarText,
                isLoading: model.activeTab?.isLoading ?? false,
                progress:  model.activeTab?.loadProgress ?? 0
            ) { committed in
                commitInput(committed)
            }

            // ── Bookmark ──────────────────────────────────────────────────────
            Button {
                let url   = model.activeTab?.url?.absoluteString ?? ""
                let title = model.activeTab?.title ?? ""
                guard !url.isEmpty else { return }
                if model.isBookmarked(url: url) {
                    if let bm = model.bookmarks.first(where: { $0.url == url }) {
                        model.removeBookmark(id: bm.id)
                    }
                } else {
                    model.addBookmark(url: url, title: title)
                }
            } label: {
                let url = model.activeTab?.url?.absoluteString ?? ""
                Image(systemName: model.isBookmarked(url: url) ? "star.fill" : "star")
                    .font(.system(size: 13))
                    .foregroundColor(model.isBookmarked(url: url) ? .yellow : .secondary)
            }

            Button { showHistory.toggle()   } label: { Image(systemName: "clock").font(.system(size: 13)) }
            Button { showBookmarks.toggle() } label: { Image(systemName: "bookmark").font(.system(size: 13)) }
            Button { showStats.toggle()     } label: { Image(systemName: "chart.bar").font(.system(size: 13)) }
        }
        .buttonStyle(.plain)
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
    }

    private func commitInput(_ raw: String) {
        let text = raw.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty else { return }

        // Explicit URL
        if text.hasPrefix("http://") || text.hasPrefix("https://") {
            if let u = URL(string: text) {
                showSearch = false
                addressBarText = text
                model.navigate(to: u)
            }
            return
        }
        // Domain name (single token with a dot, no spaces)
        if !text.contains(" "),
           text.contains("."),
           !text.hasPrefix("."),
           !text.hasSuffix(".") {
            let urlStr = "https://\(text)"
            if let u = URL(string: urlStr) {
                showSearch = false
                addressBarText = urlStr
                model.navigate(to: u)
            }
            return
        }
        // Everything else → search
        showSearch = true
        addressBarText = text
        model.performSearch(query: text)
    }
}

// MARK: - Address Bar

struct AddressBar: View {
    @Binding var text:  String
    let isLoading:      Bool
    let progress:       Double
    let onSubmit:       (String) -> Void
    @FocusState private var focused: Bool

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
                .focused($focused)
                .onSubmit { onSubmit(text) }
                .onExitCommand { focused = false }
        }
        .frame(height: 28)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(NSColor.textBackgroundColor))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(focused ? Color.accentColor : Color.gray.opacity(0.3),
                                lineWidth: focused ? 1.5 : 0.5)
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
            LazyVStack(alignment: .leading, spacing: 0, pinnedViews: []) {
                if model.isSearching && model.searchResultKinds.isEmpty {
                    HStack(spacing: 10) {
                        ProgressView().scaleEffect(0.7)
                        Text("Searching…").font(.system(size: 13)).foregroundColor(.secondary)
                    }
                    .padding(16)
                }

                if !model.searchResultKinds.isEmpty {
                    ForEach(Array(model.searchResultKinds.enumerated()), id: \.offset) { _, kind in
                        switch kind {
                        case .knownSite(let site):
                            KnownSiteRow(site: site) { navigate(to: site.url) }
                        case .localIndex(let r):
                            LocalIndexRow(result: r) { navigate(to: r.url) }
                        case .grammarDiscovery(let g):
                            GrammarRow(candidate: g) { navigate(to: g.url) }
                        }
                    }
                } else if !model.isSearching {
                    emptyState
                }
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 12)
        }
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 32))
                .foregroundColor(.secondary.opacity(0.5))
            Text("No results for \"\(model.searchQuery)\"")
                .font(.system(size: 14))
                .foregroundColor(.secondary)
            if model.isSeedingIndex {
                Text("Index is still building — try again in a moment.")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary.opacity(0.7))
            } else {
                Text("ALI is discovering relevant pages.")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary.opacity(0.7))
            }
        }
        .frame(maxWidth: .infinity)
        .padding(48)
    }

    private func navigate(to urlString: String) {
        guard let u = URL(string: urlString) else { return }
        showSearch     = false
        addressBarText = urlString
        model.navigate(to: u)
    }
}

// MARK: - Result rows

/// Known site result — large, prominent, shows official URL and description.
struct KnownSiteRow: View {
    let site:   KnownSite
    let onOpen: () -> Void

    var body: some View {
        Button(action: onOpen) {
            HStack(spacing: 14) {
                // Favicon placeholder coloured by category
                RoundedRectangle(cornerRadius: 6)
                    .fill(categoryColor(site.category).opacity(0.15))
                    .frame(width: 36, height: 36)
                    .overlay(
                        Text(String(site.name.prefix(2)).uppercased())
                            .font(.system(size: 13, weight: .bold))
                            .foregroundColor(categoryColor(site.category))
                    )

                VStack(alignment: .leading, spacing: 3) {
                    Text(site.name)
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundColor(.primary)
                    Text(site.url)
                        .font(.system(size: 12))
                        .foregroundColor(.accentColor)
                    Text(site.description)
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                }
                Spacer()
                Image(systemName: "arrow.up.right.square")
                    .font(.system(size: 14))
                    .foregroundColor(.secondary)
            }
            .padding(.vertical, 12)
            .padding(.horizontal, 8)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .background(Color(NSColor.controlBackgroundColor).opacity(0.5))
        .cornerRadius(8)
        .padding(.vertical, 3)
    }

    private func categoryColor(_ cat: String) -> Color {
        switch cat {
        case "social":        return .blue
        case "video":         return .red
        case "news":          return .orange
        case "tech":          return .green
        case "reference":     return .purple
        case "science":       return .cyan
        case "education":     return .indigo
        case "health":        return .pink
        case "entertainment": return .yellow
        case "travel":        return .mint
        default:              return .gray
        }
    }
}

/// BM25 local index result — compact, shows title, URL, excerpt.
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
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                if !result.excerpt.isEmpty {
                    Text(result.excerpt)
                        .font(.system(size: 12))
                        .foregroundColor(.primary.opacity(0.8))
                        .lineLimit(3)
                }
            }
            .padding(.vertical, 10)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .overlay(Divider().opacity(0.25), alignment: .bottom)
    }
}

/// Grammar discovery row — smaller, secondary, shown only for informational queries.
struct GrammarRow: View {
    let candidate: GrammarCandidate
    let onOpen:    () -> Void

    var body: some View {
        Button(action: onOpen) {
            HStack(spacing: 10) {
                Image(systemName: "doc.text.magnifyingglass")
                    .font(.system(size: 13))
                    .foregroundColor(.secondary)
                VStack(alignment: .leading, spacing: 2) {
                    Text(candidate.grammarName)
                        .font(.system(size: 13, weight: .medium))
                        .foregroundColor(.primary)
                    Text(candidate.url)
                        .font(.system(size: 11))
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                }
                Spacer()
                Text("source")
                    .font(.system(size: 10))
                    .foregroundColor(.secondary)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(Color.secondary.opacity(0.12))
                    .cornerRadius(4)
            }
            .padding(.vertical, 7)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .overlay(Divider().opacity(0.2), alignment: .bottom)
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
                    Text(item.title.prefix(80)).font(.system(size: 13, weight: .medium)).lineLimit(1)
                    Text(item.url).font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
                    Text(item.visitedAt.formatted(date: .abbreviated, time: .shortened))
                        .font(.system(size: 10)).foregroundColor(.secondary.opacity(0.6))
                }
                .padding(.vertical, 2)
            }
            .navigationTitle("History")
            .toolbar {
                ToolbarItem { Button("Done")  { dismiss() } }
                ToolbarItem { Button("Clear") { model.history = []; model.savePersisted() } }
            }
        }
        .frame(width: 520, height: 520)
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
                            Text(bm.title.prefix(60)).font(.system(size: 13, weight: .medium)).lineLimit(1)
                            Text(bm.url).font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
                        }
                        Spacer()
                        Button("Open") {
                            if let u = URL(string: bm.url) { model.navigate(to: u) }
                            dismiss()
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                    }
                    .padding(.vertical, 2)
                }
                .onDelete { idx in idx.forEach { model.removeBookmark(id: model.bookmarks[$0].id) } }
            }
            .navigationTitle("Bookmarks")
            .toolbar { ToolbarItem { Button("Done") { dismiss() } } }
        }
        .frame(width: 520, height: 420)
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
                HStack(spacing: 10) {
                    ProgressView(value: model.seedingProgress)
                    Text("Building index…").font(.system(size: 12)).foregroundColor(.secondary)
                }
            }

            Grid(alignment: .leading, horizontalSpacing: 28, verticalSpacing: 12) {
                GridRow { s("Documents", "\(model.stats.docCount)")
                           s("Segments", "\(model.stats.segmentCount)") }
                GridRow { s("Index size", ByteCountFormatter.string(
                                fromByteCount: Int64(model.stats.indexBytes), countStyle: .file))
                           s("Sources", "\(model.stats.sourceCount)") }
                GridRow { s("Nav edges", "\(model.stats.navEdgeCount)")
                           s("Grammars", "\(model.stats.grammarCount)") }
                GridRow { s("History", "\(model.history.count)")
                           s("Bookmarks", "\(model.bookmarks.count)") }
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

    private func s(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.system(size: 11)).foregroundColor(.secondary)
            Text(value).font(.system(size: 15, weight: .semibold)).monospacedDigit()
        }
    }
}
