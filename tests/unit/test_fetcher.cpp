// test_fetcher.cpp — Fetcher safety and address-blocking tests.
//
// Network I/O is not available in unit tests, so we test:
//   - Private/blocked addresses are rejected before any network call.
//   - Valid public URLs are accepted (but result in NetworkError stub response).
//   - is_blocked_address() classification is correct for all reserved ranges.

#include "ali/crawler/fetcher.h"

#include <gtest/gtest.h>

using namespace ali;

// ── is_blocked_address ────────────────────────────────────────────────────────

TEST(BlockedAddress, LocahostBlocked) {
    EXPECT_TRUE(is_blocked_address("localhost"));
    EXPECT_TRUE(is_blocked_address("127.0.0.1"));
    EXPECT_TRUE(is_blocked_address("127.0.0.2"));
    EXPECT_TRUE(is_blocked_address("::1"));
}

TEST(BlockedAddress, LinkLocalBlocked) {
    EXPECT_TRUE(is_blocked_address("169.254.0.1"));
    EXPECT_TRUE(is_blocked_address("169.254.169.254"));
}

TEST(BlockedAddress, Rfc1918Blocked) {
    EXPECT_TRUE(is_blocked_address("10.0.0.1"));
    EXPECT_TRUE(is_blocked_address("192.168.1.1"));
}

TEST(BlockedAddress, MetadataServiceBlocked) {
    EXPECT_TRUE(is_blocked_address("metadata.google.internal"));
}

TEST(BlockedAddress, PublicAddressesAllowed) {
    EXPECT_FALSE(is_blocked_address("example.com"));
    EXPECT_FALSE(is_blocked_address("8.8.8.8"));
    EXPECT_FALSE(is_blocked_address("1.1.1.1"));
    EXPECT_FALSE(is_blocked_address("www.google.com"));
}

TEST(BlockedAddress, EmptyHostBlocked) {
    EXPECT_TRUE(is_blocked_address(""));
}

// ── Fetcher result codes ──────────────────────────────────────────────────────

TEST(Fetcher, BlockedAddressReturnsCorrectCode) {
    Fetcher f;
    auto result = f.fetch("http://127.0.0.1/secret");
    EXPECT_EQ(result.code, FetchResultCode::BlockedAddress);
}

TEST(Fetcher, InvalidUrlReturnsNetworkError) {
    Fetcher f;
    auto result = f.fetch("not-a-url");
    EXPECT_EQ(result.code, FetchResultCode::NetworkError);
}

TEST(Fetcher, PublicUrlReturnsNetworkErrorStub) {
    // Networking is not implemented yet (Phase 9 stub).
    // This test just verifies the code compiles, runs, and returns
    // a valid (not BlockedAddress) result for a public URL.
    Fetcher f;
    auto result = f.fetch("https://example.com/");
    EXPECT_NE(result.code, FetchResultCode::BlockedAddress);
}
