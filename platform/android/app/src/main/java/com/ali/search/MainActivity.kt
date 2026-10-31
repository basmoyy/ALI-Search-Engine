package com.ali.search

// MainActivity.kt — Single-activity entry point for the ALI app.
//
// Uses Jetpack Compose with a bottom navigation bar to switch between:
//   Search      — query the local index
//   Index       — index status, segment count, crawler control
//   Benchmark   — run the device validation harness
//
// The activity is intentionally thin. All logic is in AliViewModel.
// Navigation uses the Compose NavController so the back stack is managed
// declaratively without Fragment transactions.

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.ali.search.ui.BenchmarkScreen
import com.ali.search.ui.IndexStatusScreen
import com.ali.search.ui.SearchScreen
import com.ali.search.ui.theme.AliTheme

class MainActivity : ComponentActivity() {

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            AliTheme {
                val navController = rememberNavController()
                AliScaffold(navController = navController) { padding ->
                    NavHost(navController = navController, startDestination = "search") {
                        composable("search")    { SearchScreen(padding) }
                        composable("index")     { IndexStatusScreen(padding) }
                        composable("benchmark") { BenchmarkScreen(padding) }
                    }
                }
            }
        }
    }
}
