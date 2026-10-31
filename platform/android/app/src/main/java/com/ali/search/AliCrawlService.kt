package com.ali.search

// AliCrawlService.kt — Foreground service for active crawl sessions.
//
// Started when the user explicitly taps "Start" in the UI. Runs a
// foreground service so Android does not kill the crawl process when
// the app is backgrounded. Shows a persistent notification with crawl
// progress and a "Stop" action.
//
// This is separate from AliCrawlerWorker (WorkManager) which handles
// opportunistic background crawling. The foreground service is for
// user-initiated crawl sessions.
//
// Cross-platform note: foreground services are Android-specific. The
// equivalent on iOS would be a BGProcessingTask. On desktop, the crawler
// runs as a background thread managed by the application directly.
// All crawler logic is in the C++ core — this class only manages
// Android lifecycle.

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.IBinder
import androidx.core.app.NotificationCompat

class AliCrawlService : Service() {

    companion object {
        const val CHANNEL_ID   = "ali_crawl"
        const val NOTIF_ID     = 1001
        const val ACTION_STOP  = "com.ali.search.STOP_CRAWL"
        const val EXTRA_URLS   = "seed_urls"
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            AliApplication.engine?.crawlerStop()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }

        val seedUrls = intent?.getStringArrayListExtra(EXTRA_URLS) ?: emptyList<String>()

        // Start foreground with a persistent notification.
        startForeground(NOTIF_ID, buildNotification("Crawling…"))

        // Start the crawler in the C++ core.
        val engine = AliApplication.engine
        if (engine != null) {
            seedUrls.forEach { engine.crawlerEnqueue(it) }
            engine.crawlerStart()
        } else {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }

        // Sticky: system restarts the service if killed, resuming crawl
        // from the persisted frontier in SQLite.
        return START_STICKY
    }

    override fun onDestroy() {
        AliApplication.engine?.crawlerStop()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun buildNotification(status: String): Notification {
        val stopIntent = PendingIntent.getService(
            this, 0,
            Intent(this, AliCrawlService::class.java).apply { action = ACTION_STOP },
            PendingIntent.FLAG_IMMUTABLE,
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("ALI Crawler")
            .setContentText(status)
            .setSmallIcon(android.R.drawable.ic_popup_sync)
            .setOngoing(true)
            .addAction(android.R.drawable.ic_media_pause, "Stop", stopIntent)
            .build()
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "ALI Crawler",
            NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = "Background crawl progress"
        }
        getSystemService(NotificationManager::class.java)
            .createNotificationChannel(channel)
    }
}
