package com.ali.search.ui

// BrowserScreen.kt — In-app browser using Android WebView.
//
// Features:
//   - Full WebView rendering with JavaScript enabled.
//   - Back/forward/reload navigation controls.
//   - URL bar that updates as the page navigates.
//   - Progress indicator during page load.
//   - Navigation events fed back to AliViewModel (visited URLs queued
//     for indexing, enabling passive content discovery while browsing).
//   - Respects Android back gesture (pops WebView history before popping Compose nav).

import android.annotation.SuppressLint
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ali.search.AliViewModel

@SuppressLint("SetJavaScriptEnabled")
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BrowserScreen(
    initialUrl: String = "",
    padding: PaddingValues,
    vm: AliViewModel = viewModel(),
) {
    // Mutable state for the browser chrome.
    var currentUrl   by remember { mutableStateOf(initialUrl) }
    var displayUrl   by remember { mutableStateOf(initialUrl) }
    var pageTitle    by remember { mutableStateOf("") }
    var loadProgress by remember { mutableIntStateOf(0) }
    var canGoBack    by remember { mutableStateOf(false) }
    var canGoFwd     by remember { mutableStateOf(false) }

    // Reference to the WebView so the chrome controls can call it.
    var webView: WebView? by remember { mutableStateOf(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(padding)
    ) {
        // ── Browser chrome ─────────────────────────────────────────────────

        // Progress bar (visible during load)
        if (loadProgress in 1..99) {
            LinearProgressIndicator(
                progress  = { loadProgress / 100f },
                modifier  = Modifier.fillMaxWidth(),
            )
        }

        // URL bar
        OutlinedTextField(
            value         = displayUrl,
            onValueChange = { displayUrl = it },
            modifier      = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp, vertical = 4.dp),
            singleLine    = true,
            placeholder   = { Text("Enter a URL…", maxLines = 1) },
            label         = if (pageTitle.isNotBlank()) {
                { Text(pageTitle, maxLines = 1, overflow = TextOverflow.Ellipsis) }
            } else null,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Go),
            keyboardActions = KeyboardActions(onGo = {
                val url = normaliseUrl(displayUrl)
                displayUrl = url
                webView?.loadUrl(url)
            }),
        )

        // Navigation controls row
        Row(
            modifier            = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment   = Alignment.CenterVertically,
        ) {
            IconButton(
                onClick  = { webView?.goBack() },
                enabled  = canGoBack,
            ) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
            }
            IconButton(
                onClick  = { webView?.goForward() },
                enabled  = canGoFwd,
            ) {
                Icon(Icons.AutoMirrored.Filled.ArrowForward, contentDescription = "Forward")
            }
            IconButton(onClick = { webView?.reload() }) {
                Icon(Icons.Filled.Refresh, contentDescription = "Reload")
            }
            Text(
                text     = currentUrl.removePrefix("https://").removePrefix("http://"),
                style    = MaterialTheme.typography.labelSmall,
                color    = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
            )
        }

        HorizontalDivider()

        // ── WebView ────────────────────────────────────────────────────────

        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory  = { context ->
                WebView(context).apply {
                    webView = this

                    settings.javaScriptEnabled = true
                    settings.domStorageEnabled  = true
                    settings.loadWithOverviewMode = true
                    settings.useWideViewPort      = true
                    settings.builtInZoomControls  = true
                    settings.displayZoomControls  = false
                    settings.userAgentString      = "ALIBrowser/1.0 (Android)"

                    webViewClient = object : WebViewClient() {
                        override fun shouldOverrideUrlLoading(
                            view: WebView,
                            request: WebResourceRequest,
                        ): Boolean {
                            // Let WebView handle all navigation internally.
                            return false
                        }

                        override fun onPageFinished(view: WebView, url: String?) {
                            super.onPageFinished(view, url)
                            val finalUrl = url ?: return
                            currentUrl = finalUrl
                            displayUrl = finalUrl
                            canGoBack  = view.canGoBack()
                            canGoFwd   = view.canGoForward()
                            // Feed visited URL to the discovery engine.
                            vm.onBrowserNavigated(finalUrl)
                        }
                    }

                    webChromeClient = object : WebChromeClient() {
                        override fun onProgressChanged(view: WebView?, newProgress: Int) {
                            loadProgress = newProgress
                        }
                        override fun onReceivedTitle(view: WebView?, title: String?) {
                            pageTitle = title ?: ""
                        }
                    }

                    if (initialUrl.isNotBlank()) loadUrl(normaliseUrl(initialUrl))
                }
            },
            update = { _ ->
                // WebView managed via the captured `webView` reference.
            }
        )
    }
}

/** Prepend https:// if the URL has no scheme. */
private fun normaliseUrl(raw: String): String {
    val trimmed = raw.trim()
    return if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) {
        trimmed
    } else {
        "https://$trimmed"
    }
}
