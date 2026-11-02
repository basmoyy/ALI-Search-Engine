#pragma once
// ali/crawler/url_frontier.h — Persistent URL frontier.
//
// Wraps MetadataDb to provide a priority-aware crawl queue.
// All state persists in SQLite so crawl progress survives process death.

#include "ali/storage/metadata_db.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ali {

struct FrontierConfig {
    uint32_t max_queue_size      = 100000; ///< soft cap on Discovered/Queued entries
    uint32_t min_host_delay_ms   = 1000;   ///< minimum politeness gap per host
    uint32_t max_fetch_failures  = 5;      ///< before marking TemporarilyUnavailable
    uint32_t rate_limit_delay_s  = 300;    ///< retry delay after RateLimited
    uint32_t transient_delay_s   = 60;     ///< retry delay after TemporarilyUnavailable
};

/// Priority-aware URL frontier backed by MetadataDb.
class UrlFrontier {
public:
    explicit UrlFrontier(MetadataDb& db, FrontierConfig cfg = {});

    /// Enqueue a URL if it has not been seen before.
    /// Returns true if the URL was newly added.
    bool enqueue(const std::string& url,
                 const std::string& canonical_url = {},
                 uint32_t           crawl_depth   = 0);

    /// Bulk enqueue discovered outbound links. Skips duplicates silently.
    void enqueue_discovered(const std::vector<std::string>& urls,
                             uint32_t                         crawl_depth = 0);

    /// Return up to `limit` URLs eligible for crawling right now.
    std::vector<UrlRecord> pop(uint32_t limit, int64_t now_unix);

    /// Mark a URL as successfully fetched (moves to Fetched).
    void mark_fetched(const std::string& url, int64_t now_unix);

    /// Mark a URL as indexed (moves to Indexed).
    void mark_indexed(const std::string& url, DocId doc_id, uint32_t content_hash);

    /// Mark a URL with an access failure (Restricted, AuthRequired, RateLimited, etc.).
    void mark_access_failure(const std::string& url,
                              CrawlStatus         status,
                              const std::string&  note,
                              int64_t             now_unix);

    /// Mark a transient failure and schedule a retry with backoff.
    void mark_transient_failure(const std::string& url, int64_t now_unix);

    /// Mark a URL as stale so it will be recrawled.
    void mark_stale(const std::string& url);

    [[nodiscard]] uint32_t pending_count() const;

private:
    MetadataDb&   db_;
    FrontierConfig cfg_;
};

} // namespace ali
