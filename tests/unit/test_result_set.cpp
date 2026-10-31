// test_result_set.cpp — ResultSet (bounded top-K heap) tests.

#include "ali/ranking/result_set.h"

#include <gtest/gtest.h>
#include <vector>

using namespace ali;

TEST(ResultSet, TopKIsRespected) {
    ResultSet rs(3);
    rs.try_insert(1, 1.0f);
    rs.try_insert(2, 5.0f);
    rs.try_insert(3, 3.0f);
    rs.try_insert(4, 0.5f);  // below threshold when full
    rs.try_insert(5, 10.0f); // displaces minimum (1.0)

    EXPECT_EQ(rs.size(), 3u);
    auto top = rs.top_k();
    EXPECT_EQ(top.size(), 3u);
    EXPECT_FLOAT_EQ(top[0].score, 10.0f);
    EXPECT_FLOAT_EQ(top[1].score, 5.0f);
    EXPECT_FLOAT_EQ(top[2].score, 3.0f);
}

TEST(ResultSet, ThresholdBeforeFullIsMinusInf) {
    ResultSet rs(5);
    rs.try_insert(1, 2.0f);
    EXPECT_LT(rs.threshold(), 0.0f);  // not full yet
}

TEST(ResultSet, ThresholdAfterFull) {
    ResultSet rs(2);
    rs.try_insert(1, 1.0f);
    rs.try_insert(2, 3.0f);
    EXPECT_FLOAT_EQ(rs.threshold(), 1.0f);
}

TEST(ResultSet, ClearResets) {
    ResultSet rs(3);
    rs.try_insert(1, 9.0f);
    rs.clear();
    EXPECT_EQ(rs.size(), 0u);
    EXPECT_TRUE(rs.top_k().empty());
}

TEST(ResultSet, KEqualsZeroRejectsAll) {
    ResultSet rs(0);
    EXPECT_FALSE(rs.try_insert(1, 99.0f));
    EXPECT_EQ(rs.size(), 0u);
}
