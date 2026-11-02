// topic_nav_index.cpp — Topic Navigation Index and Navigation Graph.

#include "ali/discovery/topic_nav_index.h"
#include "ali/discovery/source_registry.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstdio>

namespace ali {

// ── DDL ──────────────────────────────────────────────────────────────────────

static const char* kTniSchema = R"sql(
CREATE TABLE IF NOT EXISTS tni_entries (
    tni_id         INTEGER PRIMARY KEY,
    topic_minhash  INTEGER NOT NULL,
    domain         TEXT    NOT NULL,
    relevance      INTEGER NOT NULL DEFAULT 128,
    hit_count      INTEGER NOT NULL DEFAULT 1,
    last_hit_at    INTEGER NOT NULL DEFAULT 0,
    UNIQUE(topic_minhash, domain)
);
CREATE INDEX IF NOT EXISTS idx_tni_topic ON tni_entries(topic_minhash);
CREATE INDEX IF NOT EXISTS idx_tni_domain ON tni_entries(domain);
)sql";

static const char* kNavSchema = R"sql(
CREATE TABLE IF NOT EXISTS nav_edges (
    edge_id        INTEGER PRIMARY KEY,
    from_domain    TEXT    NOT NULL DEFAULT '',
    to_url         TEXT    NOT NULL,
    to_domain      TEXT    NOT NULL DEFAULT '',
    edge_type      INTEGER NOT NULL DEFAULT 0,
    weight         INTEGER NOT NULL DEFAULT 128,
    discovered_at  INTEGER NOT NULL DEFAULT 0,
    UNIQUE(from_domain, to_url)
);
CREATE INDEX IF NOT EXISTS idx_nav_from   ON nav_edges(from_domain);
CREATE INDEX IF NOT EXISTS idx_nav_to_dom ON nav_edges(to_domain);
)sql";

// ── Helpers ───────────────────────────────────────────────────────────────────

static TniEntry row_to_tni(void* raw) {
    auto* s = static_cast<sqlite3_stmt*>(raw);
    TniEntry e;
    e.tni_id        = static_cast<uint64_t>(sqlite3_column_int64(s, 0));
    e.topic_minhash = static_cast<uint64_t>(sqlite3_column_int64(s, 1));
    const char* d   = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
    e.domain        = d ? d : "";
    e.relevance     = static_cast<uint8_t>(sqlite3_column_int(s, 3));
    e.hit_count     = static_cast<uint32_t>(sqlite3_column_int64(s, 4));
    e.last_hit_at   = sqlite3_column_int64(s, 5);
    return e;
}

static NavEdge row_to_edge(void* raw) {
    auto* s = static_cast<sqlite3_stmt*>(raw);
    NavEdge e;
    e.edge_id       = static_cast<uint64_t>(sqlite3_column_int64(s, 0));
    const char* fd  = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
    e.from_domain   = fd ? fd : "";
    const char* tu  = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
    e.to_url        = tu ? tu : "";
    const char* td  = reinterpret_cast<const char*>(sqlite3_column_text(s, 3));
    e.to_domain     = td ? td : "";
    e.type          = static_cast<NavEdgeType>(sqlite3_column_int(s, 4));
    e.weight        = static_cast<uint8_t>(sqlite3_column_int(s, 5));
    e.discovered_at = sqlite3_column_int64(s, 6);
    return e;
}

// ── TopicNavIndex ─────────────────────────────────────────────────────────────

TopicNavIndex::TopicNavIndex(MetadataDb& db) : db_(db) {}

bool TopicNavIndex::ensure_schema() {
    return db_.begin_transaction()
        && db_.exec_raw(kTniSchema)
        && db_.exec_raw(kNavSchema)
        && db_.commit_transaction();
}

// ── TNI write ─────────────────────────────────────────────────────────────────

bool TopicNavIndex::record_hit(uint64_t           topic_minhash,
                                const std::string& domain,
                                int64_t            now_unix) {
    const char* sql =
        "INSERT INTO tni_entries(topic_minhash, domain, hit_count, last_hit_at) "
        "VALUES(?,?,1,?) "
        "ON CONFLICT(topic_minhash, domain) DO UPDATE SET "
        "hit_count    = hit_count + 1, "
        "last_hit_at  = ?, "
        "relevance    = MIN(255, relevance + 2)";
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_int64(s, 1, static_cast<int64_t>(topic_minhash));
        sqlite3_bind_text(s,  2, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(s, 3, now_unix);
        sqlite3_bind_int64(s, 4, now_unix);
    });
}

// ── TNI read ──────────────────────────────────────────────────────────────────

std::vector<TniEntry> TopicNavIndex::find_for_topic(uint64_t query_minhash,
                                                      uint32_t min_bits,
                                                      uint32_t limit) const {
    const char* sql =
        "SELECT tni_id, topic_minhash, domain, relevance, hit_count, last_hit_at "
        "FROM tni_entries ORDER BY relevance DESC LIMIT 2000";
    std::vector<TniEntry> all;
    db_.exec_query(sql, [](void*) {},
        [&](void* raw) { all.push_back(row_to_tni(raw)); });

    std::vector<TniEntry> result;
    for (auto& e : all) {
        if (SourceRegistry::minhash_similarity_bits(e.topic_minhash, query_minhash)
                >= min_bits) {
            result.push_back(std::move(e));
            if (static_cast<uint32_t>(result.size()) >= limit) break;
        }
    }
    return result;
}

// ── Nav Graph write ───────────────────────────────────────────────────────────

bool TopicNavIndex::add_edge(const NavEdge& e) {
    const char* sql =
        "INSERT INTO nav_edges(from_domain, to_url, to_domain, edge_type, weight, discovered_at) "
        "VALUES(?,?,?,?,?,?) "
        "ON CONFLICT(from_domain, to_url) DO UPDATE SET "
        "weight = MAX(weight, excluded.weight)";
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_text(s,  1, e.from_domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s,  2, e.to_url.c_str(),      -1, SQLITE_STATIC);
        sqlite3_bind_text(s,  3, e.to_domain.c_str(),   -1, SQLITE_STATIC);
        sqlite3_bind_int(s,   4, static_cast<int>(e.type));
        sqlite3_bind_int(s,   5, e.weight);
        sqlite3_bind_int64(s, 6, e.discovered_at);
    });
}

bool TopicNavIndex::add_edges(const std::vector<NavEdge>& edges) {
    if (edges.empty()) return true;
    if (!db_.begin_transaction()) return false;
    for (const auto& e : edges) {
        if (!add_edge(e)) {
            db_.rollback_transaction();
            return false;
        }
    }
    return db_.commit_transaction();
}

// ── Nav Graph read ────────────────────────────────────────────────────────────

std::vector<NavEdge> TopicNavIndex::edges_from(const std::string& from_domain,
                                                uint32_t           limit) const {
    const char* sql =
        "SELECT edge_id, from_domain, to_url, to_domain, edge_type, weight, discovered_at "
        "FROM nav_edges WHERE from_domain = ? ORDER BY weight DESC LIMIT ?";
    std::vector<NavEdge> results;
    db_.exec_query(sql,
        [&](void* raw) {
            auto* s = static_cast<sqlite3_stmt*>(raw);
            sqlite3_bind_text(s, 1, from_domain.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(s,  2, static_cast<int>(limit));
        },
        [&](void* raw) { results.push_back(row_to_edge(raw)); });
    return results;
}

std::vector<NavEdge> TopicNavIndex::frontier_edges(uint32_t limit) const {
    const char* sql =
        "SELECT e.edge_id, e.from_domain, e.to_url, e.to_domain, "
        "e.edge_type, e.weight, e.discovered_at "
        "FROM nav_edges e "
        "LEFT JOIN sources s ON s.domain = e.to_domain "
        "WHERE s.source_id IS NULL OR s.state <= 1 "
        "ORDER BY e.weight DESC LIMIT ?";
    std::vector<NavEdge> results;
    db_.exec_query(sql,
        [&](void* raw) {
            sqlite3_bind_int(static_cast<sqlite3_stmt*>(raw), 1,
                             static_cast<int>(limit));
        },
        [&](void* raw) { results.push_back(row_to_edge(raw)); });
    return results;
}

uint32_t TopicNavIndex::edge_count()       const { return db_.count("nav_edges");    }
uint32_t TopicNavIndex::tni_entry_count()  const { return db_.count("tni_entries");  }

} // namespace ali
