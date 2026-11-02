#pragma once
// ali/discovery/topic_nav_index.h — Topic Navigation Index (TNI) and
//                                   Navigation Graph (NG)
//
// TNI: maps query topic fingerprints (MinHash) to promising source domains.
//      Used at query time to select which sources to check first.
//
// NG:  stores discovered link/sitemap/feed/citation edges between sources.
//      Used by the discovery engine to navigate to new sources.
//
// Both are persisted in MetadataDb (tni_entries and nav_edges tables).

#include "ali/storage/metadata_db.h"
#include "ali/discovery/source_registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ali {

// ── Navigation Graph ─────────────────────────────────────────────────────────

enum class NavEdgeType : uint8_t {
    Hyperlink    = 0,  ///< <a href> link found during crawl
    SitemapEntry = 1,  ///< URL discovered from sitemap.xml
    FeedItem     = 2,  ///< URL from RSS/Atom feed
    Citation     = 3,  ///< Academic/editorial citation
    GrammarTarget= 4,  ///< URL produced by a Grammar Registry rule
};

struct NavEdge {
    uint64_t    edge_id       = 0;
    std::string from_domain;      ///< source domain (or "" for query-seeded)
    std::string to_url;           ///< discovered target URL
    std::string to_domain;        ///< domain of to_url
    NavEdgeType type          = NavEdgeType::Hyperlink;
    uint8_t     weight        = 128;  ///< 0–255; higher = more confidence
    int64_t     discovered_at = 0;    ///< unix seconds
};

// ── TNI Entry ────────────────────────────────────────────────────────────────

/// One TNI record: a (topic_minhash, domain) pair with a relevance weight.
struct TniEntry {
    uint64_t    tni_id        = 0;
    uint64_t    topic_minhash = 0;  ///< query's MinHash fingerprint
    std::string domain;
    uint8_t     relevance     = 128;  ///< 0–255 estimated relevance
    uint32_t    hit_count     = 0;    ///< number of useful query results from domain
    int64_t     last_hit_at   = 0;
};

// ── TopicNavIndex ─────────────────────────────────────────────────────────────

class TopicNavIndex {
public:
    explicit TopicNavIndex(MetadataDb& db);

    /// Create tables if they don't exist.
    [[nodiscard]] bool ensure_schema();

    // ── TNI write ─────────────────────────────────────────────────────────

    /// Record that `domain` was useful for `topic_minhash`.
    [[nodiscard]] bool record_hit(uint64_t    topic_minhash,
                                   const std::string& domain,
                                   int64_t            now_unix);

    // ── TNI read ──────────────────────────────────────────────────────────

    /// Find domains likely relevant to `query_minhash`.
    /// Returns up to `limit` domains sorted by relevance × hit_count.
    [[nodiscard]] std::vector<TniEntry>
        find_for_topic(uint64_t query_minhash,
                       uint32_t min_matching_bits = 32,
                       uint32_t limit             = 20) const;

    // ── Navigation Graph write ────────────────────────────────────────────

    [[nodiscard]] bool add_edge(const NavEdge& edge);
    [[nodiscard]] bool add_edges(const std::vector<NavEdge>& edges);

    // ── Navigation Graph read ─────────────────────────────────────────────

    /// All edges discovered from a given domain.
    [[nodiscard]] std::vector<NavEdge>
        edges_from(const std::string& from_domain, uint32_t limit = 100) const;

    /// Unvisited edges pointing to domains in Unknown/Promising state.
    [[nodiscard]] std::vector<NavEdge>
        frontier_edges(uint32_t limit = 50) const;

    [[nodiscard]] uint32_t edge_count() const;
    [[nodiscard]] uint32_t tni_entry_count() const;

private:
    MetadataDb& db_;
};

} // namespace ali
