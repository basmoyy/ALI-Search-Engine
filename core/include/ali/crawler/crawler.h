#pragma once
// ali/crawler/crawler.h — Resource-aware, polite, resumable web crawler.
//
// The crawler is entirely independent of the query path. It must never
// block interactive search. All work is resumable: the process may be
// killed at any point and crawling resumes from a consistent checkpoint.
//
// Politeness rules:
//   - Respect robots.txt for every host.
//   - Enforce per-host request rate limits.
//   - Respect retry backoff after fetch failures.
//   - Enforce maximum response size and redirect depth.

#include "ali/crawler/resource_controller.h"
#include "ali/storage/metadata_db.h"
#include "ali/index/index_writer.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace ali {

struct CrawlerConfig {
    uint32_t max_concurrent_fetches  = 2;
    uint32_t fetch_timeout_ms        = 10000;
    uint32_t max_response_bytes      = 2 * 1024 * 1024;   ///< 2 MB per page
    uint32_t max_redirects           = 5;
    uint32_t per_host_delay_ms       = 1000;   ///< politeness delay between requests
    uint32_t max_extract_text_bytes  = 256 * 1024;  ///< extracted plain-text cap
    uint32_t discovery_depth         = 3;      ///< link-follow depth from seed URLs
    bool     respect_robots_txt      = true;
    bool     allow_metered_network   = false;  ///< require user opt-in
    std::vector<std::string> seed_urls;
    std::vector<std::string> allowed_domains;  ///< empty = no domain restriction
};

/// Crawler lifecycle state.
enum class CrawlerState : uint8_t {
    Idle     = 0,
    Running  = 1,
    Paused   = 2,
    Stopping = 3,
};

class Crawler {
public:
    explicit Crawler(CrawlerConfig     cfg,
                     MetadataDb&       db,
                     IndexWriter&      writer,
                     ResourceController& resources);
    ~Crawler();

    Crawler(const Crawler&)            = delete;
    Crawler& operator=(const Crawler&) = delete;

    /// Start a crawl cycle. Returns immediately; work runs on internal threads.
    void start();

    /// Gracefully pause (completes in-flight requests).
    void pause();

    /// Stop and wait for all threads to exit.
    void stop();

    [[nodiscard]] CrawlerState state() const noexcept;

    /// Enqueue additional seed URLs at runtime.
    void enqueue_urls(const std::vector<std::string>& urls);

    struct Stats {
        uint64_t pages_fetched    = 0;
        uint64_t pages_indexed    = 0;
        uint64_t fetch_failures   = 0;
        uint64_t robots_rejected  = 0;
        uint64_t duplicates_skipped = 0;
    };
    [[nodiscard]] Stats stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
