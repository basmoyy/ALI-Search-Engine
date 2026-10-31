package com.ali.search

// AliApplication.kt — Application subclass.
//
// Initialises the ALI engine once at process start and holds a reference
// accessible to all components via a companion object.
//
// Cross-platform note: this class is Android-specific. The engine itself
// (AliEngine wrapping the C++ core via JNI) is platform-independent in logic.
// Platform-specific code here is limited to:
//   - Storage path resolution (AliStoragePaths)
//   - ComponentCallbacks2 for memory-pressure signals
//   - Application class lifecycle

import android.app.Application
import android.content.ComponentCallbacks2

class AliApplication : Application() {

    companion object {
        // Engine singleton. Null only before Application.onCreate() completes.
        @Volatile
        var engine: AliEngine? = null
            private set
    }

    override fun onCreate() {
        super.onCreate()
        val paths = AliStoragePaths.get(this)
        engine = AliEngine.open(paths.indexRoot, paths.dbPath)
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        // On memory pressure, signal the engine to purge caches.
        // The scanner's block cache is the primary eviction target.
        if (level >= ComponentCallbacks2.TRIM_MEMORY_MODERATE) {
            // Future: engine?.purgeCaches()
            // For now, note that the 12 MB working-set budget means
            // this is unlikely to be necessary in practice.
        }
    }

    override fun onTerminate() {
        engine?.close()
        engine = null
        super.onTerminate()
    }
}
