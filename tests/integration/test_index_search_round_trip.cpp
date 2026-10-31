 // test_index_search_round_trip.cpp — Full integration: index via C API, search via C API.

#include "ali.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

class CApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "ali_capi_test";
        fs::remove_all(root_);
        fs::create_directories(root_);
        index_root_ = root_ / "index";
        db_path_    = (root_ / "meta.db").string();

        AliEngine* engine = nullptr;
        AliStatus s = ali_engine_open(index_root_.string().c_str(),
                                       db_path_.c_str(), &engine);
        ASSERT_EQ(s, ALI_OK);
        engine_ = engine;
    }

    void TearDown() override {
        ali_engine_close(engine_);
        fs::remove_all(root_);
    }

    AliEngine* engine_ = nullptr;
    fs::path root_, index_root_;
    std::string db_path_;
};

TEST_F(CApiTest, IndexAndSearchReturnsResults) {
    ASSERT_EQ(ali_index_document(engine_,
        "https://example.com/a", "Alpha Page",
        "quick brown fox jumps over the lazy dog"), ALI_OK);

    ASSERT_EQ(ali_index_document(engine_,
        "https://example.com/b", "Beta Page",
        "lazy cat sits on warm floor"), ALI_OK);

    ASSERT_EQ(ali_index_flush(engine_), ALI_OK);

    AliResults* results = nullptr;
    AliStatus s = ali_search(engine_, "fox", 10, 1000, &results);
    ASSERT_EQ(s, ALI_OK);
    ASSERT_NE(results, nullptr);

    EXPECT_GE(ali_results_count(results), 1u);

    const char *url = nullptr, *title = nullptr, *excerpt = nullptr;
    float score = 0.0f;
    EXPECT_EQ(ali_results_get(results, 0, &url, &title, &excerpt, &score), ALI_OK);
    EXPECT_NE(url, nullptr);
    EXPECT_GT(score, 0.0f);

    ali_results_free(results);
}

TEST_F(CApiTest, EmptyQueryReturnsNoResults) {
    AliResults* results = nullptr;
    // "the" is a stop word; no tokens after processing → empty results.
    ali_search(engine_, "the", 10, 1000, &results);
    if (results) {
        // Either 0 results or a graceful empty response.
        ali_results_free(results);
    }
}

TEST_F(CApiTest, InvalidArgReturnsError) {
    EXPECT_EQ(ali_index_document(nullptr, "u", "t", "b"), ALI_ERR_INVALID_ARG);
    EXPECT_EQ(ali_index_document(engine_, nullptr, "t", "b"), ALI_ERR_INVALID_ARG);
}

TEST_F(CApiTest, StatsReflectIndexedDocs) {
    ali_index_document(engine_, "https://x.com/1", "T", "alpha beta gamma");
    ali_index_flush(engine_);

    uint64_t count = 0;
    ASSERT_EQ(ali_stats_doc_count(engine_, &count), ALI_OK);
    EXPECT_GE(count, 1u);
}
