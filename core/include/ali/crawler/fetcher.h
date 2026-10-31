#pragma once
// ali/crawler/fetcher.h — Bounded HTTP fetcher.
//
// Fetches a single URL within hard limits. Uses the platform adapter for
// networking — no direct socket code in the portable core.
//
// Safety rules enforced here:
//   - Max response size (default 2 MB)
//   - Max redirects (default 5)
//   - Timeout (default 10 s)
//   - No private-network / loopback / link-local addresses (SSRF defence)
//   - Content-type validation before full body buffering

#include "ali/crawler/resource_controller.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ali {

struct FetchConfig {
    uint32_t max_response_bytes = 2 * 1024 * 1024;  ///< 2 MB
    uint32_t timeout_ms         = 10000;
    uint32_t max_redirects      = 5;
    std::string user_agent      = "ALIBot/0.1 (local search; no-index)";
};

enum class FetchResultCode : uint8_t {
    Ok                  = 0,
    Timeout             = 1,
    TooLarge            = 2,
    TooManyRedirects    = 3,
    NetworkError        = 4,
    BlockedAddress      = 5,   ///< private/loopback/link-local blocked
    HttpError           = 6,   ///< 4xx / 5xx
    RateLimited         = 7,   ///< 429
    Restricted          = 8,   ///< 403 or robots-blocked
    AuthRequired        = 9,   ///< 401 / 407
    UnsupportedContent  = 10,  ///< non-text content type
};

struct FetchResult {
    FetchResultCode     code        = FetchResultCode::NetworkError;
    int                 http_status = 0;
    std::string         final_url;           ///< after redirects
    std::string         content_type;
    std::vector<uint8_t> body;               ///< raw bytes, bounded by max_response_bytes
    std::string         error_detail;
};

/// Synchronous single-URL fetcher. Intended to be called from a crawler
/// worker thread — not the interactive query path.
class Fetcher {
public:
    explicit Fetcher(FetchConfig cfg = {});
    ~Fetcher();

    /// Fetch a URL. Blocks until complete, timeout, or error.
    /// Never fetches private-network addresses.
    [[nodiscard]] FetchResult fetch(const std::string& url) const;

    [[nodiscard]] const FetchConfig& config() const noexcept;

private:
    FetchConfig cfg_;
};

/// Returns true if the URL resolves to a private, loopback, or link-local
/// address that must never be fetched (SSRF defence).
[[nodiscard]] bool is_blocked_address(const std::string& host) noexcept;

} // namespace ali
