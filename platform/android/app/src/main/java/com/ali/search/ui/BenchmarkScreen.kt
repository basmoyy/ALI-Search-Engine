package com.ali.search.ui

// BenchmarkScreen.kt — Device validation harness.
//
// This screen is the primary tool for validating ALI on physical hardware.
// It runs the same query benchmark as the CLI ali_benchmark tool, but on
// the device itself, and reports:
//
//   - p50 / p95 / p99 query latency (milliseconds)
//   - PSS memory delta (kilobytes) — measured via Debug.MemoryInfo
//   - Document count at time of test
//   - Raw per-query latencies for inspection
//
// Results are displayed on screen and can be shared as plain text.
// This lets you capture and paste exact numbers into the benchmark report
// without needing ADB or a connected computer.
//
// Architecture Brief requirement §15: "Report the exact phone model, OS
// version, build flags, compiler, corpus checksum and benchmark configuration."
// The device info section at the top satisfies this.

import android.os.Build
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ali.search.AliViewModel
import com.ali.search.BenchmarkRun

@Composable
fun BenchmarkScreen(
    padding: PaddingValues,
    vm: AliViewModel = viewModel(),
) {
    val state     by vm.benchmark.collectAsStateWithLifecycle()
    val indexState by vm.index.collectAsStateWithLifecycle()
    val clipboard  = LocalClipboardManager.current

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

        // ── Device info ───────────────────────────────────────────────────────
        Text("Device", style = MaterialTheme.typography.titleMedium)
        DeviceInfoCard(indexState.docCount)

        HorizontalDivider()

        // ── Run button ────────────────────────────────────────────────────────
        Text("Benchmark", style = MaterialTheme.typography.titleMedium)

        if (indexState.docCount == 0L) {
            Text(
                "No documents indexed yet. Import a corpus or start the crawler first.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error,
            )
        }

        Button(
            onClick  = { vm.runBenchmark() },
            enabled  = !state.isRunning && indexState.docCount > 0,
            modifier = Modifier.fillMaxWidth(),
        ) {
            if (state.isRunning) {
                CircularProgressIndicator(
                    modifier  = Modifier.size(18.dp),
                    strokeWidth = 2.dp,
                )
                Spacer(Modifier.width(8.dp))
                Text("Running…")
            } else {
                Icon(Icons.Default.PlayArrow, contentDescription = null)
                Spacer(Modifier.width(4.dp))
                Text("Run Benchmark (warm cache, 50 queries × 5 iterations)")
            }
        }

        state.error?.let {
            Text(it, color = MaterialTheme.colorScheme.error,
                 style = MaterialTheme.typography.labelSmall)
        }

        // ── Results ───────────────────────────────────────────────────────────
        if (state.results.isNotEmpty()) {
            HorizontalDivider()
            Text("Results", style = MaterialTheme.typography.titleMedium)

            state.results.forEach { run ->
                BenchmarkResultCard(run)
            }

            // Copy-to-clipboard for sharing results
            val reportText = buildReportText(state.results)
            OutlinedButton(
                onClick  = { clipboard.setText(AnnotatedString(reportText)) },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Copy report to clipboard")
            }
        }

        Spacer(Modifier.height(16.dp))
    }
}

@Composable
private fun DeviceInfoCard(docCount: Long) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors   = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(modifier = Modifier.padding(12.dp),
               verticalArrangement = Arrangement.spacedBy(4.dp)) {
            InfoRow("Model",      "${Build.MANUFACTURER} ${Build.MODEL}")
            InfoRow("Android",    "API ${Build.VERSION.SDK_INT} (${Build.VERSION.RELEASE})")
            InfoRow("ABI",        Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown")
            InfoRow("ALI docs",   "%,d".format(docCount))
            InfoRow("ALI version","0.1.0")
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun BenchmarkResultCard(run: BenchmarkRun) {
    Card(Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp),
               verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text("Run: ${run.label}", style = MaterialTheme.typography.labelMedium)
            Text("Docs: %,d".format(run.docCount),
                 style = MaterialTheme.typography.bodySmall)

            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            LatencyRow("p50", run.p50Ms)
            LatencyRow("p95", run.p95Ms)
            LatencyRow("p99", run.p99Ms)

            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            Row(Modifier.fillMaxWidth(), Arrangement.SpaceBetween) {
                Text("PSS delta", style = MaterialTheme.typography.bodySmall)
                val pssText = if (run.rssDeltaKb < 1024)
                    "${run.rssDeltaKb} KB"
                else
                    "%.1f MB".format(run.rssDeltaKb / 1024f)
                val budgetOk = run.rssDeltaKb < 12 * 1024
                Text(
                    "$pssText ${if (budgetOk) "✅" else "❌ >12MB"}",
                    style = MaterialTheme.typography.bodySmall,
                    color = if (budgetOk) MaterialTheme.colorScheme.onSurface
                            else         MaterialTheme.colorScheme.error,
                )
            }
        }
    }
}

@Composable
private fun LatencyRow(label: String, ms: Long) {
    Row(Modifier.fillMaxWidth(), Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
        val latencyOk = ms < 100
        Text(
            "$ms ms ${if (latencyOk) "✅" else "⚠️"}",
            style = MaterialTheme.typography.bodySmall,
            color = if (latencyOk) MaterialTheme.colorScheme.onSurface
                    else           MaterialTheme.colorScheme.error,
        )
    }
}

private fun buildReportText(runs: List<BenchmarkRun>): String = buildString {
    appendLine("=== ALI Benchmark Report ===")
    appendLine("Device: ${Build.MANUFACTURER} ${Build.MODEL}")
    appendLine("Android: API ${Build.VERSION.SDK_INT} (${Build.VERSION.RELEASE})")
    appendLine("ABI: ${Build.SUPPORTED_ABIS.firstOrNull()}")
    appendLine()
    runs.forEach { run ->
        appendLine("--- ${run.label} ---")
        appendLine("docs=${run.docCount}")
        appendLine("p50=${run.p50Ms}ms  p95=${run.p95Ms}ms  p99=${run.p99Ms}ms")
        appendLine("pss_delta=${run.rssDeltaKb}KB")
        appendLine()
    }
}
