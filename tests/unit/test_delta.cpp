// test_delta.cpp — Delta/gap encoding correctness tests.

#include "ali/compression/codec.h"

#include <gtest/gtest.h>
#include <vector>

using namespace ali::compression;

TEST(Delta, EncodeDecode_RoundTrip) {
    const std::vector<uint32_t> ids = {5, 10, 15, 100, 200, 1000};
    auto deltas = encode_deltas(ids);
    ASSERT_EQ(deltas.size(), ids.size());
    EXPECT_EQ(deltas[0], 5u);
    EXPECT_EQ(deltas[1], 5u);

    auto recovered = decode_deltas(deltas);
    EXPECT_EQ(recovered, ids);
}

TEST(Delta, SingleElement) {
    const std::vector<uint32_t> ids = {42};
    auto deltas = encode_deltas(ids);
    ASSERT_EQ(deltas.size(), 1u);
    EXPECT_EQ(deltas[0], 42u);

    auto recovered = decode_deltas(deltas);
    EXPECT_EQ(recovered, ids);
}

TEST(Delta, ConsecutiveIDs) {
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < 256; ++i) ids.push_back(i);
    auto deltas = encode_deltas(ids);
    EXPECT_EQ(deltas[0], 0u);
    for (std::size_t i = 1; i < deltas.size(); ++i) EXPECT_EQ(deltas[i], 1u);

    auto recovered = decode_deltas(deltas);
    EXPECT_EQ(recovered, ids);
}

TEST(Delta, UnsortedInputThrows) {
    const std::vector<uint32_t> unsorted = {10, 5, 20};
    EXPECT_THROW(encode_deltas(unsorted), std::invalid_argument);
}

TEST(Delta, EmptyInput) {
    const std::vector<uint32_t> empty;
    auto deltas = encode_deltas(empty);
    EXPECT_TRUE(deltas.empty());
    auto ids = decode_deltas(deltas);
    EXPECT_TRUE(ids.empty());
}
