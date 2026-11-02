// WebViewRepresentable.swift — NSViewRepresentable wrapping WKWebView.

import SwiftUI
import WebKit
import Combine

struct WebViewRepresentable: NSViewRepresentable {
    @ObservedObject var tab: BrowserTab
    @ObservedObject var model: BrowserModel

    func makeCoordinator() -> Coordinator {
        Coordinator(tab: tab, model: model)
    }

    func makeNSView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .default()    // persistent cookies & storage
        let wv = WKWebView(frame: .zero, configuration: config)
        wv.navigationDelegate = context.coordinator
        wv.uiDelegate         = context.coordinator
        wv.allowsBackForwardNavigationGestures = true
        context.coordinator.webView = wv
        // Subscribe to commands from the tab model
        context.coordinator.subscribeToCommands()
        return wv
    }

    func updateNSView(_ nsView: WKWebView, context: Context) {
        // Commands are handled via the PassthroughSubject — no work needed here.
    }

    // MARK: Coordinator

    final class Coordinator: NSObject, WKNavigationDelegate, WKUIDelegate {
        let tab:   BrowserTab
        let model: BrowserModel
        var webView: WKWebView?
        private var cancellables = Set<AnyCancellable>()

        init(tab: BrowserTab, model: BrowserModel) {
            self.tab   = tab
            self.model = model
        }

        func subscribeToCommands() {
            tab.commandSubject
                .receive(on: DispatchQueue.main)
                .sink { [weak self] cmd in self?.handle(cmd) }
                .store(in: &cancellables)
        }

        private func handle(_ cmd: BrowserTab.Command) {
            guard let wv = webView else { return }
            switch cmd {
            case .load(let url):
                wv.load(URLRequest(url: url))
            case .goBack:
                if wv.canGoBack { wv.goBack() }
            case .goForward:
                if wv.canGoForward { wv.goForward() }
            case .reload:
                wv.reload()
            case .stopLoad:
                wv.stopLoading()
            }
        }

        // MARK: WKNavigationDelegate

        func webView(_ webView: WKWebView,
                     didStartProvisionalNavigation navigation: WKNavigation!) {
            DispatchQueue.main.async { [weak self] in
                self?.tab.isLoading = true
                self?.tab.loadProgress = 0.1
            }
        }

        func webView(_ webView: WKWebView,
                     didCommit navigation: WKNavigation!) {
            DispatchQueue.main.async { [weak self] in
                self?.tab.loadProgress = 0.5
            }
        }

        func webView(_ webView: WKWebView,
                     didFinish navigation: WKNavigation!) {
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.tab.isLoading    = false
                self.tab.loadProgress = 1.0
                self.tab.canGoBack    = webView.canGoBack
                self.tab.canGoForward = webView.canGoForward

                let url   = webView.url?.absoluteString ?? ""
                let title = webView.title ?? url

                // Update tab
                self.tab.url   = webView.url
                self.tab.title = title.isEmpty ? url : title

                // Record in browsing history and feed to ALI
                if !url.isEmpty && url != "about:blank" {
                    self.model.recordVisit(url: url, title: title)
                }
            }
        }

        func webView(_ webView: WKWebView,
                     didFail navigation: WKNavigation!,
                     withError error: Error) {
            DispatchQueue.main.async { [weak self] in
                self?.tab.isLoading    = false
                self?.tab.loadProgress = 0
            }
        }

        func webView(_ webView: WKWebView,
                     didFailProvisionalNavigation navigation: WKNavigation!,
                     withError error: Error) {
            DispatchQueue.main.async { [weak self] in
                self?.tab.isLoading    = false
                self?.tab.loadProgress = 0
            }
        }

        // MARK: WKUIDelegate — handle window.open() / target="_blank"
        func webView(_ webView: WKWebView,
                     createWebViewWith configuration: WKWebViewConfiguration,
                     for navigationAction: WKNavigationAction,
                     windowFeatures: WKWindowFeatures) -> WKWebView? {
            // Open new-tab requests inside ALI
            if let url = navigationAction.request.url {
                DispatchQueue.main.async { [weak self] in
                    self?.model.newTab(url: url)
                }
            }
            return nil
        }
    }
}
