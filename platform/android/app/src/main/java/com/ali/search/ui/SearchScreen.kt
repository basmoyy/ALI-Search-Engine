package com.ali.search.ui

// SearchScreen.kt — Local search interface.
//
// Results open in ALI's built-in browser (BrowserScreen).

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.ali.search.AliEngine
import com.ali.search.AliViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SearchScreen(
    padding: PaddingValues,
    navController: NavController,
    vm: AliViewModel = viewModel(),
) {
    val state        by vm.search.collectAsStateWithLifecycle()
    val focusManager  = LocalFocusManager.current

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(padding)
            .padding(horizontal = 16.dp),
    ) {
        Spacer(Modifier.height(8.dp))

        // ── Search bar ────────────────────────────────────────────────────────
        OutlinedTextField(
            value         = state.query,
            onValueChange = vm::onQueryChange,
            modifier      = Modifier.fillMaxWidth(),
            placeholder   = { Text("Search…") },
            leadingIcon   = { Icon(Icons.Default.Search, contentDescription = null) },
            trailingIcon  = {
                if (state.query.isNotEmpty()) {
                    IconButton(onClick = { vm.onQueryChange("") }) {
                        Icon(Icons.Default.Clear, contentDescription = "Clear")
                    }
                }
            },
            singleLine      = true,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(onSearch = {
                vm.executeSearch()
                focusManager.clearFocus()
            }),
        )

        Spacer(Modifier.height(4.dp))

        // ── Status line ───────────────────────────────────────────────────────
        if (state.isLoading) {
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        } else if (state.query.isNotEmpty()) {
            Text(
                text  = "${state.results.size} results · ${state.elapsedMs} ms",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        state.error?.let { err ->
            Text(err, color = MaterialTheme.colorScheme.error,
                 style = MaterialTheme.typography.labelSmall)
        }

        Spacer(Modifier.height(8.dp))

        // ── Results list ──────────────────────────────────────────────────────
        if (state.results.isEmpty() && state.query.isNotEmpty() && !state.isLoading) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("No results for \"${state.query}\"",
                     style = MaterialTheme.typography.bodyMedium,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        } else {
            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(state.results, key = { it.url }) { result ->
                    ResultCard(result) {
                        // Open result in the in-app browser.
                        val encoded = java.net.URLEncoder.encode(result.url, "UTF-8")
                        navController.navigate("browser/$encoded")
                    }
                }
            }
        }
    }
}

@Composable
private fun ResultCard(
    result:  AliEngine.Result,
    onClick: () -> Unit,
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                text     = result.title.ifBlank { result.url },
                style    = MaterialTheme.typography.titleSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Spacer(Modifier.height(2.dp))
            Text(
                text     = result.url,
                style    = MaterialTheme.typography.labelSmall,
                color    = MaterialTheme.colorScheme.primary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (result.excerpt.isNotBlank()) {
                Spacer(Modifier.height(4.dp))
                Text(
                    text     = result.excerpt,
                    style    = MaterialTheme.typography.bodySmall,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Spacer(Modifier.height(4.dp))
            Text(
                text  = "score: ${"%.4f".format(result.score)}",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}
