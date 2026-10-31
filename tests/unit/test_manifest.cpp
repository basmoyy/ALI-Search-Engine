// test_manifest.cpp — Manifest atomic publication and recovery tests.

#include "ali/storage/manifest.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ali;

class ManifestTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "ali_manifest_test";
        fs::remove_all(root_);
    }
    void TearDown() override { fs::remove_all(root_); }
    fs::path root_;
};

TEST_F(ManifestTest, OpenCreatesDirectory) {
    Manifest m(root_);
    ASSERT_TRUE(m.open());
    EXPECT_TRUE(fs::exists(root_));
}

TEST_F(ManifestTest, FreshManifestHasZeroSegments) {
    Manifest m(root_);
    ASSERT_TRUE(m.open());
    EXPECT_EQ(m.snapshot().segments.size(), 0u);
}

TEST_F(ManifestTest, PublishSegment) {
    Manifest m(root_);
    ASSERT_TRUE(m.open());

    SegmentEntry e;
    e.segment_id = "seg-abc";
    e.generation = 1;
    e.path       = root_ / "seg-abc";
    ASSERT_TRUE(m.publish_segment(e));

    EXPECT_EQ(m.snapshot().segments.size(), 1u);
    EXPECT_EQ(m.snapshot().segments[0].segment_id, "seg-abc");
}

TEST_F(ManifestTest, VersionAdvancesOnPublish) {
    Manifest m(root_);
    ASSERT_TRUE(m.open());
    const uint64_t v0 = m.snapshot().manifest_version;

    SegmentEntry e;
    e.segment_id = "seg-1";
    m.publish_segment(e);
    EXPECT_GT(m.snapshot().manifest_version, v0);
}

TEST_F(ManifestTest, PersistsAndReloads) {
    {
        Manifest m(root_);
        ASSERT_TRUE(m.open());
        SegmentEntry e;
        e.segment_id = "reload-seg";
        e.generation = 5;
        m.publish_segment(e);
    }
    // Reopen
    Manifest m2(root_);
    ASSERT_TRUE(m2.open());
    const auto snap = m2.snapshot();
    ASSERT_EQ(snap.segments.size(), 1u);
    EXPECT_EQ(snap.segments[0].segment_id, "reload-seg");
}

TEST_F(ManifestTest, RetireSegment) {
    Manifest m(root_);
    ASSERT_TRUE(m.open());
    SegmentEntry e;
    e.segment_id = "to-retire";
    m.publish_segment(e);

    m.retire_segment("to-retire");
    EXPECT_TRUE(m.snapshot().segments[0].is_tombstone);
    EXPECT_EQ(m.snapshot().total_doc_count(), 0u);
}
