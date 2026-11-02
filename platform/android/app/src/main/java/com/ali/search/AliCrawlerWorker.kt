package com.ali.search

// AliCrawlerWorker.kt — Android background crawl worker.
//
// Wraps the native crawler lifecycle (start/pause/stop) inside an Android
// WorkManager Worker so the OS can manage execution constraints (battery,
// network, charging) without the app needing to remain in the foreground.
//
// Lifecycle rules (Architecture Brief §10.4):
//   - Only runs on unmetered Wi-Fi by default (user can opt in to metered).
//   - Stops immediately on memory pressure, thermal throttling, or low battery.
//   - All crawl state is persisted in SQLite so work is resumable after kill.
//   - Never promises continuous crawling — each WorkManager invocation is
//     opportunistic and bounded.
//
// Usage: enqueue via AliCrawlerWorker.enqueue(context).

import android.content.Context
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.NetworkType
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.concurrent.TimeUnit

class AliCrawlerWorker(
    context: Context,
    params: WorkerParameters,
) : CoroutineWorker(context, params) {

    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val rc = AliResourceController(applicationContext)

        // Get current device conditions
        val cond = rc.currentConditions()

        // Pre-flight check
        if (!rc.mayCrawl()) return@withContext Result.retry()

        val paths  = AliStoragePaths.get(applicationContext)
        val engine = AliEngine.open(paths.indexRoot, paths.dbPath)
            ?: return@withContext Result.failure()

        try {
            // Push real device conditions to native engine
            engine.updateDeviceConditions(
                networkAvailable  = cond.networkAvailable,
                networkMetered    = cond.networkMetered,
                charging          = cond.charging,
                batteryLevelPct   = cond.batteryLevelPct,
                thermalThrottling = cond.thermalThrottling,
                memoryPressure    = cond.memoryPressure,
                freeStorageBytes  = cond.freeStorageBytes,
            )

            engine.crawlerStart()

            // Run for up to 3 minutes or until conditions change
            val deadline = System.currentTimeMillis() + 3 * 60 * 1000L
            while (System.currentTimeMillis() < deadline && rc.mayCrawl()) {
                Thread.sleep(5_000L)
                // Refresh conditions periodically
                val updatedCond = rc.currentConditions()
                engine.updateDeviceConditions(
                    networkAvailable  = updatedCond.networkAvailable,
                    networkMetered    = updatedCond.networkMetered,
                    charging          = updatedCond.charging,
                    batteryLevelPct   = updatedCond.batteryLevelPct,
                    thermalThrottling = updatedCond.thermalThrottling,
                    memoryPressure    = updatedCond.memoryPressure,
                    freeStorageBytes  = updatedCond.freeStorageBytes,
                )
            }

            engine.crawlerStop()
            Result.success()
        } catch (e: Exception) {
            Result.retry()
        } finally {
            engine.close()
        }
    }

    companion object {
        private const val WORK_NAME = "ali_crawl_periodic"

        /// Enqueue a periodic crawl request. Requires unmetered network and
        /// battery not low. Runs at most once every 15 minutes (WorkManager minimum).
        fun enqueue(context: Context) {
            val constraints = Constraints.Builder()
                .setRequiredNetworkType(NetworkType.UNMETERED)
                .setRequiresBatteryNotLow(true)
                .build()

            val request = PeriodicWorkRequestBuilder<AliCrawlerWorker>(
                15, TimeUnit.MINUTES
            )
                .setConstraints(constraints)
                .build()

            WorkManager.getInstance(context).enqueueUniquePeriodicWork(
                WORK_NAME,
                ExistingPeriodicWorkPolicy.KEEP,
                request,
            )
        }

        /// Cancel any pending crawl work.
        fun cancel(context: Context) {
            WorkManager.getInstance(context).cancelUniqueWork(WORK_NAME)
        }
    }
}
