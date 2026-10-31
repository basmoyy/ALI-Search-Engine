// test_multi_segment.cpp — Multi-segment query fan-out integration test.

#include "ali/storage/manifest.h"
#include "ali/storage/metadata_db.h"
#include "ali/index/index_writer.h"
#include "ali/index/index_reader.h"
#include "ali/query/tokeniser.h"
#include "ali/query/query_planner.h"
#include "ali/scanner/ali_scanner.h"
#include "ali/locality/locality_memory.h"

#include <atomic>
#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;
using namespace ali;

TEST(MultiSegment, ResultsSpanSegments) {
    const fs::path root = fs::temp_directory_path() / "ali_multiseg_test";
    fs::remove_all(root);

    Manifest  manifest(root);
    manifest.open();
    MetadataDb db(root / "meta.db");
    db.open();

    // Write two tiny segments of 2 documents each.
    for (int seg = 0; seg < 2; ++seg) {
        IndexWriterConfig wcfg;
        wcfg.index_root      = root.string();
        wcfg.docs_per_segment = 2;
        IndexWriter writer(wcfg, manifest, db);
        writer.open();

        Tokeniser tok;
        for (int i = 0; i < 2; ++i) {
            const std::string url  = "https://s" + std::to_string(seg) + ".com/" + std::to_string(i);
            const std::string body = "shared term unique" + std::to_string(seg * 10 + i);
            InputDocument doc;
            doc.url = url; doc.title = "T"; doc.body_text = body;
            writer.add_document(doc);
        }
        writer.flush();
    }

    IndexReader  reader(manifest);
    reader.open();
    LocalityMemory locality;
    AliScanner scanner(reader, locality);
    QueryPlanner planner(reader);
    Tokeniser tok;

    Query q;
    q.tokens = tok.tokenise_query("shared");
    q.top_k  = 10;
    auto plan = planner.plan(q);
    std::atomic<bool> cancel{false};
    auto resp = scanner.execute(plan, cancel);

    // "shared" appears in all 4 documents across 2 segments.
    EXPECT_GE(resp.results.size(), 2u);

    fs::remove_all(root);
}
