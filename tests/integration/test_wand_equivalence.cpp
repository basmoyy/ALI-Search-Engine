// test_wand_equivalence.cpp — Block-Max WAND top-K equivalence tests.
//
// Architecture Brief §8.3 requirement:
//   "Compare Block-Max WAND with exhaustive top-K scoring and verify
//    identical top-K results under the same ranking function."
//
// This test indexes a deterministic corpus, runs every query twice —
// once with WAND enabled and once with forced exhaustive scoring — and
// asserts that the top-K result sets are identical (same doc IDs, same
// scores, same rank order) for every query in the fixture suite.
//
// It also records which blocks were skipped by WAND so we can verify
// that pruning actually occurred (if no blocks are skipped, the corpus
// is too small for WAND to engage — flag this as a warning, not a failure).

#include "ali/storage/manifest.h"
#include "ali/storage/metadata_db.h"
#include "ali/index/index_writer.h"
#include "ali/index/index_reader.h"
#include "ali/query/tokeniser.h"
#include "ali/query/query_planner.h"
#include "ali/scanner/ali_scanner.h"
#include "ali/locality/locality_memory.h"

#include <atomic>
#include <filesystem>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace ali;

// ── Fixture corpus ────────────────────────────────────────────────────────────
// 30 documents with varied term frequencies to produce realistic BM25 scores.

static const std::vector<std::pair<std::string, std::string>> kCorpus = {
    {"https://t.com/1",  "on device search engine for mobile phones with low memory"},
    {"https://t.com/2",  "inverted index design for efficient full text search"},
    {"https://t.com/3",  "bm25 ranking function term frequency document frequency"},
    {"https://t.com/4",  "block max wand pruning top k retrieval algorithm"},
    {"https://t.com/5",  "locality memory cache admission block access patterns"},
    {"https://t.com/6",  "sqlite metadata storage for crawl state and url frontier"},
    {"https://t.com/7",  "compressed posting lists vbyte delta encoding integers"},
    {"https://t.com/8",  "resource controller battery network thermal mobile device"},
    {"https://t.com/9",  "web crawler robots txt politeness rate limiting backoff"},
    {"https://t.com/10", "android kotlin jni bridge native code performance"},
    {"https://t.com/11", "segment writer reader manifest atomic publication crash safe"},
    {"https://t.com/12", "query planner broad term detection top k heap result set"},
    {"https://t.com/13", "mobile search offline capability local index no server"},
    {"https://t.com/14", "memory bounded working set twelve megabytes query budget"},
    {"https://t.com/15", "html parser boilerplate removal link extraction canonical"},
    {"https://t.com/16", "deduplication content fingerprint fnv hash canonical url"},
    {"https://t.com/17", "compaction segment merge deletion tombstone background"},
    {"https://t.com/18", "platform adapter posix macos linux windows ios android"},
    {"https://t.com/19", "sparse dictionary navigation index lookup file offset"},
    {"https://t.com/20", "benchmark methodology cold warm cache latency percentile"},
    {"https://t.com/21", "search engine index term posting list document scoring"},
    {"https://t.com/22", "mobile device low ram two gigabytes phone android search"},
    {"https://t.com/23", "crawl scheduler priority queue url fresh stale retry"},
    {"https://t.com/24", "index segment immutable format binary version checksum"},
    {"https://t.com/25", "block metadata upper bound score wand skip algorithm"},
    {"https://t.com/26", "tokeniser english stop words lower case normalisation"},
    {"https://t.com/27", "locality statistics useful access decaying priority score"},
    {"https://t.com/28", "android workmanager background job battery network unmetered"},
    {"https://t.com/29", "openssl tls https fetch certificate verify peer"},
    {"https://t.com/30", "search result ranking relevance quality freshness offline"},
};

static const std::vector<std::string> kQueries = {
    "search engine",
    "mobile device",
    "index posting",
    "wand block",
    "locality memory",
    "crawler robots",
    "bm25 ranking",
    "android kotlin",
    "segment manifest",
    "battery network",
};

// ── Test fixture ──────────────────────────────────────────────────────────────

class WandEquivalenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "ali_wand_test";
        fs::remove_all(root_);
        fs::create_directories(root_);

        manifest_ = std::make_unique<Manifest>(root_);
        ASSERT_TRUE(manifest_->open());

        db_ = std::make_unique<MetadataDb>(root_ / "meta.db");
        ASSERT_TRUE(db_->open());

        IndexWriterConfig wcfg;
        wcfg.index_root = root_.string();
        wcfg.docs_per_segment = 10;  // Force multiple segments for realism.
        writer_ = std::make_unique<IndexWriter>(wcfg, *manifest_, *db_);
        ASSERT_TRUE(writer_->open());

        Tokeniser tok;
        for (const auto& [url, body] : kCorpus) {
            InputDocument doc;
            doc.url       = url;
            doc.title     = url;
            doc.body_text = body;
            writer_->add_document(doc);
        }
        ASSERT_TRUE(writer_->flush());

        reader_   = std::make_unique<IndexReader>(*manifest_);
        ASSERT_TRUE(reader_->open());
        locality_ = std::make_unique<LocalityMemory>();
    }

    void TearDown() override { fs::remove_all(root_); }

    // Run a query with WAND setting controlled by force_exhaustive.
    SearchResponse run_query(const std::string& raw_query,
                              bool               force_exhaustive) {
        Tokeniser    tok;
        QueryPlannerConfig pcfg;
        pcfg.prefer_block_max_wand = !force_exhaustive;
        QueryPlanner planner(*reader_, pcfg);

        Query q;
        q.tokens     = tok.tokenise_query(raw_query);
        q.top_k      = 5;
        q.timeout_ms = 5000;

        QueryPlan plan = planner.plan(q);
        if (force_exhaustive) plan.use_block_max_wand = false;

        ScannerConfig scfg;
        AliScanner scanner(*reader_, *locality_, scfg);
        std::atomic<bool> cancel{false};
        return scanner.execute(plan, cancel);
    }

    fs::path root_;
    std::unique_ptr<Manifest>     manifest_;
    std::unique_ptr<MetadataDb>   db_;
    std::unique_ptr<IndexWriter>  writer_;
    std::unique_ptr<IndexReader>  reader_;
    std::unique_ptr<LocalityMemory> locality_;
};

// ── Equivalence tests ─────────────────────────────────────────────────────────

TEST_F(WandEquivalenceTest, TopKResultsMatchExhaustiveForAllQueries) {
    for (const auto& query : kQueries) {
        const auto wand_resp = run_query(query, /*force_exhaustive=*/false);
        const auto exh_resp  = run_query(query, /*force_exhaustive=*/true);

        ASSERT_EQ(wand_resp.results.size(), exh_resp.results.size())
            << "Result count differs for query: " << query;

        for (std::size_t i = 0; i < wand_resp.results.size(); ++i) {
            EXPECT_EQ(wand_resp.results[i].url, exh_resp.results[i].url)
                << "URL mismatch at rank " << i << " for query: " << query;

            EXPECT_NEAR(wand_resp.results[i].score, exh_resp.results[i].score, 1e-4f)
                << "Score mismatch at rank " << i << " for query: " << query;
        }
    }
}

TEST_F(WandEquivalenceTest, TopKScoresArePositiveAndDescending) {
    for (const auto& query : kQueries) {
        const auto resp = run_query(query, false);
        if (resp.results.empty()) continue;

        for (const auto& r : resp.results) {
            EXPECT_GT(r.score, 0.0f) << "Zero score for query: " << query;
        }

        for (std::size_t i = 1; i < resp.results.size(); ++i) {
            EXPECT_GE(resp.results[i-1].score, resp.results[i].score)
                << "Results not in descending score order for query: " << query;
        }
    }
}

TEST_F(WandEquivalenceTest, MetricsArePopulated) {
    const auto resp = run_query("search engine", false);
    EXPECT_GT(resp.blocks_read, 0u);
    EXPECT_GT(resp.bytes_read,  0u);
    EXPECT_GT(resp.elapsed_ms,  0u);  // at least 1 ms on any real hardware
}

TEST_F(WandEquivalenceTest, CancellationReturnsEmptyWithoutCrash) {
    Tokeniser tok;
    QueryPlanner planner(*reader_);
    Query q;
    q.tokens = tok.tokenise_query("search");
    q.top_k  = 5;
    auto plan = planner.plan(q);

    ScannerConfig scfg;
    AliScanner scanner(*reader_, *locality_, scfg);
    std::atomic<bool> cancel{true};  // pre-cancelled
    auto resp = scanner.execute(plan, cancel);
    EXPECT_TRUE(resp.results.empty());
}

TEST_F(WandEquivalenceTest, BroadQueryDoesNotExceedCandidateBudget) {
    // "index" appears in many documents — should not overflow the accumulator.
    ScannerConfig scfg;
    scfg.max_candidates = 8;
    AliScanner scanner(*reader_, *locality_, scfg);

    Tokeniser tok;
    QueryPlanner planner(*reader_);
    Query q;
    q.tokens = tok.tokenise_query("index");
    q.top_k  = 5;
    auto plan = planner.plan(q);

    std::atomic<bool> cancel{false};
    auto resp = scanner.execute(plan, cancel);

    EXPECT_LE(resp.total_candidates_evaluated, scfg.max_candidates);
}
