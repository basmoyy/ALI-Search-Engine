package com.ali.search

// AliAndroidFetcher.kt — Real HTTP/HTTPS fetcher using Android's native networking.
//
// The C++ fetcher (fetcher.cpp) is compiled with ALI_DISABLE_OPENSSL on Android,
// so it returns NetworkError for all requests. This Kotlin class provides the
// actual network I/O using HttpsURLConnection — Android's built-in TLS stack.
//
// Design:
//   - Runs on Dispatchers.IO only — never called from the main thread.
//   - Hard-capped response size (2 MB default).
//   - Follows redirects up to maxRedirects; refuses HTTPS→HTTP downgrades.
//   - Enforces connect and read timeouts.
//   - Validates content-type before buffering the response body.
//   - Blocks private/loopback/link-local addresses (SSRF defence).
//   - Maps HTTP status codes to typed results (429→RateLimited, 403→Restricted, etc.)
//   - Does NOT bypass robots.txt, CAPTCHAs or access controls.
//
// The result is a FetchResult data class that mirrors the C++ FetchResultCode
// enum, keeping both layers in sync.

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.URL
import javax.net.ssl.HttpsURLConnection

// ── Result types (mirror C++ FetchResultCode) ─────────────────────────────────

enum class FetchCode {
    OK,
    TIMEOUT,
    TOO_LARGE,
    TOO_MANY_REDIRECTS,
    NETWORK_ERROR,
    BLOCKED_ADDRESS,   // private/loopback/link-local
    HTTP_ERROR,        // 4xx/5xx other than handled codes
    RATE_LIMITED,      // 429
    RESTRICTED,        // 403
    AUTH_REQUIRED,     // 401/407
    UNSUPPORTED_CONTENT, // non-text content-type
    NO_NETWORK,
}

data class FetchResult(
    val code:        FetchCode,
    val httpStatus:  Int    = 0,
    val finalUrl:    String = "",
    val contentType: String = "",
    val body:        ByteArray = ByteArray(0),
    val errorDetail: String = "",
) {
    val isSuccess get() = code == FetchCode.OK
}

// ── SSRF defence ──────────────────────────────────────────────────────────────

private fun isBlockedAddress(host: String): Boolean {
    if (host.isBlank()) return true
    return try {
        val addr = InetAddress.getByName(host)
        addr.isLoopbackAddress ||
        addr.isLinkLocalAddress ||
        addr.isSiteLocalAddress ||    // RFC1918: 10.x, 172.16-31.x, 192.168.x
        addr.isAnyLocalAddress ||
        addr.isMulticastAddress ||
        host == "metadata.google.internal" ||
        host == "169.254.169.254"
    } catch (e: Exception) {
        false // DNS failure — let the connection attempt fail naturally
    }
}

// ── Fetcher ───────────────────────────────────────────────────────────────────

object AliAndroidFetcher {

    private const val MAX_RESPONSE_BYTES = 2 * 1024 * 1024  // 2 MB
    private const val CONNECT_TIMEOUT_MS = 10_000
    private const val READ_TIMEOUT_MS    = 10_000
    private const val MAX_REDIRECTS      = 5
    private const val USER_AGENT         = "ALIBot/0.1 (local search; +https://github.com/basmoyy/ALI-Search-Engine)"

    fun isNetworkAvailable(context: Context): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val cap = cm.getNetworkCapabilities(cm.activeNetwork) ?: return false
        return cap.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
               cap.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
    }

    fun fetch(urlString: String, context: Context? = null): FetchResult {
        if (context != null && !isNetworkAvailable(context)) {
            return FetchResult(FetchCode.NO_NETWORK, errorDetail = "No network connection")
        }

        var currentUrl = urlString.trim()
        var redirectCount = 0

        while (redirectCount <= MAX_REDIRECTS) {
            val result = fetchOnce(currentUrl)

            // Follow redirects manually so we can enforce HTTPS→HTTP downgrade refusal.
            if (result.httpStatus in 300..399 && result.finalUrl.isNotEmpty()) {
                if (currentUrl.startsWith("https://") &&
                    result.finalUrl.startsWith("http://")) {
                    return FetchResult(
                        FetchCode.TOO_MANY_REDIRECTS,
                        errorDetail = "HTTPS to HTTP downgrade refused"
                    )
                }
                currentUrl = result.finalUrl
                redirectCount++
                continue
            }
            return result
        }
        return FetchResult(FetchCode.TOO_MANY_REDIRECTS, errorDetail = "Exceeded $MAX_REDIRECTS redirects")
    }

    private fun fetchOnce(urlString: String): FetchResult {
        var conn: HttpURLConnection? = null
        return try {
            val url = URL(urlString)

            // SSRF defence — resolve host and check address range.
            if (isBlockedAddress(url.host)) {
                return FetchResult(
                    FetchCode.BLOCKED_ADDRESS,
                    errorDetail = "Blocked address: ${url.host}"
                )
            }

            conn = url.openConnection() as HttpURLConnection
            conn.instanceFollowRedirects = false  // we handle redirects ourselves
            conn.connectTimeout = CONNECT_TIMEOUT_MS
            conn.readTimeout    = READ_TIMEOUT_MS
            conn.setRequestProperty("User-Agent", USER_AGENT)
            conn.setRequestProperty("Accept", "text/html,text/plain;q=0.9,*/*;q=0.8")
            conn.setRequestProperty("Accept-Encoding", "identity") // no compression
            conn.requestMethod = "GET"

            val status      = conn.responseCode
            val contentType = conn.contentType ?: ""
            val location    = conn.getHeaderField("Location") ?: ""

            // Map status codes to typed results.
            when {
                status == 429 -> return FetchResult(FetchCode.RATE_LIMITED, status, urlString)
                status == 403 -> return FetchResult(FetchCode.RESTRICTED,   status, urlString)
                status == 401 || status == 407 ->
                    return FetchResult(FetchCode.AUTH_REQUIRED, status, urlString)
                status in 300..399 ->
                    return FetchResult(FetchCode.OK, status, location)
                status < 200 || status >= 300 ->
                    return FetchResult(FetchCode.HTTP_ERROR, status, urlString,
                                        errorDetail = "HTTP $status")
            }

            // Validate content-type — only index text content.
            val lowerCt = contentType.lowercase()
            if (!lowerCt.contains("text/") &&
                !lowerCt.contains("application/xhtml")) {
                return FetchResult(
                    FetchCode.UNSUPPORTED_CONTENT,
                    status, urlString, contentType,
                    errorDetail = "Non-text content-type: $contentType"
                )
            }

            // Read body with hard size cap.
            val buffer = ByteArrayOutputStream()
            conn.inputStream.use { input ->
                val chunk = ByteArray(8192)
                var read: Int
                var total = 0
                while (input.read(chunk).also { read = it } != -1) {
                    buffer.write(chunk, 0, read)
                    total += read
                    if (total >= MAX_RESPONSE_BYTES) {
                        // Body truncated — still usable for indexing.
                        return@use
                    }
                }
            }

            FetchResult(
                code        = FetchCode.OK,
                httpStatus  = status,
                finalUrl    = urlString,
                contentType = contentType,
                body        = buffer.toByteArray(),
            )

        } catch (e: java.net.SocketTimeoutException) {
            FetchResult(FetchCode.TIMEOUT, errorDetail = "Timeout: ${e.message}")
        } catch (e: Exception) {
            FetchResult(FetchCode.NETWORK_ERROR, errorDetail = e.message ?: "Unknown error")
        } finally {
            conn?.disconnect()
        }
    }
}
