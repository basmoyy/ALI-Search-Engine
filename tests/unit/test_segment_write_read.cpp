// test_segment_write_read.cpp — SegmentWriter / SegmentReader round-trip tests.

#include "ali/storage/segment_writer.h"
#include "ali/storage/segment_reader.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ali;

class SegmentRoundTripTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "ali_seg_test";
        fs::remove_all(dir_);
    }
    void TearDown() override {
        fs::remove_all(dir_);
    }
    fs::path dir_;
};

TEST_F(SegmentRoundTripTest, WriteAndReadBack) {
    SegmentWriter writer(dir_ / "tmp");
    const DocId id = writer.add_document(
        "https://example.com/page",
        "Test Page",
        "the quick brown fox jumps over lazy dog",
        {{"brown", 1}, {"fox", 1}, {"jumps", 1}, {"lazy", 1}, {"quick", 1}});

    ASSERT_NE(id, kInvalidDocId);
    ASSERT_TRUE(writer.finish(dir_ / "seg0", 0));

    SegmentReader reader(dir_ / "seg0");
    ASSERT_TRUE(reader.is_valid());
    EXPECT_EQ(reader.doc_count(), 1u);

    auto entry = reader.find_term("fox");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->doc_frequency, 1u);

    auto absent = reader.find_term("elephant");
    EXPECT_FALSE(absent.has_value());
}

TEST_F(SegmentRoundTripTest, MultipleDocuments) {
    SegmentWriter writer(dir_ / "tmp");
    for (uint32_t i = 0; i < 10; ++i) {
        const std::string url = "https://example.com/" + std::to_string(i);
        writer.add_document(url, "Title " + std::to_string(i),
                             "content word" + std::to_string(i),
                             {{"content", 1}, {"word" + std::to_string(i), 1}});
    }
    ASSERT_TRUE(writer.finish(dir_ / "seg1", 1));

    SegmentReader reader(dir_ / "seg1");
    ASSERT_TRUE(reader.is_valid());
    EXPECT_EQ(reader.doc_count(), 10u);

    auto entry = reader.find_term("content");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->doc_frequency, 10u);
}

TEST_F(SegmentRoundTripTest, DisplayDataRoundTrip) {
    SegmentWriter writer(dir_ / "tmp");
    writer.add_document("https://test.example/", "My Title",
                         "Some body text here for testing",
                         {{"body", 1}, {"testing", 1}});
    ASSERT_TRUE(writer.finish(dir_ / "seg2", 0));

    SegmentReader reader(dir_ / "seg2");
    ASSERT_TRUE(reader.is_valid());

    std::string url, title, excerpt;
    ASSERT_TRUE(reader.get_doc_display(0, url, title, excerpt));
    EXPECT_EQ(url,   "https://test.example/");
    EXPECT_EQ(title, "My Title");
    EXPECT_FALSE(excerpt.empty());
}

TEST_F(SegmentRoundTripTest, MissingDirectoryIsInvalid) {
    SegmentReader reader(dir_ / "nonexistent");
    EXPECT_FALSE(reader.is_valid());
}
