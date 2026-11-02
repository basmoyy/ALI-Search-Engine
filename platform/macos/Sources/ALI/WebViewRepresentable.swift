// WebViewRepresentable.swift
//
// Tab-aware browser view.
//
// One WKWebView is created per BrowserTab and stored in WebViewPool.
// BrowserContentView (NSViewControllerRepresentable) directly manages the
// NSView hierarchy, swapping in the correct WKWebView when the active tab
// changes. This avoids SwiftUI's reconciliation from showing stale content.

import SwiftUI
import WebKit
import Combine

// MARK: - Shared WKWebView pool

final class WebViewPool {
    static let shared = WebViewPool()
    private var views:        [UUID: WKWebView]          = [:]
    private var coordinators: [UUID: WebViewCoordinator] = [:]

    private init() {}

    /// Get or create the WKWebView for a given tab.
    func get(for tab: BrowserTab, model: BrowserModel) -> WKWebView {
        if let existing = views[tab.id] { return existing }
        let wv = makeWebView(tab: tab, model: model)
        views[tab.id]        = wv
        return wv
    }

    private func makeWebView(tab: BrowserTab, model: BrowserModel) -> WKWebView {
        let cfg = WKWebViewConfiguration()
        cfg.websiteDataStore = .default()
        let wv = WKWebView(frame: .zero, configuration: cfg)
        wv.allowsBackForwardNavigationGestures = true

        let coord = WebViewCoordinator(tab: tab, model: model, webView: wv)
        wv.navigationDelegate = coord
        wv.uiDelegate         = coord
        coord.subscribeToCommands()
        coordinators[tab.id] = coord   // retain
        return wv
    }

    func remove(_ tabId: UUID) {
        views[tabId]?.stopLoading()
        views.removeValue(forKey: tabId)
        coordinators.removeValue(forKey: tabId)
    }
}

// MARK: - Coordinator (one per tab, lives as long as the tab does)

final class WebViewCoordinator: NSObject, WKNavigationDelegate, WKUIDelegate {
    let tab:     BrowserTab
    let model:   BrowserModel
    let webView: WKWebView
    private var cancellables = Set<AnyCancellable>()

    init(tab: BrowserTab, model: BrowserModel, webView: WKWebView) {
        self.tab = tab; self.model = model; self.webView = webView
    }

    func subscribeToCommands() {
        tab.commandSubject
            .receive(on: DispatchQueue.main)
            .sink { [weak self] cmd in
                guard let self else { return }
                switch cmd {
                case .load(let url):  self.webView.load(URLRequest(url: url))
                case .goBack:         if self.webView.canGoBack    { self.webView.goBack() }
                case .goForward:      if self.webView.canGoForward { self.webView.goForward() }
                case .reload:         self.webView.reload()
                case .stopLoad:       self.webView.stopLoading()
                }
            }
            .store(in: &cancellables)
    }

    func webView(_ wv: WKWebView, didStartProvisionalNavigation _: WKNavigation!) {
        DispatchQueue.main.async { [weak self] in
            self?.tab.isLoading = true; self?.tab.loadProgress = 0.1
        }
    }
    func webView(_ wv: WKWebView, didCommit _: WKNavigation!) {
        DispatchQueue.main.async { [weak self] in self?.tab.loadProgress = 0.5 }
    }
    func webView(_ wv: WKWebView, didFinish _: WKNavigation!) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.tab.isLoading    = false
            self.tab.loadProgress = 1.0
            self.tab.canGoBack    = wv.canGoBack
            self.tab.canGoForward = wv.canGoForward
            let url   = wv.url?.absoluteString ?? ""
            let title = wv.title ?? ""
            self.tab.url   = wv.url
            self.tab.title = title.isEmpty ? url : title
            if !url.isEmpty && url != "about:blank" {
                self.model.recordVisit(url: url, title: self.tab.title)
            }
        }
    }
    func webView(_ wv: WKWebView, didFail _: WKNavigation!, withError e: Error) {
        DispatchQueue.main.async { [weak self] in
            self?.tab.isLoading = false; self?.tab.loadProgress = 0
        }
    }
    func webView(_ wv: WKWebView, didFailProvisionalNavigation _: WKNavigation!, withError e: Error) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.tab.isLoading = false; self.tab.loadProgress = 0
            let html = "<html><body style='font-family:system-ui;padding:40px;color:#888'><h2>Can't open page</h2><p>\(e.localizedDescription)</p></body></html>"
            self.webView.loadHTMLString(html, baseURL: nil)
        }
    }
    func webView(_ wv: WKWebView, createWebViewWith cfg: WKWebViewConfiguration,
                 for action: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        if let url = action.request.url {
            DispatchQueue.main.async { [weak self] in self?.model.newTab(url: url) }
        }
        return nil
    }
}

// MARK: - BrowserContentViewController
// An NSViewController that directly manages the WKWebView as a subview.
// Calling showTab() immediately replaces the current subview — no SwiftUI
// diffing, no stale content.

final class BrowserContentViewController: NSViewController {
    var model: BrowserModel?
    private var currentTabId: UUID?

    override func loadView() { view = NSView() }

    func showTab(_ tab: BrowserTab) {
        guard let model else { return }

        // Remove existing subviews
        view.subviews.forEach { $0.removeFromSuperview() }
        currentTabId = tab.id

        // Install the pooled WKWebView for this tab
        let wv = WebViewPool.shared.get(for: tab, model: model)
        wv.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(wv)
        NSLayoutConstraint.activate([
            wv.topAnchor.constraint(equalTo: view.topAnchor),
            wv.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            wv.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            wv.trailingAnchor.constraint(equalTo: view.trailingAnchor),
        ])
    }
}

// MARK: - SwiftUI wrapper

struct BrowserContentView: NSViewControllerRepresentable {
    let tab:   BrowserTab
    let model: BrowserModel

    func makeNSViewController(context: Context) -> BrowserContentViewController {
        let vc = BrowserContentViewController()
        vc.model = model
        vc.showTab(tab)
        return vc
    }

    func updateNSViewController(_ vc: BrowserContentViewController, context: Context) {
        vc.model = model
        vc.showTab(tab)   // swaps WKWebView instantly when active tab changes
    }
}
