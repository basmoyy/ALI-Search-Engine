// crawler.cpp — Resource-aware crawler stub.
// Full implementation deferred to Phase 8. Structure and public API are stable.

#include "ali/crawler/crawler.h"

namespace ali {

struct Crawler::Impl {
    CrawlerConfig       cfg;
    MetadataDb&         db;
    IndexWriter&        writer;
    ResourceController& resources;
    CrawlerState        state_   = CrawlerState::Idle;
    Stats               stats_;
};

Crawler::Crawler(CrawlerConfig cfg, MetadataDb& db, IndexWriter& writer,
                 ResourceController& resources)
    : impl_(new Impl{std::move(cfg), db, writer, resources}) {}
Crawler::~Crawler() { stop(); }

void Crawler::start() { impl_->state_ = CrawlerState::Running; }
void Crawler::pause() { impl_->state_ = CrawlerState::Paused; }
void Crawler::stop()  { impl_->state_ = CrawlerState::Stopping; }

CrawlerState Crawler::state() const noexcept { return impl_->state_; }

void Crawler::enqueue_urls(const std::vector<std::string>& urls) {
    for (const auto& url : urls) {
        UrlRecord rec;
        rec.url = rec.canonical_url = url;
        rec.status = CrawlStatus::Discovered;
        impl_->db.upsert_url(rec);
    }
}

Crawler::Stats Crawler::stats() const noexcept { return impl_->stats_; }

} // namespace ali
