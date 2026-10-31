// test_url_frontier.cpp — UrlFrontier correctness tests.
//
// Covers: enqueue, duplicate suppression, pop, mark_indexed,
// mark_access_failure, mark_transient_failure with backoff,
// and terminal-status exclusion from pop().

#include "ali/crawler/url_frontier.h"
#include "ali/storage/metadata_db.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ali;

class FrontierTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = fs::temp_directory_path() / "ali_frontier_test.db";
        fs::remove(db_path_);
        db_      = std::make_unique<MetadataDb>(db_path_);
        ASSERT_TRUE(db_->open());
        frontier_ = std::make_unique<UrlFrontier>(*db_);
    }
    void TearDown() override { fs::remove(db_path_); }

    fs::path db_path_;
    std::unique_ptr<MetadataDb>  db_;
    std::unique_ptr<UrlFrontier> frontier_;
};

TEST_F(FrontierTest, EnqueueNewUrl) {
    EXPECT_TRUE(frontier_->enqueue("https://a.com/"));
}

TEST_F(FrontierTest, DuplicateEnqueueReturnsFalse) {
    EXPECT_TRUE(frontier_->enqueue("https://b.com/"));
    EXPECT_FALSE(frontier_->enqueue("https://b.com/"));
}

TEST_F(FrontierTest, PopReturnsEnqueuedUrl) {
    frontier_->enqueue("https://pop.com/");
    auto records = frontier_->pop(10, INT64_MAX);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].url, "https://pop.com/");
}

TEST_F(FrontierTest, MarkIndexedSetsStatus) {
    frontier_->enqueue("https://index.com/");
    frontier_->mark_indexed("https://index.com/", 42, 12345);
    auto rec = db_->find_url("https://index.com/");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status,  CrawlStatus::Indexed);
    EXPECT_EQ(rec->doc_id,  42u);
}

TEST_F(FrontierTest, MarkAccessFailureRestrictedNotPoppedAgain) {
    frontier_->enqueue("https://blocked.com/");
    frontier_->mark_access_failure("https://blocked.com/",
                                    CrawlStatus::Restricted,
                                    "robots.txt", 0);
    // Restricted is terminal — should not appear in pop results.
    auto records = frontier_->pop(10, INT64_MAX);
    for (const auto& r : records) {
        EXPECT_NE(r.url, "https://blocked.com/");
    }
}

TEST_F(FrontierTest, MarkTransientFailureSchedulesRetry) {
    frontier_->enqueue("https://flaky.com/");
    frontier_->mark_transient_failure("https://flaky.com/", 1000);
    auto rec = db_->find_url("https://flaky.com/");
    ASSERT_TRUE(rec.has_value());
    // next_fetch_at should be in the future relative to now=1000
    EXPECT_GT(rec->next_fetch_at, 1000LL);
}

TEST_F(FrontierTest, MarkStaleAllowsRecrawl) {
    frontier_->enqueue("https://stale.com/");
    frontier_->mark_indexed("https://stale.com/", 1, 111);
    frontier_->mark_stale("https://stale.com/");
    auto rec = db_->find_url("https://stale.com/");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status, CrawlStatus::Stale);
}
