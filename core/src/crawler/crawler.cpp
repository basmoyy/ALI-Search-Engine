// crawler.cpp — Resource-aware, polite, resumable web crawler.
//
// Connects the full pipeline: Scheduler → Fetcher → HtmlParser →
// Deduplicator → SegmentBuilder → IndexWriter → Manifest.
//
// Design guarantees:
//   - start() returns immediately; all work runs on a single background
//     thread (conservative default for low-end devices; concurrency is
//     controlled by CrawlerConfig::max_concurrent_fetches which the
//     scheduler honours in future phases).
//   - stop() blocks until the background thread exits cleanly.
//   - The crawl thread checks the cancel flag before every fetch and
//     between every stage. It cannot stall the interactive query path.
//   - Per-host politeness delay (default 1 s) is enforced between requests
//     to the same host.
//   - Robots.txt rules are fetched and cached per host. A missing or
//     unparseable robots.txt is treated as "allow all".
//   - All frontier state lives in SQLite — killing the process at any
//     point leaves the index in a consistent, reopenable state.

#include "ali/crawler/crawler.h"
#include "ali/crawler/url_frontier.h"
#include "ali/crawler/scheduler.h"
#include "ali/crawler/fetcher.h"
#include "ali/crawler/html_parser.h"
#include "ali/crawler/deduplicator.h"
#include "ali/crawler/segment_builder.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace ali {

// ── Robots.txt cache ──────────────────────────────────────────────────────────
// Very simple line-by-line robots.txt parser. Only honours User-agent: *
// and Disallow: directives. Fetched once per host per session.

struct RobotsEntry {
    std::vector<std::string> disallowed_prefixes;
    int64_t fetched_at = 0;
};

static bool robots_allows(const RobotsEntry& entry, const std::string& path) {
    for (const auto& prefix : entry.disallowed_prefixes) {
        if (prefix.empty()) continue;
        if (path.rfind(prefix, 0) == 0) return false;
    }
    return true;
}

static RobotsEntry parse_robots_txt(const std::vector<uint8_t>& body) {
    RobotsEntry entry;
    bool in_our_agent = false;
    std::string text(reinterpret_cast<const char*>(body.data()), body.size());
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string lower;
        lower.resize(line.size());
        std::transform(line.begin(), line.end(), lower.begin(), ::tolower);

        if (lower.rfind("user-agent:", 0) == 0) {
            auto val = line.substr(11);
            auto s = val.find_first_not_of(" \t");
            if (s != std::string::npos) val = val.substr(s);
            in_our_agent = (val == "*" || val.rfind("alibot", 0) == 0);
        } else if (in_our_agent && lower.rfind("disallow:", 0) == 0) {
            auto val = line.substr(9);
            auto s = val.find_first_not_of(" \t");
            if (s != std::string::npos) val = val.substr(s);
            if (!val.empty()) entry.disallowed_prefixes.push_back(val);
        }
    }
    return entry;
}

static std::string host_of(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return {};
    auto host_start = scheme_end + 3;
    auto path_start = url.find('/', host_start);
    auto host = (path_start == std::string::npos)
        ? url.substr(host_start) : url.substr(host_start, path_start - host_start);
    auto colon = host.rfind(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    return host;
}

static std::string path_of(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return "/";
    auto host_start = scheme_end + 3;
    auto path_start = url.find('/', host_start);
    return (path_start == std::string::npos) ? "/" : url.substr(path_start);
}

// ── Crawler::Impl ─────────────────────────────────────────────────────────────

struct Crawler::Impl {
    CrawlerConfig       cfg;
    MetadataDb&         db;
    IndexWriter&        writer;
    ResourceController& resources;

    std::atomic<CrawlerState> state_{CrawlerState::Idle};
    std::atomic<bool>         cancel_flag_{false};
    std::thread               worker_thread_;

    mutable std::mutex        stats_mtx_;
    Crawler::Stats            stats_{};

    // Per-host robots cache and last-request timestamps (session-only).
    std::mutex                robots_mtx_;
    std::unordered_map<std::string, RobotsEntry> robots_cache_;

    std::mutex                host_delay_mtx_;
    std::unordered_map<std::string, int64_t> last_fetch_ms_;

    explicit Impl(CrawlerConfig c, MetadataDb& d, IndexWriter& w, ResourceController& r)
        : cfg(std::move(c)), db(d), writer(w), resources(r) {}

    // ── Robots helpers ────────────────────────────────────────────────────────

    bool check_robots(const std::string& url, const Fetcher& fetcher) {
        const std::string host = host_of(url);
        const std::string path = path_of(url);
        if (host.empty()) return true;

        {
            std::lock_guard lock(robots_mtx_);
            auto it = robots_cache_.find(host);
            if (it != robots_cache_.end()) {
                return robots_allows(it->second, path);
            }
        }

        // Fetch robots.txt for this host (not counted in politeness delay).
        std::string scheme = url.substr(0, url.find("://"));
        std::string robots_url = scheme + "://" + host + "/robots.txt";

        FetchConfig rcfg;
        rcfg.timeout_ms = 5000;
        rcfg.max_response_bytes = 64 * 1024;
        rcfg.user_agent = cfg.respect_robots_txt ? fetcher.config().user_agent : "";
        Fetcher rf(rcfg);
        auto r = rf.fetch(robots_url);

        RobotsEntry entry;
        if (r.code == FetchResultCode::Ok && !r.body.empty()) {
            entry = parse_robots_txt(r.body);
        }
        // Cache regardless of fetch result (treat failure as allow-all).
        entry.fetched_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        {
            std::lock_guard lock(robots_mtx_);
            robots_cache_[host] = entry;
        }
        return robots_allows(entry, path);
    }

    // ── Host politeness delay ─────────────────────────────────────────────────

    void enforce_host_delay(const std::string& host) {
        const int64_t delay_ms = static_cast<int64_t>(cfg.per_host_delay_ms);
        int64_t wait_ms = 0;
        {
            std::lock_guard lock(host_delay_mtx_);
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto it = last_fetch_ms_.find(host);
            if (it != last_fetch_ms_.end()) {
                wait_ms = std::max(int64_t{0}, delay_ms - (now - it->second));
            }
        }
        if (wait_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }
        {
            std::lock_guard lock(host_delay_mtx_);
            last_fetch_ms_[host] =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    // ── Main crawl loop ───────────────────────────────────────────────────────

    void run() {
        UrlFrontier frontier(db);
        for (const auto& url : cfg.seed_urls) frontier.enqueue(url);

        FetchConfig fcfg;
        fcfg.timeout_ms         = cfg.fetch_timeout_ms;
        fcfg.max_response_bytes = cfg.max_response_bytes;
        fcfg.max_redirects      = cfg.max_redirects;
        Fetcher fetcher(fcfg);

        HtmlParser parser;
        Deduplicator dedup(db);
        SegmentBuilder seg_builder(writer, dedup);

        SchedulerConfig scfg;
        scfg.batch_size     = 5;
        scfg.idle_sleep_ms  = 500;
        Scheduler scheduler(frontier, resources, scfg);

        auto now_unix = []() -> int64_t {
            return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        };

        while (state_.load() == CrawlerState::Running && !cancel_flag_.load()) {
            scheduler.run_cycle(
                [&](const UrlRecord& rec) -> bool {
                    if (cancel_flag_.load()) return false;
                    if (state_.load() != CrawlerState::Running) return false;

                    const std::string& url = rec.url;
                    const std::string  host = host_of(url);

                    // Domain filter
                    if (!cfg.allowed_domains.empty()) {
                        bool allowed = false;
                        for (const auto& d : cfg.allowed_domains) {
                            if (host.find(d) != std::string::npos) { allowed = true; break; }
                        }
                        if (!allowed) {
                            frontier.mark_access_failure(
                                url, CrawlStatus::Restricted, "domain not in allowlist", now_unix());
                            std::lock_guard lk(stats_mtx_);
                            ++stats_.robots_rejected;
                            return true;
                        }
                    }

                    // Robots.txt check
                    if (cfg.respect_robots_txt && !check_robots(url, fetcher)) {
                        frontier.mark_access_failure(
                            url, CrawlStatus::Restricted, "robots.txt disallow", now_unix());
                        std::lock_guard lk(stats_mtx_);
                        ++stats_.robots_rejected;
                        return true;
                    }

                    // Per-host politeness delay
                    enforce_host_delay(host);

                    // Fetch
                    frontier.mark_fetched(url, now_unix());
                    auto result = fetcher.fetch(url);

                    {
                        std::lock_guard lk(stats_mtx_);
                        ++stats_.pages_fetched;
                    }

                    // Map HTTP result to frontier state
                    if (result.code == FetchResultCode::RateLimited) {
                        frontier.mark_access_failure(url, CrawlStatus::RateLimited,
                            "HTTP 429", now_unix());
                        return true;
                    }
                    if (result.code == FetchResultCode::Restricted) {
                        frontier.mark_access_failure(url, CrawlStatus::Restricted,
                            "HTTP 403", now_unix());
                        std::lock_guard lk(stats_mtx_); ++stats_.robots_rejected;
                        return true;
                    }
                    if (result.code == FetchResultCode::AuthRequired) {
                        frontier.mark_access_failure(url, CrawlStatus::AuthRequired,
                            "HTTP 401/407", now_unix());
                        return true;
                    }
                    if (result.code != FetchResultCode::Ok
                     && result.code != FetchResultCode::TooLarge) {
                        frontier.mark_transient_failure(url, now_unix());
                        std::lock_guard lk(stats_mtx_); ++stats_.fetch_failures;
                        return true;
                    }

                    // Parse
                    auto parsed = parser.parse(result.body,
                        result.final_url.empty() ? url : result.final_url);
                    if (parsed.parse_error) {
                        frontier.mark_transient_failure(url, now_unix());
                        return true;
                    }

                    // Fingerprint and dedup
                    const uint32_t hash = Deduplicator::fingerprint(parsed.body_text);
                    if (dedup.is_duplicate(parsed.canonical_url, hash)) {
                        frontier.mark_indexed(url,
                            rec.doc_id == kInvalidDocId ? 0 : rec.doc_id, hash);
                        std::lock_guard lk(stats_mtx_); ++stats_.duplicates_skipped;
                        return true;
                    }

                    // Index
                    const bool accepted = seg_builder.submit(
                        parsed.canonical_url, parsed.title, parsed.body_text, hash);

                    if (accepted) {
                        frontier.mark_indexed(url, kInvalidDocId, hash);
                        std::lock_guard lk(stats_mtx_); ++stats_.pages_indexed;
                    } else {
                        frontier.mark_transient_failure(url, now_unix());
                    }

                    // Enqueue discovered links (bounded by discovery depth)
                    if (rec.fetch_failures < cfg.discovery_depth) {
                        frontier.enqueue_discovered(parsed.links);
                    }

                    return true;
                },
                now_unix(),
                cancel_flag_
            );
        }

        // Final flush when stopping cleanly.
        seg_builder.flush();
        state_.store(CrawlerState::Idle);
    }
};

// ── Public API ────────────────────────────────────────────────────────────────

Crawler::Crawler(CrawlerConfig cfg, MetadataDb& db, IndexWriter& writer,
                 ResourceController& resources)
    : impl_(new Impl(std::move(cfg), db, writer, resources)) {}

Crawler::~Crawler() { stop(); }

void Crawler::start() {
    CrawlerState expected = CrawlerState::Idle;
    if (!impl_->state_.compare_exchange_strong(expected, CrawlerState::Running))
        return; // already running or stopping

    impl_->cancel_flag_.store(false);
    impl_->worker_thread_ = std::thread([this]() { impl_->run(); });
}

void Crawler::pause() {
    impl_->state_.store(CrawlerState::Paused);
}

void Crawler::stop() {
    impl_->state_.store(CrawlerState::Stopping);
    impl_->cancel_flag_.store(true);
    if (impl_->worker_thread_.joinable()) {
        impl_->worker_thread_.join();
    }
}

CrawlerState Crawler::state() const noexcept {
    return impl_->state_.load();
}

void Crawler::enqueue_urls(const std::vector<std::string>& urls) {
    UrlFrontier frontier(impl_->db);
    for (const auto& url : urls) frontier.enqueue(url);
}

Crawler::Stats Crawler::stats() const noexcept {
    std::lock_guard lk(impl_->stats_mtx_);
    return impl_->stats_;
}

} // namespace ali
