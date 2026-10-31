// test_crash_recovery.cpp — Segment publication crash-safety tests.
//
// Simulates a partial write by creating a .tmp manifest file without
// completing the rename, then verifying the reader opens without error.

#include "ali/storage/manifest.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace ali;

TEST(CrashRecovery, PartialManifestTmpIgnored) {
    const fs::path root = fs::temp_directory_path() / "ali_recovery_test";
    fs::remove_all(root);
    fs::create_directories(root);

    // Leave a stray .tmp file (simulated interrupted write).
    std::ofstream stray(root / "manifest.bin.tmp", std::ios::binary | std::ios::trunc);
    stray.write("garbage", 7);
    stray.close();

    // Open without an existing manifest.bin → should start fresh.
    Manifest m(root);
    ASSERT_TRUE(m.open());
    EXPECT_EQ(m.snapshot().segments.size(), 0u);

    fs::remove_all(root);
}

TEST(CrashRecovery, PublishAfterOpenIsConsistent) {
    const fs::path root = fs::temp_directory_path() / "ali_recovery_pub";
    fs::remove_all(root);

    Manifest m(root);
    ASSERT_TRUE(m.open());

    SegmentEntry e;
    e.segment_id = "crash-seg";
    e.generation = 1;
    ASSERT_TRUE(m.publish_segment(e));

    // Reopen and verify.
    Manifest m2(root);
    ASSERT_TRUE(m2.open());
    ASSERT_EQ(m2.snapshot().segments.size(), 1u);
    EXPECT_EQ(m2.snapshot().segments[0].segment_id, "crash-seg");

    fs::remove_all(root);
}
