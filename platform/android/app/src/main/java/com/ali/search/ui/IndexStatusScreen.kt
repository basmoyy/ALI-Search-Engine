package com.ali.search.ui

// IndexStatusScreen.kt — Index status and crawler control panel.
//
// Shows:
//   - Total documents indexed
//   - Number of segments
//   - Index size on disk
//   - Crawler status (running / paused / idle)
//   - Seed URL input and crawl start/stop controls
//
// All state mutations go through AliViewModel — this composable is
// purely declarative and has no direct engine access.

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ali.search.AliViewModel

@Composable
fun IndexStatusScreen(
    padding: PaddingValues,
    vm: AliViewModel = viewModel(),
) {
    val state     by vm.index.collectAsStateWithLifecycle()
    var seedInput by remember { mutableStateOf("") }

    LaunchedEffect(Unit) { vm.refreshIndexState() }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(padding)
            .padding(horizontal = 16.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Spacer(Modifier.height(8.dp))

        // ── Index statistics ──────────────────────────────────────────────────
        Text("Index", style = MaterialTheme.typography.titleMedium)

        StatRow("Documents",  "%,d".format(state.docCount))
        StatRow("Segments",   state.segmentCount.toString())
        StatRow("Index size", formatBytes(state.indexBytes))

        HorizontalDivider()

        // ── Crawler control ───────────────────────────────────────────────────
        Text("Crawler", style = MaterialTheme.typography.titleMedium)

        StatusChip(active = state.crawlerActive)

        OutlinedTextField(
            value         = seedInput,
            onValueChange = { seedInput = it },
            modifier      = Modifier.fillMaxWidth(),
            label         = { Text("Seed URL") },
            placeholder   = { Text("https://example.com") },
            singleLine    = true,
        )

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(
                onClick  = {
                    val urls = seedInput.trim().split("\n", ",")
                        .map { it.trim() }.filter { it.startsWith("http") }
                    if (urls.isNotEmpty()) vm.startCrawler(urls)
                },
                enabled  = !state.crawlerActive,
                modifier = Modifier.weight(1f),
            ) {
                Icon(Icons.Default.PlayArrow, contentDescription = null)
                Spacer(Modifier.width(4.dp))
                Text("Start")
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

        if (state.seedUrls.isNotEmpty()) {
            Text(
                text  = "Seeded: ${state.seedUrls.joinToString(", ")}",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        Spacer(Modifier.height(16.dp))
    }
}

@Composable
private fun StatRow(label: String, value: String) {
    Row(
        modifier            = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun StatusChip(active: Boolean) {
    AssistChip(
        onClick = {},
        label   = { Text(if (active) "Crawling" else "Idle") },
        colors  = AssistChipDefaults.assistChipColors(
            containerColor = if (active)
                MaterialTheme.colorScheme.primaryContainer
            else
                MaterialTheme.colorScheme.surfaceVariant,
        ),
    )
}

private fun formatBytes(bytes: Long): String = when {
    bytes >= 1_000_000_000L -> "%.1f GB".format(bytes / 1e9)
    bytes >= 1_000_000L     -> "%.1f MB".format(bytes / 1e6)
    bytes >= 1_000L         -> "%.1f KB".format(bytes / 1e3)
    else                    -> "$bytes B"
}
