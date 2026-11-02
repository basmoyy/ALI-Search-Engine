// url_frontier.cpp — Persistent URL frontier backed by MetadataDb.
//
// All frontier state lives in SQLite so crawl progress survives process death.

#include "ali/crawler/url_frontier.h"

#include <chrono>

namespace ali {

UrlFrontier::UrlFrontier(MetadataDb& db, FrontierConfig cfg)
    : db_(db), cfg_(cfg) {}

bool UrlFrontier::enqueue(const std::string& url,
                           const std::string& canonical_url,
                           uint32_t           crawl_depth) {
    if (db_.find_url(url).has_value()) return false;

    UrlRecord rec;
    rec.url           = url;
    rec.canonical_url = canonical_url.empty() ? url : canonical_url;
    rec.status        = CrawlStatus::Discovered;
    rec.next_fetch_at = 0;
    rec.crawl_depth   = crawl_depth;
    return db_.upsert_url(rec);
}

void UrlFrontier::enqueue_discovered(const std::vector<std::string>& urls,
                                      uint32_t crawl_depth) {
    for (const auto& url : urls) {
        enqueue(url, {}, crawl_depth);
    }
}

std::vector<UrlRecord> UrlFrontier::pop(uint32_t limit, int64_t now_unix) {
    auto records = db_.pop_crawl_queue(limit, now_unix);
    for (const auto& rec : records) {
        if (rec.status == CrawlStatus::Discovered
         || rec.status == CrawlStatus::Stale) {
            (void)db_.update_status(rec.url, CrawlStatus::Queued);
        }
    }
    return records;
}

void UrlFrontier::mark_fetched(const std::string& url, int64_t now_unix) {
    auto rec = db_.find_url(url);
    if (!rec) return;
    rec->status          = CrawlStatus::Fetched;
    rec->last_fetched_at = now_unix;
    (void)db_.upsert_url(*rec);
}

void UrlFrontier::mark_indexed(const std::string& url,
                                DocId              doc_id,
                                uint32_t           content_hash) {
    auto rec = db_.find_url(url);
    if (!rec) return;
    rec->status       = CrawlStatus::Indexed;
    rec->doc_id       = doc_id;
    rec->content_hash = content_hash;
    (void)db_.upsert_url(*rec);
}

void UrlFrontier::mark_access_failure(const std::string& url,
                                       CrawlStatus         status,
                                       const std::string&  note,
                                       int64_t             now_unix) {
    auto rec = db_.find_url(url);
    if (!rec) { (void)db_.update_status(url, status, note); return; }
    rec->status = status;
    if (status == CrawlStatus::RateLimited) {
        const int64_t base_time = (now_unix > 0) ? now_unix
            : std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
        rec->next_fetch_at = base_time + cfg_.rate_limit_delay_s;
    }
    (void)db_.upsert_url(*rec);
}

void UrlFrontier::mark_transient_failure(const std::string& url,
                                          int64_t             now_unix) {
    auto rec = db_.find_url(url);
    if (!rec) return;

    ++rec->fetch_failures;
    if (rec->fetch_failures >= cfg_.max_fetch_failures) {
        rec->status = CrawlStatus::TemporarilyUnavailable;
    }
    const int64_t delay = static_cast<int64_t>(cfg_.transient_delay_s)
                        * (1LL << std::min(rec->fetch_failures, 6u));
    rec->next_fetch_at = now_unix + std::min(delay, static_cast<int64_t>(3600));
    (void)db_.upsert_url(*rec);
}

void UrlFrontier::mark_stale(const std::string& url) {
    (void)db_.update_status(url, CrawlStatus::Stale);
}

uint32_t UrlFrontier::pending_count() const {
    return static_cast<uint32_t>(
        db_.find_by_status(CrawlStatus::Discovered, 100000).size()
      + db_.find_by_status(CrawlStatus::Queued,     100000).size());
}

} // namespace ali
