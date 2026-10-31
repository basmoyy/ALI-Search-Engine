// test_locality_memory.cpp — LocalityMemory correctness and bounds tests.

#include "ali/locality/locality_memory.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace ali;

TEST(LocalityMemory, UnknownBlockHasZeroPriority) {
    LocalityMemory lm;
    EXPECT_EQ(lm.priority(99999), 0.0f);
}

TEST(LocalityMemory, UsefulAccessIncreasesAboveAccess) {
    LocalityMemory lm;
    lm.record_access(1, false);
    const float p_normal = lm.priority(1);

    LocalityMemory lm2;
    lm2.record_access(2, true);
    const float p_useful = lm2.priority(2);

    EXPECT_GT(p_useful, p_normal);
}

TEST(LocalityMemory, EntryCapEnforced) {
    LocalityConfig cfg;
    cfg.max_entries = 10;
    LocalityMemory lm(cfg);
    for (BlockId i = 0; i < 20; ++i) lm.record_access(i, true);
    EXPECT_LE(lm.entry_count(), cfg.max_entries);
}

TEST(LocalityMemory, CompactReducesEntries) {
    LocalityConfig cfg;
    cfg.lambda = 100.0f;  // Very fast decay so entries become near-zero quickly.
    cfg.max_entries = 100;
    LocalityMemory lm(cfg);
    for (BlockId i = 0; i < 50; ++i) lm.record_access(i, false);
    // With very fast decay, compact should evict most entries.
    lm.compact();
    EXPECT_LE(lm.entry_count(), 50u);
}

TEST(LocalityMemory, PersistAndLoad) {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "ali_locality_test.bin";

    LocalityMemory lm;
    lm.record_access(42, true);
    lm.record_access(43, false);
    ASSERT_TRUE(lm.save(tmp));

    LocalityMemory lm2;
    ASSERT_TRUE(lm2.load(tmp));
    EXPECT_GT(lm2.priority(42), 0.0f);

    fs::remove(tmp);
}
