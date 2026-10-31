// test_query_planner.cpp — QueryPlanner tests (with an empty index).

#include "ali/query/query_planner.h"
#include "ali/query/tokeniser.h"
#include "ali/storage/manifest.h"
#include "ali/index/index_reader.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ali;

class QueryPlannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "ali_planner_test";
        fs::remove_all(root_);
        manifest_ = std::make_unique<Manifest>(root_);
        manifest_->open();
        reader_ = std::make_unique<IndexReader>(*manifest_);
        reader_->open();
        planner_ = std::make_unique<QueryPlanner>(*reader_);
        tokeniser_ = std::make_unique<Tokeniser>();
    }
    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
    std::unique_ptr<Manifest>     manifest_;
    std::unique_ptr<IndexReader>  reader_;
    std::unique_ptr<QueryPlanner> planner_;
    std::unique_ptr<Tokeniser>    tokeniser_;
};

TEST_F(QueryPlannerTest, EmptyIndexProducesValidPlan) {
    Query q;
    q.tokens = tokeniser_->tokenise_query("search engine test");
    q.top_k  = 10;

    auto plan = planner_->plan(q);
    EXPECT_FALSE(plan.term_plans.empty());
    // Empty index → no block metadata → WAND off.
    EXPECT_FALSE(plan.use_block_max_wand);
}

TEST_F(QueryPlannerTest, EmptyQueryProducesNoTerms) {
    Query q;
    q.tokens = {};
    q.top_k  = 10;
    auto plan = planner_->plan(q);
    EXPECT_TRUE(plan.term_plans.empty());
}
