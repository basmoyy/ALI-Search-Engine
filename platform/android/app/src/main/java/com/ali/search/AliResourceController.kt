package com.ali.search

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.BatteryManager
import android.os.PowerManager

/**
 * Android implementation of the resource controller.
 *
 * Reports current device conditions to the native C++ ResourceController
 * decision logic. All conditions are read lazily on each call; no caching.
 */
class AliResourceController(private val context: Context) {

    data class Conditions(
        val networkAvailable: Boolean,
        val networkMetered: Boolean,
        val charging: Boolean,
        val batteryLevelPct: Int,
        val thermalThrottling: Boolean,
        val memoryPressure: Boolean,
        val interactiveSearch: Boolean,
        val freeStorageBytes: Long,
    )

    fun conditions(interactiveSearch: Boolean = false): Conditions {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val nc = cm.getNetworkCapabilities(cm.activeNetwork)
        val networkAvailable = nc != null &&
                (nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                 || nc.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
                 || nc.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET))
        val networkMetered = cm.isActiveNetworkMetered

        val batteryIntent = context.registerReceiver(
            null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        )
        val level = batteryIntent?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = batteryIntent?.getIntExtra(BatteryManager.EXTRA_SCALE, 100) ?: 100
        val batteryPct = if (scale > 0) (level * 100 / scale).coerceIn(0, 100) else 50
        val plugged = batteryIntent?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0
        val charging = plugged != 0

        val pm = context.getSystemService(Context.POWER_SERVICE) as PowerManager
        val thermalThrottling = pm.isDeviceIdleMode

        val freeBytes = context.filesDir.freeSpace

        return Conditions(
            networkAvailable   = networkAvailable,
            networkMetered     = networkMetered,
            charging           = charging,
            batteryLevelPct    = batteryPct,
            thermalThrottling  = thermalThrottling,
            memoryPressure     = false,  // Populated via ComponentCallbacks2 in Application
            interactiveSearch  = interactiveSearch,
            freeStorageBytes   = freeBytes,
        )
    }

    /** Returns true if background crawling is currently permitted. */
    fun mayCrawl(interactiveSearch: Boolean = false): Boolean {
        val c = conditions(interactiveSearch)
        if (c.interactiveSearch) return false
        if (c.memoryPressure)    return false
        if (c.thermalThrottling) return false
        if (!c.networkAvailable) return false
        if (c.batteryLevelPct < 15 && !c.charging) return false
        if (c.networkMetered && !c.charging)        return false
        return true
    }
}
