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
            // ── Tab bar ───────────────────────────────────────────────────────
            TabBarView(model: model)

            Divider()

            // ── Toolbar ───────────────────────────────────────────────────────
            ToolbarView(
                model:           model,
                addressBarText:  $addressBarText,
                showSearch:      $showSearch,
                showHistory:     $showHistory,
                showBookmarks:   $showBookmarks,
                showStats:       $showStats
            )

            Divider()

            // ── Main content area ─────────────────────────────────────────────
            if showSearch {
                SearchResultsView(model: model, addressBarText: $addressBarText, showSearch: $showSearch)
            } else if let tab = model.activeTab {
                BrowserPane(tab: tab, model: model)
            }
        }
        .sheet(isPresented: $showHistory)   { HistoryView(model: model) }
        .sheet(isPresented: $showBookmarks) { BookmarksView(model: model, addressBarText: $addressBarText) }
        .sheet(isPresented: $showStats)     { StatsView(model: model) }
        .frame(minWidth: 900, minHeight: 600)
        .background(Color(NSColor.windowBackgroundColor))
        .onAppear {
            // Sync address bar with active tab URL
        }
        .onChange(of: model.activeTabIndex) { _ in syncAddressBar() }
        .onChange(of: model.activeTab?.url) { _ in syncAddressBar() }
    }

    private func syncAddressBar() {
        addressBarText = model.activeTab?.url?.absoluteString ?? ""
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
                // New tab button
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
    @ObservedObject var tab:    BrowserTab
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
            Text(tab.title.prefix(20))
                .font(.system(size: 12))
                .lineLimit(1)
                .frame(maxWidth: 140, alignment: .leading)
            Button(action: onClose) {
                Image(systemName: "xmark").font(.system(size: 9, weight: .bold))
            }
            .buttonStyle(.plain)
            .opacity(0.6)
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
    @ObservedObject var model:          BrowserModel
    @Binding var addressBarText:        String
    @Binding var showSearch:            Bool
    @Binding var showHistory:           Bool
    @Binding var showBookmarks:         Bool
    @Binding var showStats:             Bool

    var body: some View {
        HStack(spacing: 6) {
            // Back / Forward / Reload
            Button(action: { model.activeTab?.goBack() }) {
                Image(systemName: "chevron.left")
            }
            .disabled(!(model.activeTab?.canGoBack ?? false))
            .keyboardShortcut("[", modifiers: .command)

            Button(action: { model.activeTab?.goForward() }) {
                Image(systemName: "chevron.right")
            }
            .disabled(!(model.activeTab?.canGoForward ?? false))
            .keyboardShortcut("]", modifiers: .command)

            Button(action: {
                if model.activeTab?.isLoading == true { model.activeTab?.stopLoad() }
                else { model.activeTab?.reload() }
            }) {
                Image(systemName: model.activeTab?.isLoading == true ? "xmark" : "arrow.clockwise")
            }
            .keyboardShortcut("r", modifiers: .command)

            // Address / search bar
            AddressBar(
                text:       $addressBarText,
                isLoading:  model.activeTab?.isLoading ?? false,
                progress:   model.activeTab?.loadProgress ?? 0
            ) { submitted in
                handleInput(submitted)
            }

            // Bookmark toggle
            Button(action: {
                guard let url = model.activeTab?.url?.absoluteString,
                      let title = model.activeTab?.title else { return }
                if model.isBookmarked(url: url) {
                    model.removeBookmark(id: model.bookmarks.first(where: { $0.url == url })!.id)
                } else {
                    model.addBookmark(url: url, title: title)
                }
            }) {
                Image(systemName: model.isBookmarked(url: model.activeTab?.url?.absoluteString ?? "")
                      ? "star.fill" : "star")
                    .foregroundColor(model.isBookmarked(url: model.activeTab?.url?.absoluteString ?? "")
                                     ? .yellow : .secondary)
            }

            // Panel toggles
            Button(action: { showHistory.toggle() }) {
                Image(systemName: "clock")
            }
            Button(action: { showBookmarks.toggle() }) {
                Image(systemName: "bookmark")
            }
            Button(action: { showStats.toggle() }) {
                Image(systemName: "chart.bar")
            }
        }
        .buttonStyle(.plain)
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
    }

    private func handleInput(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return }

        // Check if it looks like a URL
        if trimmed.contains(".") && !trimmed.contains(" ") {
            showSearch = false
            model.navigate(to: trimmed)
        } else {
            // Search query
            showSearch = true
            model.searchQuery = trimmed
            model.performSearch(query: trimmed)
        }
        addressBarText = trimmed
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
            // Progress background
            GeometryReader { geo in
                if isLoading && progress > 0 && progress < 1 {
                    Rectangle()
                        .fill(Color.accentColor.opacity(0.15))
                        .frame(width: geo.size.width * progress)
                        .animation(.easeInOut(duration: 0.3), value: progress)
                }
            }
            .cornerRadius(6)

            TextField("Search or enter address", text: $text)
                .textFieldStyle(.plain)
                .font(.system(size: 13))
                .padding(.horizontal, 8)
                .onSubmit { onSubmit(text) }
                .focused($focused)
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
                    .padding(.horizontal, 0)
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

                // Grammar-guided discovery candidates
                if !model.grammarResults.isEmpty {
                    SectionHeader(title: "Discovery — authoritative sources")
                    ForEach(model.grammarResults) { g in
                        GrammarCandidateRow(candidate: g) {
                            open(url: g.url)
                        }
                    }
                    Divider().padding(.vertical, 8)
                }

                // Local BM25 results
                if model.isSearching {
                    HStack {
                        ProgressView()
                        Text("Searching local index…")
                            .font(.system(size: 13))
                            .foregroundColor(.secondary)
                    }
                    .padding()
                } else if model.searchResults.isEmpty {
                    VStack(spacing: 8) {
                        Image(systemName: "magnifyingglass")
                            .font(.largeTitle).foregroundColor(.secondary)
                        Text("No local results for \"\(model.searchQuery)\"")
                            .font(.system(size: 14)).foregroundColor(.secondary)
                        Text("Discovery is running — results will appear as pages are indexed.")
                            .font(.system(size: 12)).foregroundColor(.secondary)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(32)
                } else {
                    SectionHeader(title: "Results from local index (\(model.searchResults.count))")
                    ForEach(model.searchResults) { r in
                        SearchResultRow(result: r) { open(url: r.url) }
                    }
                }
            }
            .padding(.horizontal, 16)
            .padding(.top, 12)
        }
        .background(Color(NSColor.windowBackgroundColor))
    }

    private func open(url: String) {
        guard let u = URL(string: url) else { return }
        showSearch = false
        addressBarText = url
        model.newTab(url: u)
    }
}

struct SectionHeader: View {
    let title: String
    var body: some View {
        Text(title)
            .font(.system(size: 11, weight: .semibold))
            .foregroundColor(.secondary)
            .padding(.bottom, 4)
            .padding(.top, 8)
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
                    .foregroundColor(.primary)
                    .lineLimit(2)
            }
        }
        .padding(.vertical, 8)
        .padding(.horizontal, 4)
        .contentShape(Rectangle())
        .onTapGesture { onOpen() }
        .overlay(Divider().opacity(0.4), alignment: .bottom)
    }
}

struct GrammarCandidateRow: View {
    let candidate: GrammarCandidate
    let onOpen: () -> Void

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text(candidate.grammarName)
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundColor(.accentColor)
                    Text("•")
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
            Text("\(Int(candidate.confidence * 100))%")
                .font(.system(size: 11, weight: .medium))
                .foregroundColor(.secondary)
            Button("Open") { onOpen() }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)
        }
        .padding(.vertical, 6)
        .contentShape(Rectangle())
        .onTapGesture { onOpen() }
        .overlay(Divider().opacity(0.3), alignment: .bottom)
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
                ToolbarItem { Button("Done") { dismiss() } }
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
                            model.newTab(url: URL(string: bm.url)!)
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
                    stat("Documents indexed", value: "\(model.stats.docCount)")
                    stat("Segments",           value: "\(model.stats.segmentCount)")
                }
                GridRow {
                    stat("Index size",         value: ByteCountFormatter.string(fromByteCount: Int64(model.stats.indexBytes), countStyle: .file))
                    stat("Sources tracked",    value: "\(model.stats.sourceCount)")
                }
                GridRow {
                    stat("Nav edges",          value: "\(model.stats.navEdgeCount)")
                    stat("Grammars loaded",    value: "\(model.stats.grammarCount)")
                }
                GridRow {
                    stat("History entries",    value: "\(model.history.count)")
                    stat("Bookmarks",          value: "\(model.bookmarks.count)")
                }
            }

            Divider()

            HStack {
                Button("Refresh") { model.refreshStats() }
                Spacer()
                Button("Done")    { dismiss() }
                    .buttonStyle(.borderedProminent)
            }
        }
        .padding(24)
        .frame(width: 380)
    }

    private func stat(_ label: String, value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label)
                .font(.system(size: 11))
                .foregroundColor(.secondary)
            Text(value)
                .font(.system(size: 15, weight: .semibold))
                .monospacedDigit()
        }
    }
}
