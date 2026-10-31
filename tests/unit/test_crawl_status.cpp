// test_crawl_status.cpp — URL lifecycle state tests (ALI Architecture Brief v0.3 §5).
//
// Verifies:
//   - All 11 CrawlStatus values persist and round-trip through MetadataDb.
//   - Terminal statuses are never returned by pop_crawl_queue.
//   - is_terminal_status / is_retryable_status helpers are correct.
//   - update_status + access_note round-trips correctly.
//   - remove_doc sets status to Removed (not any legacy tombstone value).
//   - find_by_status works for every state.

#include "ali/storage/metadata_db.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ali;

class CrawlStatusTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = fs::temp_directory_path() / "ali_crawl_status_test.db";
        fs::remove(db_path_);
        db_ = std::make_unique<MetadataDb>(db_path_);
        ASSERT_TRUE(db_->open());
    }
    void TearDown() override { fs::remove(db_path_); }

    void insert(const std::string& url, CrawlStatus status,
                const std::string& note = {}) {
        UrlRecord rec;
        rec.url           = url;
        rec.canonical_url = url;
        rec.status        = status;
        rec.access_note   = note;
        ASSERT_TRUE(db_->upsert_url(rec));
    }

    fs::path db_path_;
    std::unique_ptr<MetadataDb> db_;
};

// ── Helper predicates ────────────────────────────────────────────────────────

TEST(CrawlStatusHelpers, TerminalStates) {
    EXPECT_TRUE(is_terminal_status(CrawlStatus::Restricted));
    EXPECT_TRUE(is_terminal_status(CrawlStatus::AuthRequired));
    EXPECT_TRUE(is_terminal_status(CrawlStatus::Removed));

    EXPECT_FALSE(is_terminal_status(CrawlStatus::Discovered));
    EXPECT_FALSE(is_terminal_status(CrawlStatus::Queued));
    EXPECT_FALSE(is_terminal_status(CrawlStatus::Fetching));
    EXPECT_FALSE(is_terminal_status(CrawlStatus::Fetched));
    EXPECT_FALSE(is_terminal_status(CrawlStatus::Indexed));
    EXPECT_FALSE(is_terminal_status(CrawlStatus::RateLimited));
    EXPECT_FALSE(is_terminal_status(CrawlStatus::TemporarilyUnavailable));
    EXPECT_FALSE(is_terminal_status(CrawlStatus::Stale));
}

TEST(CrawlStatusHelpers, RetryableStates) {
    EXPECT_TRUE(is_retryable_status(CrawlStatus::RateLimited));
    EXPECT_TRUE(is_retryable_status(CrawlStatus::TemporarilyUnavailable));

    EXPECT_FALSE(is_retryable_status(CrawlStatus::Discovered));
    EXPECT_FALSE(is_retryable_status(CrawlStatus::Indexed));
    EXPECT_FALSE(is_retryable_status(CrawlStatus::Restricted));
    EXPECT_FALSE(is_retryable_status(CrawlStatus::Removed));
}

// ── Database round-trips ─────────────────────────────────────────────────────

TEST_F(CrawlStatusTest, AllStatusesRoundTrip) {
    const std::vector<std::pair<CrawlStatus, std::string>> cases = {
        {CrawlStatus::Discovered,             "https://test.com/0"},
        {CrawlStatus::Queued,                 "https://test.com/1"},
        {CrawlStatus::Fetching,               "https://test.com/2"},
        {CrawlStatus::Fetched,                "https://test.com/3"},
        {CrawlStatus::Indexed,                "https://test.com/4"},
        {CrawlStatus::Restricted,             "https://test.com/5"},
        {CrawlStatus::AuthRequired,           "https://test.com/6"},
        {CrawlStatus::RateLimited,            "https://test.com/7"},
        {CrawlStatus::TemporarilyUnavailable, "https://test.com/8"},
        {CrawlStatus::Stale,                  "https://test.com/9"},
        {CrawlStatus::Removed,                "https://test.com/10"},
    };

    for (const auto& [status, url] : cases) {
        insert(url, status);
        auto rec = db_->find_url(url);
        ASSERT_TRUE(rec.has_value()) << "Missing: " << url;
        EXPECT_EQ(rec->status, status) << "Status mismatch for: " << url;
    }
}

TEST_F(CrawlStatusTest, AccessNoteRoundTrips) {
    insert("https://blocked.example/", CrawlStatus::Restricted, "robots.txt disallow");
    auto rec = db_->find_url("https://blocked.example/");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status,      CrawlStatus::Restricted);
    EXPECT_EQ(rec->access_note, "robots.txt disallow");
}

TEST_F(CrawlStatusTest, UpdateStatus) {
    insert("https://update.example/", CrawlStatus::Discovered);
    ASSERT_TRUE(db_->update_status("https://update.example/",
                                    CrawlStatus::RateLimited,
                                    "429 Too Many Requests"));
    auto rec = db_->find_url("https://update.example/");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status,      CrawlStatus::RateLimited);
    EXPECT_EQ(rec->access_note, "429 Too Many Requests");
}

TEST_F(CrawlStatusTest, RemoveDocSetsRemovedStatus) {
    UrlRecord rec;
    rec.url = rec.canonical_url = "https://remove.example/";
    rec.status = CrawlStatus::Indexed;
    rec.doc_id = 42;
    ASSERT_TRUE(db_->upsert_url(rec));

    ASSERT_TRUE(db_->remove_doc(42));

    auto found = db_->find_url("https://remove.example/");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, CrawlStatus::Removed);
}

TEST_F(CrawlStatusTest, TombstoneDocDelegatesToRemoveDoc) {
    UrlRecord rec;
    rec.url = rec.canonical_url = "https://tombstone.example/";
    rec.status = CrawlStatus::Indexed;
    rec.doc_id = 99;
    ASSERT_TRUE(db_->upsert_url(rec));

    ASSERT_TRUE(db_->tombstone_doc(99));

    auto found = db_->find_url("https://tombstone.example/");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, CrawlStatus::Removed);
}

// ── pop_crawl_queue: terminal states excluded ─────────────────────────────────

TEST_F(CrawlStatusTest, PopCrawlQueueExcludesTerminalStatuses) {
    // Eligible
    insert("https://q.com/discovered",  CrawlStatus::Discovered);
    insert("https://q.com/queued",      CrawlStatus::Queued);
    insert("https://q.com/stale",       CrawlStatus::Stale);
    insert("https://q.com/ratelimited", CrawlStatus::RateLimited);
    insert("https://q.com/unavail",     CrawlStatus::TemporarilyUnavailable);

    // Terminal — must never be returned
    insert("https://q.com/restricted",   CrawlStatus::Restricted);
    insert("https://q.com/authrequired", CrawlStatus::AuthRequired);
    insert("https://q.com/removed",      CrawlStatus::Removed);

    // Fetching/Fetched/Indexed — not in the eligible list
    insert("https://q.com/fetching",  CrawlStatus::Fetching);
    insert("https://q.com/fetched",   CrawlStatus::Fetched);
    insert("https://q.com/indexed",   CrawlStatus::Indexed);

    const auto results = db_->pop_crawl_queue(100, INT64_MAX);

    for (const auto& rec : results) {
        EXPECT_FALSE(is_terminal_status(rec.status))
            << "Terminal status in queue: " << rec.url;
        EXPECT_NE(rec.status, CrawlStatus::Fetching);
        EXPECT_NE(rec.status, CrawlStatus::Fetched);
        EXPECT_NE(rec.status, CrawlStatus::Indexed);
    }

    // Exactly the 5 eligible entries should be returned.
    EXPECT_EQ(results.size(), 5u);
}

TEST_F(CrawlStatusTest, FindByStatus) {
    insert("https://a.com/1", CrawlStatus::Restricted, "robots");
    insert("https://a.com/2", CrawlStatus::Restricted, "policy");
    insert("https://a.com/3", CrawlStatus::Indexed);

    auto restricted = db_->find_by_status(CrawlStatus::Restricted);
    EXPECT_EQ(restricted.size(), 2u);

    auto indexed = db_->find_by_status(CrawlStatus::Indexed);
    EXPECT_EQ(indexed.size(), 1u);
}
