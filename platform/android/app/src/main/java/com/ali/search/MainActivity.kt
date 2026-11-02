package com.ali.search

// MainActivity.kt — Single-activity entry point for the ALI app.
//
// Screens:
//   search      — query the local index; tap a result to open it in the browser
//   browser/{url} — in-app WebView browser
//   index       — index status, segment count, crawler control
//   benchmark   — run the device validation harness

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.ali.search.ui.BenchmarkScreen
import com.ali.search.ui.BrowserScreen
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
                        composable("search") {
                            SearchScreen(padding = padding, navController = navController)
                        }
                        composable("browser/{url}") { back ->
                            val encoded = back.arguments?.getString("url") ?: ""
                            val url = java.net.URLDecoder.decode(encoded, "UTF-8")
                            BrowserScreen(
                                initialUrl = url,
                                padding    = padding,
                            )
                        }
                        composable("index")     { IndexStatusScreen(padding) }
                        composable("benchmark") { BenchmarkScreen(padding) }
                    }
                }
            }
        }
    }
}
