#pragma once
// ali/storage/metadata_db.h — SQLite-backed structured metadata store.
//
// Stores URL records, document crawl state, robots rules, host scheduling,
// tombstones and locality statistics. The inverted index itself lives in
// binary segment files, not here.
//
// URL lifecycle states (v0.3 — access-aware crawler extension):
//
//   DISCOVERED            → known but not yet scheduled
//   QUEUED                → eligible; waiting for a fetch attempt
//   FETCHING              → actively being retrieved (transient; not persisted long)
//   FETCHED               → response received; content processing may still fail
//   INDEXED               → successfully processed and published into the index
//   RESTRICTED            → robots rules, policy, or access controls blocked retrieval
//   AUTH_REQUIRED         → content requires authentication or an authorised session
//   RATE_LIMITED          → host requested reduced frequency; awaiting backoff
//   TEMPORARILY_UNAVAILABLE → transient network, server, DNS, or service failure
//   STALE                 → previously indexed; may need refresh
//   REMOVED               → deleted, withdrawn, or intentionally removed from ALI
//
// A blocked or unindexed URL is never equivalent to a nonexistent URL.
// Do not retry RESTRICTED or AUTH_REQUIRED URLs aggressively.

#include "ali/storage/types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ali {

/// Full URL lifecycle state as specified in ALI Architecture Brief v0.3 §5.
enum class CrawlStatus : uint8_t {
    Discovered            = 0,   ///< Known; not yet scheduled.
    Queued                = 1,   ///< Eligible; waiting for a fetch attempt.
    Fetching              = 2,   ///< Actively being retrieved (short-lived).
    Fetched               = 3,   ///< Response received; processing may still fail.
    Indexed               = 4,   ///< Content published into the searchable index.
    Restricted            = 5,   ///< Robots/policy/access controls blocked retrieval.
    AuthRequired          = 6,   ///< Content requires authentication.
    RateLimited           = 7,   ///< Host requested reduced frequency; in backoff.
    TemporarilyUnavailable = 8,  ///< Transient network/server/DNS failure.
    Stale                 = 9,   ///< Previously indexed; may need refresh.
    Removed               = 10,  ///< Deleted, withdrawn, or removed from ALI.
};

/// Returns true if the status represents a terminal non-retryable state.
/// Callers must not schedule aggressive retries for these statuses.
[[nodiscard]] constexpr bool is_terminal_status(CrawlStatus s) noexcept {
    return s == CrawlStatus::Restricted
        || s == CrawlStatus::AuthRequired
        || s == CrawlStatus::Removed;
}

/// Returns true if the status represents a retryable transient failure.
[[nodiscard]] constexpr bool is_retryable_status(CrawlStatus s) noexcept {
    return s == CrawlStatus::RateLimited
        || s == CrawlStatus::TemporarilyUnavailable;
}

struct UrlRecord {
    uint64_t    url_id          = 0;
    std::string url;
    std::string canonical_url;
    DocId       doc_id          = kInvalidDocId;
    CrawlStatus status          = CrawlStatus::Discovered;
    int64_t     last_fetched_at = 0;   ///< unix seconds; 0 = never
    int64_t     next_fetch_at   = 0;   ///< 0 = eligible immediately
    uint32_t    fetch_failures  = 0;
    uint32_t    content_hash    = 0;   ///< cheap fingerprint for dedup
    std::string access_note;           ///< optional: reason for Restricted/AuthRequired/etc.
    uint32_t    crawl_depth     = 0;   ///< hop distance from seed (0 = seed itself)
};

struct RobotsRecord {
    std::string host;
    std::string rules_text;
    int64_t     fetched_at    = 0;
    int64_t     expires_at    = 0;
};

/// Structured metadata store backed by SQLite (WAL mode).
class MetadataDb {
public:
    explicit MetadataDb(std::filesystem::path db_path);
    ~MetadataDb();

    MetadataDb(const MetadataDb&)            = delete;
    MetadataDb& operator=(const MetadataDb&) = delete;

    [[nodiscard]] bool open();
    void               close();

    // ── URL / document records ───────────────────────────────────────────────

    [[nodiscard]] bool upsert_url(const UrlRecord& rec);
    [[nodiscard]] std::optional<UrlRecord> find_url(const std::string& url) const;
    [[nodiscard]] std::optional<UrlRecord> find_by_doc_id(DocId doc_id) const;

    /// Update the status (and optional access_note) of a URL record by URL.
    [[nodiscard]] bool update_status(const std::string& url,
                                      CrawlStatus         new_status,
                                      const std::string&  access_note = {});

    /// Mark a document as Removed in the URL table. Posting lists remain
    /// until the next compaction removes the owning segment.
    [[nodiscard]] bool remove_doc(DocId doc_id);

    /// Legacy alias kept for internal callers; delegates to remove_doc().
    [[nodiscard]] bool tombstone_doc(DocId doc_id);

    /// Find all URLs with a given status (up to limit).
    [[nodiscard]] std::vector<UrlRecord>
        find_by_status(CrawlStatus status, uint32_t limit = 256) const;

    // ── Robots rules ─────────────────────────────────────────────────────────

    [[nodiscard]] bool upsert_robots(const RobotsRecord& rec);
    [[nodiscard]] std::optional<RobotsRecord> find_robots(const std::string& host) const;

    // ── Crawl frontier ───────────────────────────────────────────────────────

    /// Return up to `limit` URLs eligible for crawling:
    ///   status IN (Discovered, Queued, Stale, RateLimited, TemporarilyUnavailable)
    ///   AND next_fetch_at <= now_unix
    ///   AND NOT is_terminal_status(status)
    [[nodiscard]] std::vector<UrlRecord>
        pop_crawl_queue(uint32_t limit, int64_t now_unix) const;

    // ── Transactions ─────────────────────────────────────────────────────────

    [[nodiscard]] bool begin_transaction();
    [[nodiscard]] bool commit_transaction();
    [[nodiscard]] bool rollback_transaction();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
