// test_discovery.cpp — Unit tests for SR, TNI/NG, and Grammar Registry.

#include "ali/discovery/source_registry.h"
#include "ali/discovery/topic_nav_index.h"
#include "ali/discovery/grammar_registry.h"
#include "ali/storage/metadata_db.h"

#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ali;

// ── Fixture ──────────────────────────────────────────────────────────────────

class DiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "ali_discovery_test";
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        db_  = std::make_unique<MetadataDb>(dir_ / "meta.db");
        ASSERT_TRUE(db_->open());
        sr_  = std::make_unique<SourceRegistry>(*db_);
        ASSERT_TRUE(sr_->ensure_schema());
        tni_ = std::make_unique<TopicNavIndex>(*db_);
        ASSERT_TRUE(tni_->ensure_schema());
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path                      dir_;
    std::unique_ptr<MetadataDb>   db_;
    std::unique_ptr<SourceRegistry> sr_;
    std::unique_ptr<TopicNavIndex>  tni_;
};

// ── SourceRegistry tests ──────────────────────────────────────────────────────

TEST_F(DiscoveryTest, UpsertAndFind) {
    SourceRecord r;
    r.domain    = "en.wikipedia.org";
    r.state     = SourceState::Profiled;
    r.authority = 230;
    ASSERT_TRUE(sr_->upsert(r));

    auto found = sr_->find("en.wikipedia.org");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->domain,    "en.wikipedia.org");
    EXPECT_EQ(found->state,     SourceState::Profiled);
    EXPECT_EQ(found->authority, 230u);
}

TEST_F(DiscoveryTest, FindMissingReturnsNullopt) {
    auto found = sr_->find("no-such-domain.example");
    EXPECT_FALSE(found.has_value());
}

TEST_F(DiscoveryTest, RecordFetchIncrementsCounters) {
    const int64_t now = 1000000;
    ASSERT_TRUE(sr_->record_fetch("example.com", /*useful=*/true, now));
    ASSERT_TRUE(sr_->record_fetch("example.com", /*useful=*/false, now + 1));
    ASSERT_TRUE(sr_->record_fetch("example.com", /*useful=*/true, now + 2));

    auto found = sr_->find("example.com");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->fetch_count,  3u);
    EXPECT_EQ(found->useful_count, 2u);
}

TEST_F(DiscoveryTest, UpdateAuthorityClampedTo255) {
    SourceRecord r; r.domain = "test.org"; r.authority = 250;
    ASSERT_TRUE(sr_->upsert(r));
    ASSERT_TRUE(sr_->update_authority("test.org", 20));  // would overflow to 270
    auto found = sr_->find("test.org");
    ASSERT_TRUE(found.has_value());
    EXPECT_LE(found->authority, 255u);
}

TEST_F(DiscoveryTest, SetStateTransitions) {
    SourceRecord r; r.domain = "news.bbc.com"; r.state = SourceState::Unknown;
    ASSERT_TRUE(sr_->upsert(r));
    ASSERT_TRUE(sr_->set_state("news.bbc.com", SourceState::Reliable));
    auto found = sr_->find("news.bbc.com");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->state, SourceState::Reliable);
}

TEST_F(DiscoveryTest, FindByStateReturnsMatchingRecords) {
    for (const auto& dom : {"a.com", "b.com", "c.com"}) {
        SourceRecord r; r.domain = dom; r.state = SourceState::Profiled; r.authority = 100;
        ASSERT_TRUE(sr_->upsert(r));
    }
    SourceRecord other; other.domain = "d.com"; other.state = SourceState::Unknown;
    ASSERT_TRUE(sr_->upsert(other));

    auto profiled = sr_->find_by_state(SourceState::Profiled, 100);
    EXPECT_EQ(profiled.size(), 3u);

    auto unknown = sr_->find_by_state(SourceState::Unknown, 100);
    EXPECT_EQ(unknown.size(), 1u);
    EXPECT_EQ(unknown[0].domain, "d.com");
}

TEST_F(DiscoveryTest, TopByAuthorityOrdered) {
    for (uint8_t auth : {50u, 200u, 150u}) {
        SourceRecord r;
        r.domain    = "dom" + std::to_string(auth) + ".com";
        r.authority = auth;
        r.state     = SourceState::Profiled;
        ASSERT_TRUE(sr_->upsert(r));
    }
    auto top = sr_->top_by_authority(3);
    ASSERT_EQ(top.size(), 3u);
    EXPECT_GE(top[0].authority, top[1].authority);
    EXPECT_GE(top[1].authority, top[2].authority);
}

TEST_F(DiscoveryTest, TotalCount) {
    EXPECT_EQ(sr_->total_count(), 0u);
    SourceRecord r; r.domain = "x.com";
    sr_->upsert(r);
    EXPECT_EQ(sr_->total_count(), 1u);
}

// ── MinHash tests ─────────────────────────────────────────────────────────────

TEST(MinHashTest, SameSetSameHash) {
    std::vector<std::string> terms = {"search", "engine", "index"};
    EXPECT_EQ(SourceRegistry::compute_minhash(terms),
              SourceRegistry::compute_minhash(terms));
}

TEST(MinHashTest, EmptySetZero) {
    EXPECT_EQ(SourceRegistry::compute_minhash({}), 0u);
}

TEST(MinHashTest, SimilarSetHighBitMatch) {
    std::vector<std::string> a = {"search", "engine", "index", "query"};
    std::vector<std::string> b = {"search", "engine", "index", "rank"};
    const auto ha = SourceRegistry::compute_minhash(a);
    const auto hb = SourceRegistry::compute_minhash(b);
    // 3/4 terms overlap → expect >40 matching bits out of 64
    EXPECT_GT(SourceRegistry::minhash_similarity_bits(ha, hb), 40u);
}

TEST(MinHashTest, DisjointSetLowBitMatch) {
    std::vector<std::string> a = {"apple", "orange", "banana"};
    std::vector<std::string> b = {"quantum", "physics", "neutron"};
    const auto ha = SourceRegistry::compute_minhash(a);
    const auto hb = SourceRegistry::compute_minhash(b);
    // Different terms → expect fewer than 40 matching bits
    EXPECT_LT(SourceRegistry::minhash_similarity_bits(ha, hb), 40u);
}

// ── TopicNavIndex tests ───────────────────────────────────────────────────────

TEST_F(DiscoveryTest, AddEdgeAndRetrieve) {
    NavEdge e;
    e.from_domain   = "example.com";
    e.to_url        = "https://other.org/page";
    e.to_domain     = "other.org";
    e.type          = NavEdgeType::Hyperlink;
    e.weight        = 200;
    e.discovered_at = 1000;
    ASSERT_TRUE(tni_->add_edge(e));

    auto edges = tni_->edges_from("example.com");
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].to_url, "https://other.org/page");
    EXPECT_EQ(edges[0].weight, 200u);
}

TEST_F(DiscoveryTest, AddEdgesDuplicateIgnored) {
    NavEdge e; e.from_domain="x.com"; e.to_url="https://y.com/"; e.to_domain="y.com"; e.weight=100;
    ASSERT_TRUE(tni_->add_edge(e));
    ASSERT_TRUE(tni_->add_edge(e));  // duplicate — ON CONFLICT DO NOTHING
    EXPECT_EQ(tni_->edge_count(), 1u);
}

TEST_F(DiscoveryTest, FrontierEdgesExcludesProfiledDomains) {
    // Edge pointing to a domain that has been profiled should not appear in frontier.
    NavEdge e1; e1.from_domain="src.com"; e1.to_url="https://profiled.org/"; e1.to_domain="profiled.org";
    NavEdge e2; e2.from_domain="src.com"; e2.to_url="https://unknown.org/";  e2.to_domain="unknown.org";
    ASSERT_TRUE(tni_->add_edge(e1));
    ASSERT_TRUE(tni_->add_edge(e2));

    // Profile "profiled.org" in source registry.
    SourceRecord sr; sr.domain="profiled.org"; sr.state=SourceState::Profiled;
    ASSERT_TRUE(sr_->upsert(sr));

    auto frontier = tni_->frontier_edges(10);
    // Only unknown.org should appear.
    ASSERT_EQ(frontier.size(), 1u);
    EXPECT_EQ(frontier[0].to_domain, "unknown.org");
}

TEST_F(DiscoveryTest, RecordHitIncrements) {
    const uint64_t topic = 0xDEADBEEFCAFEBABEULL;
    ASSERT_TRUE(tni_->record_hit(topic, "wikipedia.org", 1000));
    ASSERT_TRUE(tni_->record_hit(topic, "wikipedia.org", 2000));
    ASSERT_TRUE(tni_->record_hit(topic, "britannica.com", 1500));

    auto entries = tni_->find_for_topic(topic, 0, 10);
    // All entries match (0 min bits).
    EXPECT_GE(entries.size(), 2u);
    // Wikipedia hit twice → higher hit_count.
    auto wiki_it = std::find_if(entries.begin(), entries.end(),
        [](const TniEntry& e) { return e.domain == "wikipedia.org"; });
    ASSERT_NE(wiki_it, entries.end());
    EXPECT_EQ(wiki_it->hit_count, 2u);
}

// ── Grammar Registry tests ────────────────────────────────────────────────────

TEST(GrammarTest, LoadDefaults) {
    GrammarRegistry gr;
    gr.load_defaults();
    EXPECT_GE(gr.size(), 40u);
}

TEST(GrammarTest, FactoidQueryMatchesWikipedia) {
    GrammarRegistry gr;
    gr.load_defaults();
    auto results = gr.construct("who invented the microwave", 5);
    ASSERT_FALSE(results.empty());
    bool found_wiki = false;
    for (const auto& r : results) {
        if (r.grammar_name.find("Wikipedia") != std::string::npos) {
            found_wiki = true;
            EXPECT_FALSE(r.url.empty());
            EXPECT_GT(r.authority, 0u);
        }
    }
    EXPECT_TRUE(found_wiki);
}

TEST(GrammarTest, TechnicalQueryMatchesStackOverflow) {
    GrammarRegistry gr;
    gr.load_defaults();
    auto results = gr.construct("how to fix python import error", 10);
    ASSERT_FALSE(results.empty());
    bool found_so = false;
    for (const auto& r : results) {
        if (r.grammar_name.find("Stack Overflow") != std::string::npos) found_so = true;
    }
    EXPECT_TRUE(found_so);
}

TEST(GrammarTest, AcademicQueryMatchesArxiv) {
    GrammarRegistry gr;
    gr.load_defaults();
    auto results = gr.construct("research paper quantum entanglement", 10);
    bool found_arxiv = false;
    for (const auto& r : results) {
        if (r.grammar_name.find("arXiv") != std::string::npos) found_arxiv = true;
    }
    EXPECT_TRUE(found_arxiv);
}

TEST(GrammarTest, NormaliseTopic) {
    // "who invented the microwave oven?" strips "who invented " → "the microwave oven" → "The_Microwave_Oven"
    EXPECT_EQ(GrammarRegistry::normalise_topic("who invented the microwave oven?"),
              "The_Microwave_Oven");
    // "what is machine learning" strips "what is " → "machine learning" → "Machine_Learning"
    EXPECT_EQ(GrammarRegistry::normalise_topic("what is machine learning"),
              "Machine_Learning");
    // Proper noun — each word capitalised
    EXPECT_EQ(GrammarRegistry::normalise_topic("Albert Einstein"),
              "Albert_Einstein");
}

TEST(GrammarTest, ConstructedUrlsAreNonEmpty) {
    GrammarRegistry gr;
    gr.load_defaults();
    auto results = gr.construct("quantum computing", 5);
    for (const auto& r : results) {
        EXPECT_FALSE(r.url.empty()) << "Grammar: " << r.grammar_name;
        EXPECT_TRUE(r.url.find("http") == 0) << "URL should start with http";
    }
}

TEST(GrammarTest, QueryClassifier) {
    EXPECT_EQ(classify_query("who invented the telephone"),    QueryType::Factoid);
    EXPECT_EQ(classify_query("how to use docker compose"),    QueryType::Technical);
    EXPECT_EQ(classify_query("arxiv paper neural networks"),  QueryType::Academic);
    EXPECT_EQ(classify_query("breaking news today"),          QueryType::News);
}
