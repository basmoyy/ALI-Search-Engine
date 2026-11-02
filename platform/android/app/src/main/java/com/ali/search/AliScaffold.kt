package com.ali.search

// AliScaffold.kt — Bottom navigation scaffold shared by all screens.

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.List
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.navigation.NavController
import androidx.navigation.compose.currentBackStackEntryAsState

data class NavDestination(
    val route: String,
    val label: String,
    val icon:  @Composable () -> Unit,
)

private val destinations = listOf(
    NavDestination("search",    "Search")    { Icon(Icons.Default.Search,   "Search") },
    NavDestination("index",     "Index")     { Icon(Icons.Default.List,     "Index") },
    NavDestination("benchmark", "Benchmark") { Icon(Icons.Default.Settings, "Benchmark") },
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AliScaffold(
    navController: NavController,
    content: @Composable (PaddingValues) -> Unit,
) {
    val backStack by navController.currentBackStackEntryAsState()
    val currentRoute = backStack?.destination?.route ?: "search"
    // For nested browser routes, highlight search tab.
    val activeTab = when {
        currentRoute.startsWith("browser") -> "search"
        else -> currentRoute
    }

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("ALI") })
        },
        bottomBar = {
            NavigationBar {
                destinations.forEach { dest ->
                    NavigationBarItem(
                        selected = activeTab == dest.route,
                        onClick  = {
                            if (currentRoute != dest.route) {
                                navController.navigate(dest.route) {
                                    popUpTo(navController.graph.startDestinationId) {
                                        saveState = true
                                    }
                                    launchSingleTop = true
                                    restoreState    = true
                                }
                            }
                        },
                        icon     = dest.icon,
                        label    = { Text(dest.label) },
                    )
                }
            }
        },
        content = content,
    )
}
