// test_vbyte.cpp — Variable-byte codec correctness tests.

#include "ali/compression/codec.h"

#include <gtest/gtest.h>
#include <vector>

using namespace ali::compression;

TEST(VByte, EncodeDecodeSingleValue) {
    const std::vector<uint32_t> input = {42};
    std::vector<uint8_t> buf(kVByte.max_encoded_size(input.size()));
    const std::size_t n = kVByte.encode(input, buf);
    ASSERT_GT(n, 0u);

    std::vector<uint32_t> output(input.size());
    kVByte.decode({buf.data(), n}, 1, output);
    EXPECT_EQ(output[0], 42u);
}

TEST(VByte, EncodeDecodeMultipleValues) {
    const std::vector<uint32_t> input = {0, 1, 127, 128, 16383, 16384, UINT32_MAX / 2};
    std::vector<uint8_t> buf(kVByte.max_encoded_size(input.size()));
    const std::size_t n = kVByte.encode(input, buf);
    ASSERT_GT(n, 0u);

    std::vector<uint32_t> output(input.size());
    kVByte.decode({buf.data(), n}, static_cast<uint32_t>(input.size()), output);
    EXPECT_EQ(input, output);
}

TEST(VByte, EncodeDecodeZero) {
    const std::vector<uint32_t> input = {0};
    std::vector<uint8_t> buf(kVByte.max_encoded_size(1));
    const std::size_t n = kVByte.encode(input, buf);
    ASSERT_EQ(n, 1u);  // 0x00 encodes to one byte

    std::vector<uint32_t> output(1);
    kVByte.decode({buf.data(), n}, 1, output);
    EXPECT_EQ(output[0], 0u);
}

TEST(VByte, EmptyInput) {
    const std::vector<uint32_t> input;
    std::vector<uint8_t> buf(16);
    EXPECT_EQ(kVByte.encode(input, buf), 0u);
}
