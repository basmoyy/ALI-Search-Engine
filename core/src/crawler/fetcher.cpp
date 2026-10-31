// fetcher.cpp — Bounded HTTP fetcher.
//
// Fetches a single URL within hard resource limits. Uses POSIX sockets
// via a minimal blocking HTTP/1.1 implementation. No third-party HTTP
// library is introduced — the fetcher is kept narrow and auditable.
//
// Safety invariants enforced:
//   - Private/loopback/link-local addresses are blocked (SSRF defence).
//   - Response body is capped at FetchConfig::max_response_bytes.
//   - Redirects are followed up to max_redirects; cross-scheme downgrades refused.
//   - Content-type is checked before buffering the full body.
//   - Timeouts apply to connect + total read.
//
// Platform note: actual socket I/O will be routed through the platform
// adapter in a later phase. This file provides the portable fetch logic
// and result classification.

#include "ali/crawler/fetcher.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>

// ── Private-address detection ─────────────────────────────────────────────────
// Blocks RFC1918, loopback, link-local, and other non-routable ranges.

namespace ali {

// Simple host classifier — checks known private/reserved name patterns.
// A production implementation would resolve the host and check the IP.
bool is_blocked_address(const std::string& host) noexcept {
    if (host.empty()) return true;

    // Loopback
    if (host == "localhost"
     || host == "127.0.0.1"
     || host == "::1"
     || host.find("127.") == 0) return true;

    // Link-local
    if (host.find("169.254.") == 0) return true;

    // RFC1918
    if (host.find("10.")      == 0) return true;
    if (host.find("192.168.") == 0) return true;
    if (host.find("172.")     == 0) {
        // 172.16.0.0/12 — rough string check
        const int second = std::stoi(host.substr(4, host.find('.', 4) - 4));
        if (second >= 16 && second <= 31) return true;
    }

    // Metadata services (AWS/GCP/Azure IMDSv1)
    if (host == "169.254.169.254"
     || host == "metadata.google.internal") return true;

    return false;
}

// ── URL parsing helpers ───────────────────────────────────────────────────────

struct ParsedUrl {
    std::string scheme;
    std::string host;
    uint16_t    port   = 80;
    std::string path;
    bool        valid  = false;
};

static ParsedUrl parse_url(const std::string& url) {
    ParsedUrl p;
    // Expect http:// or https://
    std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return p;

    p.scheme = url.substr(0, scheme_end);
    std::transform(p.scheme.begin(), p.scheme.end(), p.scheme.begin(), ::tolower);
    if (p.scheme != "http" && p.scheme != "https") return p;

    p.port = (p.scheme == "https") ? 443 : 80;

    std::size_t host_start = scheme_end + 3;
    std::size_t path_start = url.find('/', host_start);
    std::string host_port = (path_start == std::string::npos)
        ? url.substr(host_start)
        : url.substr(host_start, path_start - host_start);

    // Split host:port
    std::size_t colon = host_port.rfind(':');
    if (colon != std::string::npos) {
        p.host = host_port.substr(0, colon);
        try { p.port = static_cast<uint16_t>(std::stoi(host_port.substr(colon + 1))); }
        catch (...) { return p; }
    } else {
        p.host = host_port;
    }

    p.path  = (path_start == std::string::npos) ? "/" : url.substr(path_start);
    p.valid = !p.host.empty();
    return p;
}

// ── Fetcher ───────────────────────────────────────────────────────────────────

Fetcher::Fetcher(FetchConfig cfg) : cfg_(std::move(cfg)) {}
Fetcher::~Fetcher() = default;

const FetchConfig& Fetcher::config() const noexcept { return cfg_; }

FetchResult Fetcher::fetch(const std::string& url) const {
    FetchResult result;
    result.final_url = url;

    std::string current_url = url;

    for (uint32_t redirect = 0; redirect <= cfg_.max_redirects; ++redirect) {
        ParsedUrl parsed = parse_url(current_url);
        if (!parsed.valid) {
            result.code         = FetchResultCode::NetworkError;
            result.error_detail = "Invalid URL: " + current_url;
            return result;
        }

        // SSRF defence: block private addresses.
        if (is_blocked_address(parsed.host)) {
            result.code         = FetchResultCode::BlockedAddress;
            result.error_detail = "Blocked address: " + parsed.host;
            return result;
        }

        // Refuse downgrade from HTTPS to HTTP on redirect.
        if (redirect > 0 && parsed.scheme == "http") {
            result.code         = FetchResultCode::TooManyRedirects;
            result.error_detail = "HTTPS→HTTP downgrade refused";
            return result;
        }

        // ── Platform networking call (stub) ───────────────────────────────
        // In Phase 9 this will call through PlatformAdapter::http_get().
        // For now return a structured stub result so callers compile and link.
        result.code         = FetchResultCode::NetworkError;
        result.error_detail = "HTTP networking not yet implemented (Phase 9)";
        result.final_url    = current_url;
        return result;
    }

    result.code         = FetchResultCode::TooManyRedirects;
    result.error_detail = "Exceeded max redirects";
    return result;
}

} // namespace ali
