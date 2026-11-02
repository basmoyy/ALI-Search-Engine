// ContentView.swift — Main window: tab bar + toolbar + browser/search panes.

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
            TabBarView(model: model)
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

            if showSearch {
                SearchResultsView(
                    model:           model,
                    addressBarText:  $addressBarText,
                    showSearch:      $showSearch
                )
            } else if let tab = model.activeTab {
                BrowserPane(tab: tab, model: model)
            }
        }
        .sheet(isPresented: $showHistory)   { HistoryView(model: model) }
        .sheet(isPresented: $showBookmarks) { BookmarksView(model: model, addressBarText: $addressBarText) }
        .sheet(isPresented: $showStats)     { StatsView(model: model) }
        .frame(minWidth: 900, minHeight: 600)
        .background(Color(NSColor.windowBackgroundColor))
        .onChange(of: model.activeTabIndex) { _ in syncAddressBar() }
        .onChange(of: model.activeTab?.url) { _ in syncAddressBar() }
    }

    private func syncAddressBar() {
        // Don't overwrite the bar if we're showing search results
        if !showSearch {
            addressBarText = model.activeTab?.url?.absoluteString ?? ""
        }
    }
}

// MARK: - Tab Bar

struct TabBarView: View {
    @ObservedObject var model: BrowserModel

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 0) {
                ForEach(Array(model.tabs.enumerated()), id: \.element.id) { idx, tab in
                    TabCell(tab: tab, isActive: idx == model.activeTabIndex) {
                        model.selectTab(tab)
                    } onClose: {
                        model.closeTab(id: tab.id)
                    }
                }
                Button(action: { model.newTab() }) {
                    Image(systemName: "plus")
                        .font(.system(size: 11, weight: .medium))
                        .frame(width: 28, height: 28)
                }
                .buttonStyle(.plain)
                .padding(.horizontal, 4)
            }
            .padding(.horizontal, 4)
        }
        .frame(height: 34)
        .background(Color(NSColor.controlBackgroundColor))
    }
}

struct TabCell: View {
    @ObservedObject var tab: BrowserTab
    let isActive: Bool
    let onSelect: () -> Void
    let onClose:  () -> Void

    var body: some View {
        HStack(spacing: 4) {
            if tab.isLoading {
                ProgressView().scaleEffect(0.5).frame(width: 12, height: 12)
            } else {
                Image(systemName: "globe")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            }
            Text(tab.title.prefix(22))
                .font(.system(size: 12))
                .lineLimit(1)
                .frame(maxWidth: 150, alignment: .leading)
            Button(action: onClose) {
                Image(systemName: "xmark").font(.system(size: 9, weight: .bold))
            }
            .buttonStyle(.plain)
            .opacity(0.5)
        }
        .padding(.horizontal, 10)
        .frame(height: 30)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isActive
                      ? Color(NSColor.selectedContentBackgroundColor).opacity(0.15)
                      : Color.clear)
        )
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
        HStack(spacing: 6) {
            // Back
            Button { model.activeTab?.goBack() } label: {
                Image(systemName: "chevron.left")
            }
            .disabled(!(model.activeTab?.canGoBack ?? false))
            .keyboardShortcut("[", modifiers: .command)

            // Forward
            Button { model.activeTab?.goForward() } label: {
                Image(systemName: "chevron.right")
            }
            .disabled(!(model.activeTab?.canGoForward ?? false))
            .keyboardShortcut("]", modifiers: .command)

            // Reload / Stop
            Button {
                if model.activeTab?.isLoading == true { model.activeTab?.stopLoad() }
                else { model.activeTab?.reload() }
            } label: {
                Image(systemName: model.activeTab?.isLoading == true
                      ? "xmark" : "arrow.clockwise")
            }
            .keyboardShortcut("r", modifiers: .command)

            // Address / search bar
            AddressBar(
                text:      $addressBarText,
                isLoading: model.activeTab?.isLoading ?? false,
                progress:  model.activeTab?.loadProgress ?? 0
            ) { submitted in
                // FIX: single, clear decision — called exactly once on Enter
                commitInput(submitted)
            }

            // Bookmark star
            Button {
                guard let url   = model.activeTab?.url?.absoluteString,
                      let title = model.activeTab?.title else { return }
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
                    .foregroundColor(model.isBookmarked(url: url) ? .yellow : .secondary)
            }

            Button { showHistory.toggle()   } label: { Image(systemName: "clock") }
            Button { showBookmarks.toggle() } label: { Image(systemName: "bookmark") }
            Button { showStats.toggle()     } label: { Image(systemName: "chart.bar") }
        }
        .buttonStyle(.plain)
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
    }

    /// The single entry point for address-bar input. Called exactly once per Enter.
    private func commitInput(_ raw: String) {
        let text = raw.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty else { return }

        // 1. Explicit URL or domain name → navigate directly, never search
        if looksLikeURL(text) {
            let url = text.hasPrefix("http") ? text : "https://\(text)"
            if let u = URL(string: url) {
                showSearch = false
                addressBarText = url
                model.navigate(to: u)
            }
            return
        }

        // 2. Everything else → search (single call here, nowhere else)
        showSearch = true
        addressBarText = text
        model.performSearch(query: text)
    }

    /// Returns true if the input should be treated as a URL rather than a search query.
    private func looksLikeURL(_ s: String) -> Bool {
        if s.hasPrefix("http://") || s.hasPrefix("https://") { return true }
        // Single token (no spaces) with a dot not at the start/end
        if !s.contains(" ") {
            let parts = s.components(separatedBy: ".")
            if parts.count >= 2 && parts.first!.count > 0 && parts.last!.count > 0 {
                return true
            }
        }
        return false
    }
}

// MARK: - Address Bar

struct AddressBar: View {
    @Binding var text:  String
    let isLoading:      Bool
    let progress:       Double
    let onSubmit:       (String) -> Void   // called exactly once, by .onSubmit

    @FocusState private var focused: Bool

    var body: some View {
        ZStack(alignment: .leading) {
            // Load progress tint
            GeometryReader { geo in
                if isLoading && progress > 0 && progress < 1 {
                    Rectangle()
                        .fill(Color.accentColor.opacity(0.12))
                        .frame(width: geo.size.width * progress)
                        .animation(.easeInOut(duration: 0.25), value: progress)
                }
            }
            .cornerRadius(6)

            TextField("Search or enter address", text: $text)
                .textFieldStyle(.plain)
                .font(.system(size: 13))
                .padding(.horizontal, 8)
                .onSubmit {
                    // This is the ONLY place onSubmit is called.
                    onSubmit(text)
                }
                .focused($focused)
                .onExitCommand {
                    // Esc clears focus without submitting
                    focused = false
                }
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
            WebViewRepresentable(tab: tab, model: model)
                .ignoresSafeArea()
            if tab.isLoading {
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
            VStack(alignment: .leading, spacing: 0) {

                // ── 1. Direct navigation suggestions (top priority) ───────────
                // These appear immediately, before any I/O, so the user can go
                // straight to the site they typed.
                if !model.navSuggestions.isEmpty {
                    SectionHeader(title: "Navigate to")
                    ForEach(model.navSuggestions) { nav in
                        NavigationalRow(nav: nav) { open(url: nav.url) }
                    }
                    Divider().padding(.vertical, 6)
                }

                // ── 2. Grammar-guided discovery candidates ────────────────────
                if !model.grammarResults.isEmpty {
                    SectionHeader(title: "Relevant sources")
                    ForEach(model.grammarResults) { g in
                        GrammarCandidateRow(candidate: g) { open(url: g.url) }
                    }
                    Divider().padding(.vertical, 6)
                }

                // ── 3. Local BM25 index results ───────────────────────────────
                if model.isSearching {
                    HStack(spacing: 8) {
                        ProgressView().scaleEffect(0.7)
                        Text("Searching…")
                            .font(.system(size: 13))
                            .foregroundColor(.secondary)
                    }
                    .padding(12)
                } else if !model.searchResults.isEmpty {
                    SectionHeader(title: "From local index (\(model.searchResults.count))")
                    ForEach(model.searchResults) { r in
                        SearchResultRow(result: r) { open(url: r.url) }
                    }
                } else if model.navSuggestions.isEmpty && model.grammarResults.isEmpty {
                    emptyState
                }
            }
            .padding(.horizontal, 16)
            .padding(.top, 8)
        }
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var emptyState: some View {
        VStack(spacing: 10) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 36)).foregroundColor(.secondary)
            Text("No results for \"\(model.searchQuery)\"")
                .font(.system(size: 14)).foregroundColor(.secondary)
            Text("ALI is discovering relevant pages in the background.")
                .font(.system(size: 12)).foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(40)
    }

    private func open(url: String) {
        guard let u = URL(string: url) else { return }
        showSearch     = false
        addressBarText = url
        model.navigate(to: u)
    }
}

// MARK: - Result rows

struct SectionHeader: View {
    let title: String
    var body: some View {
        Text(title.uppercased())
            .font(.system(size: 10, weight: .semibold))
            .foregroundColor(.secondary)
            .tracking(0.5)
            .padding(.top, 10)
            .padding(.bottom, 4)
    }
}

struct NavigationalRow: View {
    let nav:    NavigationalResult
    let onOpen: () -> Void

    var body: some View {
        Button(action: onOpen) {
            HStack(spacing: 12) {
                Image(systemName: "arrow.right.circle.fill")
                    .font(.system(size: 18))
                    .foregroundColor(.accentColor)
                VStack(alignment: .leading, spacing: 2) {
                    Text(nav.label)
                        .font(.system(size: 14, weight: .medium))
                        .foregroundColor(.primary)
                    Text(nav.subtitle)
                        .font(.system(size: 11))
                        .foregroundColor(.secondary)
                }
                Spacer()
                Image(systemName: "chevron.right")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            }
            .padding(.vertical, 8)
            .padding(.horizontal, 4)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .overlay(Divider().opacity(0.3), alignment: .bottom)
    }
}

struct SearchResultRow: View {
    let result:  SearchResult
    let onOpen:  () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(result.title.isEmpty ? result.url : result.title)
                .font(.system(size: 14, weight: .medium))
                .foregroundColor(.accentColor)
                .lineLimit(1)
            Text(result.url)
                .font(.system(size: 11))
                .foregroundColor(.secondary)
                .lineLimit(1)
            if !result.excerpt.isEmpty {
                Text(result.excerpt)
                    .font(.system(size: 12))
                    .foregroundColor(.primary.opacity(0.85))
                    .lineLimit(2)
            }
        }
        .padding(.vertical, 8)
        .padding(.horizontal, 4)
        .contentShape(Rectangle())
        .onTapGesture { onOpen() }
        .overlay(Divider().opacity(0.3), alignment: .bottom)
    }
}

struct GrammarCandidateRow: View {
    let candidate: GrammarCandidate
    let onOpen: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text(candidate.grammarName)
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundColor(.accentColor)
                    Text("·")
                        .foregroundColor(.secondary)
                    Text(candidate.domain)
                        .font(.system(size: 11))
                        .foregroundColor(.secondary)
                }
                Text(candidate.url)
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
                    .lineLimit(1)
            }
            Spacer()
            Button("Open") { onOpen() }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)
        }
        .padding(.vertical, 7)
        .contentShape(Rectangle())
        .onTapGesture { onOpen() }
        .overlay(Divider().opacity(0.25), alignment: .bottom)
    }
}

// MARK: - History

struct HistoryView: View {
    @ObservedObject var model: BrowserModel
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationView {
            List {
                ForEach(model.history.prefix(200)) { item in
                    VStack(alignment: .leading, spacing: 2) {
                        Text(item.title.prefix(80))
                            .font(.system(size: 13, weight: .medium))
                            .lineLimit(1)
                        Text(item.url)
                            .font(.system(size: 11))
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                        Text(item.visitedAt.formatted(date: .abbreviated, time: .shortened))
                            .font(.system(size: 10))
                            .foregroundColor(.secondary.opacity(0.6))
                    }
                    .padding(.vertical, 2)
                }
            }
            .navigationTitle("History")
            .toolbar {
                ToolbarItem { Button("Done")  { dismiss() } }
                ToolbarItem { Button("Clear") { model.history = []; model.savePersisted() } }
            }
        }
        .frame(width: 500, height: 500)
    }
}

// MARK: - Bookmarks

struct BookmarksView: View {
    @ObservedObject var model: BrowserModel
    @Binding var addressBarText: String
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationView {
            List {
                ForEach(model.bookmarks) { bm in
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(bm.title.prefix(60))
                                .font(.system(size: 13, weight: .medium))
                                .lineLimit(1)
                            Text(bm.url)
                                .font(.system(size: 11))
                                .foregroundColor(.secondary)
                                .lineLimit(1)
                        }
                        Spacer()
                        Button("Open") {
                            addressBarText = bm.url
                            if let u = URL(string: bm.url) { model.navigate(to: u) }
                            dismiss()
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                    }
                    .padding(.vertical, 2)
                }
                .onDelete { idx in
                    idx.forEach { model.removeBookmark(id: model.bookmarks[$0].id) }
                }
            }
            .navigationTitle("Bookmarks")
            .toolbar { ToolbarItem { Button("Done") { dismiss() } } }
        }
        .frame(width: 500, height: 400)
    }
}

// MARK: - Stats

struct StatsView: View {
    @ObservedObject var model: BrowserModel
    @Environment(\.dismiss) var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("ALI Engine Stats")
                .font(.title2.bold())

            Grid(alignment: .leading, horizontalSpacing: 24, verticalSpacing: 10) {
                GridRow {
                    stat("Documents indexed", "\(model.stats.docCount)")
                    stat("Segments",          "\(model.stats.segmentCount)")
                }
                GridRow {
                    stat("Index size", ByteCountFormatter.string(
                        fromByteCount: Int64(model.stats.indexBytes), countStyle: .file))
                    stat("Sources tracked", "\(model.stats.sourceCount)")
                }
                GridRow {
                    stat("Nav edges",       "\(model.stats.navEdgeCount)")
                    stat("Grammars loaded", "\(model.stats.grammarCount)")
                }
                GridRow {
                    stat("History entries", "\(model.history.count)")
                    stat("Bookmarks",       "\(model.bookmarks.count)")
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

    private func stat(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.system(size: 11)).foregroundColor(.secondary)
            Text(value).font(.system(size: 15, weight: .semibold)).monospacedDigit()
        }
    }
}
