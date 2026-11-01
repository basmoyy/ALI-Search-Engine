package com.ali.search.ui

// IndexStatusScreen.kt — Crawl control, URL fetch and index status.
//
// Phases covered:
//   Phase 2: "Fetch this URL now" — single-URL fetch → parse → index
//   Phase 4: "Start crawler" — autonomous discovery from a seed URL
//
// The crawl log shows real-time status for every fetch attempt.

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ali.search.AliViewModel
import kotlinx.coroutines.launch

@Composable
fun IndexStatusScreen(
    padding: PaddingValues,
    vm: AliViewModel = viewModel(),
) {
    val state   by vm.index.collectAsStateWithLifecycle()
    val context  = LocalContext.current
    var urlInput by remember { mutableStateOf("") }
    val logState = rememberLazyListState()
    val scope    = rememberCoroutineScope()

    // Auto-scroll log to bottom when new entries arrive.
    LaunchedEffect(state.crawlLog.size) {
        if (state.crawlLog.isNotEmpty()) {
            logState.animateScrollToItem(state.crawlLog.size - 1)
        }
    }

    LaunchedEffect(Unit) { vm.refreshIndexState() }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(padding)
            .padding(horizontal = 16.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Spacer(Modifier.height(8.dp))

        // ── Index statistics ──────────────────────────────────────────────────
        Text("Index", style = MaterialTheme.typography.titleMedium)
        StatRow("Documents",  "%,d".format(state.docCount))
        StatRow("Segments",   state.segmentCount.toString())
        StatRow("Index size", formatBytes(state.indexBytes))
        if (state.pagesIndexedThisSession > 0) {
            StatRow("This session", "${state.pagesIndexedThisSession} pages indexed")
        }

        HorizontalDivider()

        // ── Phase 2: Fetch a single URL ───────────────────────────────────────
        Text("Fetch & Index URL", style = MaterialTheme.typography.titleMedium)
        Text(
            "Enter any public URL. ALI will fetch it, extract its text and index it locally.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        OutlinedTextField(
            value         = urlInput,
            onValueChange = { urlInput = it },
            modifier      = Modifier.fillMaxWidth(),
            label         = { Text("URL") },
            placeholder   = { Text("https://en.wikipedia.org/wiki/Search_engine") },
            singleLine    = true,
            leadingIcon   = { Icon(Icons.Default.Search, contentDescription = null) },
        )

        Button(
            onClick  = {
                val url = urlInput.trim()
                if (url.startsWith("http")) vm.fetchAndIndex(url)
            },
            enabled  = !state.isFetching && urlInput.trim().startsWith("http"),
            modifier = Modifier.fillMaxWidth(),
        ) {
            if (state.isFetching) {
                CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
                Text("Fetching…")
            } else {
                Text("Fetch and Index")
            }
        }

        HorizontalDivider()

        // ── Phase 4: Autonomous crawler ───────────────────────────────────────
        Text("Crawler", style = MaterialTheme.typography.titleMedium)
        Text(
            "Enter a seed URL. ALI will fetch it and discover linked pages automatically.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        Row(
            modifier              = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            AssistChip(
                onClick = {},
                label   = {
                    Text(if (state.crawlerActive) "Crawling" else "Idle")
                },
                colors = AssistChipDefaults.assistChipColors(
                    containerColor = if (state.crawlerActive)
                        MaterialTheme.colorScheme.primaryContainer
                    else
                        MaterialTheme.colorScheme.surfaceVariant,
                ),
            )
            if (state.pendingQueueSize > 0) {
                Text(
                    "${state.pendingQueueSize} URLs queued",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(
                onClick  = {
                    val url = urlInput.trim()
                    if (url.startsWith("http")) {
                        vm.startCrawler(listOf(url))
                    }
                },
                enabled  = !state.crawlerActive && urlInput.trim().startsWith("http"),
                modifier = Modifier.weight(1f),
            ) {
                Icon(Icons.Default.PlayArrow, contentDescription = null)
                Spacer(Modifier.width(4.dp))
                Text("Start Crawler")
            }

            OutlinedButton(
                onClick  = { vm.stopCrawler() },
                enabled  = state.crawlerActive,
                modifier = Modifier.weight(1f),
            ) {
                Icon(Icons.Default.Close, contentDescription = null)
                Spacer(Modifier.width(4.dp))
                Text("Stop")
            }
        }

        HorizontalDivider()

        // ── Crawl log ─────────────────────────────────────────────────────────
        if (state.crawlLog.isNotEmpty()) {
            Text("Activity log", style = MaterialTheme.typography.titleMedium)
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(200.dp)
                    .background(MaterialTheme.colorScheme.surfaceVariant,
                                MaterialTheme.shapes.small)
                    .padding(8.dp),
            ) {
                LazyColumn(state = logState) {
                    items(state.crawlLog) { entry ->
                        Text(
                            text     = entry,
                            style    = MaterialTheme.typography.bodySmall.copy(
                                fontFamily = FontFamily.Monospace,
                                fontSize   = 11.sp,
                            ),
                            modifier = Modifier.padding(vertical = 1.dp),
                        )
                    }
                }
            }
        }

        Spacer(Modifier.height(16.dp))
    }
}

@Composable
private fun StatRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

private fun formatBytes(bytes: Long): String = when {
    bytes >= 1_000_000_000L -> "%.1f GB".format(bytes / 1e9)
    bytes >= 1_000_000L     -> "%.1f MB".format(bytes / 1e6)
    bytes >= 1_000L         -> "%.1f KB".format(bytes / 1e3)
    else                    -> "$bytes B"
}
