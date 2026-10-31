// test_bm25.cpp — BM25 scorer correctness tests.

#include "ali/ranking/bm25.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace ali;

TEST(Bm25, ZeroDocFreqReturnsZero) {
    Bm25Scorer scorer({}, 1000, 100.0f);
    EXPECT_EQ(scorer.score(0, 5, 100), 0.0f);
}

TEST(Bm25, ZeroTermFreqReturnsZero) {
    Bm25Scorer scorer({}, 1000, 100.0f);
    EXPECT_EQ(scorer.score(10, 0, 100), 0.0f);
}

TEST(Bm25, HigherTermFreqGivesHigherScore) {
    Bm25Scorer scorer({}, 1000, 100.0f);
    const float s1 = scorer.score(10, 1, 100);
    const float s2 = scorer.score(10, 5, 100);
    EXPECT_GT(s2, s1);
}

TEST(Bm25, LessCommonTermGivesHigherScore) {
    Bm25Scorer scorer({}, 1000, 100.0f);
    // rare term (df=1) vs common term (df=500)
    const float rare   = scorer.score(1,   1, 100);
    const float common = scorer.score(500, 1, 100);
    EXPECT_GT(rare, common);
}

TEST(Bm25, LongerDocGivesLowerScore) {
    Bm25Scorer scorer({}, 1000, 100.0f);
    const float short_doc = scorer.score(10, 2, 50);
    const float long_doc  = scorer.score(10, 2, 500);
    EXPECT_GT(short_doc, long_doc);
}

TEST(Bm25, ScoreIsPositive) {
    Bm25Scorer scorer({}, 1000, 100.0f);
    EXPECT_GT(scorer.score(5, 3, 120), 0.0f);
}

TEST(Bm25, IdealCaseProducesFiniteScore) {
    Bm25Scorer scorer({}, 1000, 100.0f);
    const float s = scorer.score(1, 1, 1);
    EXPECT_TRUE(std::isfinite(s));
    EXPECT_GT(s, 0.0f);
}
