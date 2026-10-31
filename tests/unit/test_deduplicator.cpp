// test_deduplicator.cpp — Deduplicator correctness tests.
//
// Covers: fingerprint stability, exact duplicate detection, non-duplicate
// pass-through, and zero-hash edge case.

#include "ali/crawler/deduplicator.h"
#include "ali/storage/metadata_db.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ali;

class DeduplicatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = fs::temp_directory_path() / "ali_dedup_test.db";
        fs::remove(db_path_);
        db_ = std::make_unique<MetadataDb>(db_path_);
        ASSERT_TRUE(db_->open());
        dedup_ = std::make_unique<Deduplicator>(*db_);
    }
    void TearDown() override { fs::remove(db_path_); }

    fs::path db_path_;
    std::unique_ptr<MetadataDb>   db_;
    std::unique_ptr<Deduplicator> dedup_;
};

TEST(DeduplicatorStatic, FingerprintIsDeterministic) {
    const uint32_t h1 = Deduplicator::fingerprint("hello world");
    const uint32_t h2 = Deduplicator::fingerprint("hello world");
    EXPECT_EQ(h1, h2);
}

TEST(DeduplicatorStatic, DifferentBodiesHaveDifferentFingerprints) {
    const uint32_t h1 = Deduplicator::fingerprint("hello world");
    const uint32_t h2 = Deduplicator::fingerprint("goodbye world");
    EXPECT_NE(h1, h2);
}

TEST(DeduplicatorStatic, EmptyBodyFingerprint) {
    // Should not crash or return same as a non-empty string.
    const uint32_t h = Deduplicator::fingerprint("");
    EXPECT_EQ(h, 2166136261u);  // FNV-1a offset basis (no data mixed in)
}

TEST_F(DeduplicatorTest, UnknownUrlIsNotDuplicate) {
    EXPECT_FALSE(dedup_->is_duplicate("https://new.example.com/", 12345));
}

TEST_F(DeduplicatorTest, IndexedUrlWithMatchingHashIsDuplicate) {
    UrlRecord rec;
    rec.url = rec.canonical_url = "https://dup.example.com/";
    rec.status       = CrawlStatus::Indexed;
    rec.content_hash = 99999;
    db_->upsert_url(rec);

    EXPECT_TRUE(dedup_->is_duplicate("https://dup.example.com/", 99999));
}

TEST_F(DeduplicatorTest, IndexedUrlWithDifferentHashIsNotDuplicate) {
    UrlRecord rec;
    rec.url = rec.canonical_url = "https://changed.example.com/";
    rec.status       = CrawlStatus::Indexed;
    rec.content_hash = 11111;
    db_->upsert_url(rec);

    // Different hash = content changed = not a duplicate
    EXPECT_FALSE(dedup_->is_duplicate("https://changed.example.com/", 22222));
}

TEST_F(DeduplicatorTest, ZeroHashNeverDuplicate) {
    UrlRecord rec;
    rec.url = rec.canonical_url = "https://zerohash.example.com/";
    rec.status       = CrawlStatus::Indexed;
    rec.content_hash = 0;
    db_->upsert_url(rec);

    // Zero hash is treated as "unknown" — never a duplicate
    EXPECT_FALSE(dedup_->is_duplicate("https://zerohash.example.com/", 0));
}
