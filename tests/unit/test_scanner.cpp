// test_scanner.cpp — AliScanner end-to-end tests against a known fixture corpus.

#include "ali/scanner/ali_scanner.h"
#include "ali/query/tokeniser.h"
#include "ali/query/query_planner.h"
#include "ali/storage/manifest.h"
#include "ali/storage/metadata_db.h"
#include "ali/index/index_writer.h"
#include "ali/index/index_reader.h"
#include "ali/locality/locality_memory.h"

#include <atomic>
#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;
using namespace ali;

class ScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "ali_scanner_test";
        fs::remove_all(root_);
        fs::create_directories(root_);

        manifest_ = std::make_unique<Manifest>(root_);
        manifest_->open();

        db_ = std::make_unique<MetadataDb>(root_ / "meta.db");
        db_->open();

        IndexWriterConfig wcfg;
        wcfg.index_root = root_.string();
        writer_ = std::make_unique<IndexWriter>(wcfg, *manifest_, *db_);
        writer_->open();

        // Index a small deterministic corpus.
        const std::vector<std::tuple<std::string,std::string,std::string>> docs = {
            {"https://a.com/1", "Fox Page",    "quick brown fox jumped"},
            {"https://a.com/2", "Dog Page",    "lazy dog sat home"},
            {"https://a.com/3", "Mixed Page",  "quick brown dog lazy fox"},
        };
        Tokeniser tok;
        for (const auto& [url, title, body] : docs) {
            auto tokens = tok.tokenise_document(body);
            auto freqs  = Tokeniser::compute_term_freqs(tokens);
            InputDocument doc;
            doc.url       = url;
            doc.title     = title;
            doc.body_text = body;
            writer_->add_document(doc);
        }
        writer_->flush();

        reader_ = std::make_unique<IndexReader>(*manifest_);
        reader_->open();
        locality_ = std::make_unique<LocalityMemory>();
        scanner_  = std::make_unique<AliScanner>(*reader_, *locality_);
    }

    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
    std::unique_ptr<Manifest>     manifest_;
    std::unique_ptr<MetadataDb>   db_;
    std::unique_ptr<IndexWriter>  writer_;
    std::unique_ptr<IndexReader>  reader_;
    std::unique_ptr<LocalityMemory> locality_;
    std::unique_ptr<AliScanner>   scanner_;
};

TEST_F(ScannerTest, FindsDocumentByTerm) {
    Tokeniser tok;
    QueryPlanner planner(*reader_);

    Query q;
    q.tokens = tok.tokenise_query("fox");
    q.top_k  = 10;
    auto plan = planner.plan(q);

    std::atomic<bool> cancel{false};
    auto resp = scanner_->execute(plan, cancel);

    // Both doc 1 and doc 3 contain "fox".
    EXPECT_GE(resp.results.size(), 1u);
    EXPECT_GT(resp.elapsed_ms, 0u);
}

TEST_F(ScannerTest, CancellationReturnsEmpty) {
    Tokeniser tok;
    QueryPlanner planner(*reader_);

    Query q;
    q.tokens = tok.tokenise_query("fox");
    q.top_k  = 10;
    auto plan = planner.plan(q);

    std::atomic<bool> cancel{true};  // pre-cancelled
    auto resp = scanner_->execute(plan, cancel);
    EXPECT_TRUE(resp.results.empty());
}

TEST_F(ScannerTest, MetricsArePopulated) {
    Tokeniser tok;
    QueryPlanner planner(*reader_);

    Query q;
    q.tokens = tok.tokenise_query("quick");
    q.top_k  = 5;
    auto plan = planner.plan(q);

    std::atomic<bool> cancel{false};
    auto resp = scanner_->execute(plan, cancel);

    EXPECT_GT(resp.total_candidates_evaluated, 0u);
}
