package com.ali.search

// AliStoragePaths.kt — Resolves platform-specific storage paths for the engine.
//
// The index root and SQLite database live in the app's files directory so
// they are included in Android Auto Backup and are private to the app.
// Users can inspect or delete these via standard Android storage settings.
//
// Path layout:
//   files/ali/index/    — segment directories
//   files/ali/meta.db   — SQLite metadata database
//   files/ali/locality  — persisted locality statistics

import android.content.Context
import java.io.File

data class AliStoragePaths(
    val indexRoot: String,
    val dbPath: String,
    val localityPath: String,
) {
    companion object {
        fun get(context: Context): AliStoragePaths {
            val base = File(context.filesDir, "ali")
            val indexDir = File(base, "index")
            indexDir.mkdirs()
            return AliStoragePaths(
                indexRoot    = indexDir.absolutePath,
                dbPath       = File(base, "meta.db").absolutePath,
                localityPath = File(base, "locality").absolutePath,
            )
        }
    }
}
