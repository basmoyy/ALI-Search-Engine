// source_registry.cpp — Source Registry (SR) implementation.
//
// Persisted in the MetadataDb `sources` table via generic SQL helpers.

#include "ali/discovery/source_registry.h"

#include <sqlite3.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>

namespace ali {

// ── DDL ──────────────────────────────────────────────────────────────────────

static const char* kSourcesSchema = R"sql(
CREATE TABLE IF NOT EXISTS sources (
    source_id        INTEGER PRIMARY KEY,
    domain           TEXT    NOT NULL UNIQUE,
    state            INTEGER NOT NULL DEFAULT 0,
    authority        INTEGER NOT NULL DEFAULT 128,
    topic_minhash    INTEGER NOT NULL DEFAULT 0,
    first_seen_at    INTEGER NOT NULL DEFAULT 0,
    last_fetched_at  INTEGER NOT NULL DEFAULT 0,
    last_useful_at   INTEGER NOT NULL DEFAULT 0,
    fetch_count      INTEGER NOT NULL DEFAULT 0,
    useful_count     INTEGER NOT NULL DEFAULT 0,
    fail_count       INTEGER NOT NULL DEFAULT 0,
    sitemap_url      TEXT    NOT NULL DEFAULT '',
    homepage_excerpt TEXT    NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_sources_state     ON sources(state);
CREATE INDEX IF NOT EXISTS idx_sources_authority ON sources(authority DESC);
CREATE INDEX IF NOT EXISTS idx_sources_topic     ON sources(topic_minhash);
)sql";

// ── Helpers ───────────────────────────────────────────────────────────────────

static SourceRecord row_to_source(void* raw) {
    auto* s = static_cast<sqlite3_stmt*>(raw);
    SourceRecord r;
    r.source_id        = static_cast<uint64_t>(sqlite3_column_int64(s, 0));
    const char* dom    = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
    r.domain           = dom ? dom : "";
    r.state            = static_cast<SourceState>(sqlite3_column_int(s, 2));
    r.authority        = static_cast<uint8_t>(sqlite3_column_int(s, 3));
    r.topic_minhash    = static_cast<uint64_t>(sqlite3_column_int64(s, 4));
    r.first_seen_at    = sqlite3_column_int64(s, 5);
    r.last_fetched_at  = sqlite3_column_int64(s, 6);
    r.last_useful_at   = sqlite3_column_int64(s, 7);
    r.fetch_count      = static_cast<uint32_t>(sqlite3_column_int64(s, 8));
    r.useful_count     = static_cast<uint32_t>(sqlite3_column_int64(s, 9));
    r.fail_count       = static_cast<uint32_t>(sqlite3_column_int64(s, 10));
    const char* sit    = reinterpret_cast<const char*>(sqlite3_column_text(s, 11));
    r.sitemap_url      = sit ? sit : "";
    const char* exc    = reinterpret_cast<const char*>(sqlite3_column_text(s, 12));
    r.homepage_excerpt = exc ? exc : "";
    return r;
}

static const char* kSelectAll =
    "SELECT source_id, domain, state, authority, topic_minhash, "
    "first_seen_at, last_fetched_at, last_useful_at, "
    "fetch_count, useful_count, fail_count, sitemap_url, homepage_excerpt";

// ── SourceRegistry ────────────────────────────────────────────────────────────

SourceRegistry::SourceRegistry(MetadataDb& db) : db_(db) {}

bool SourceRegistry::ensure_schema() {
    return db_.begin_transaction()
        && db_.exec_raw(kSourcesSchema)
        && db_.commit_transaction();
}

bool SourceRegistry::upsert(const SourceRecord& r) {
    const char* sql =
        "INSERT INTO sources(domain, state, authority, topic_minhash, "
        "first_seen_at, last_fetched_at, last_useful_at, "
        "fetch_count, useful_count, fail_count, sitemap_url, homepage_excerpt) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(domain) DO UPDATE SET "
        "state=excluded.state, authority=excluded.authority, "
        "topic_minhash=excluded.topic_minhash, "
        "last_fetched_at=excluded.last_fetched_at, "
        "last_useful_at=excluded.last_useful_at, "
        "fetch_count=excluded.fetch_count, "
        "useful_count=excluded.useful_count, "
        "fail_count=excluded.fail_count, "
        "sitemap_url=excluded.sitemap_url, "
        "homepage_excerpt=excluded.homepage_excerpt";
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_text(s,  1, r.domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(s,   2, static_cast<int>(r.state));
        sqlite3_bind_int(s,   3, r.authority);
        sqlite3_bind_int64(s, 4, static_cast<int64_t>(r.topic_minhash));
        sqlite3_bind_int64(s, 5, r.first_seen_at);
        sqlite3_bind_int64(s, 6, r.last_fetched_at);
        sqlite3_bind_int64(s, 7, r.last_useful_at);
        sqlite3_bind_int64(s, 8, static_cast<int64_t>(r.fetch_count));
        sqlite3_bind_int64(s, 9, static_cast<int64_t>(r.useful_count));
        sqlite3_bind_int64(s,10, static_cast<int64_t>(r.fail_count));
        sqlite3_bind_text(s, 11, r.sitemap_url.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 12, r.homepage_excerpt.c_str(), -1, SQLITE_STATIC);
    });
}

bool SourceRegistry::record_fetch(const std::string& domain,
                                   bool               was_useful,
                                   int64_t            now_unix) {
    const char* sql =
        "INSERT INTO sources(domain, state, fetch_count, useful_count, "
        "last_fetched_at, last_useful_at, first_seen_at) "
        "VALUES(?,2,1,?,?,?,?) "
        "ON CONFLICT(domain) DO UPDATE SET "
        "fetch_count = fetch_count + 1, "
        "useful_count = useful_count + ?, "
        "last_fetched_at = ?, "
        "last_useful_at  = CASE WHEN ? THEN ? ELSE last_useful_at END, "
        "state = CASE WHEN state < 2 THEN 2 ELSE state END";
    const int64_t useful = was_useful ? 1 : 0;
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_text(s,  1, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(s, 2, useful);
        sqlite3_bind_int64(s, 3, now_unix);
        sqlite3_bind_int64(s, 4, was_useful ? now_unix : 0);
        sqlite3_bind_int64(s, 5, now_unix);
        sqlite3_bind_int64(s, 6, useful);
        sqlite3_bind_int64(s, 7, now_unix);
        sqlite3_bind_int64(s, 8, useful);
        sqlite3_bind_int64(s, 9, now_unix);
    });
}

bool SourceRegistry::update_authority(const std::string& domain, int8_t delta) {
    const char* sql =
        "UPDATE sources SET authority = MAX(0, MIN(255, CAST(authority AS INTEGER) + ?)) "
        "WHERE domain = ?";
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_int(s,  1, static_cast<int>(delta));
        sqlite3_bind_text(s, 2, domain.c_str(), -1, SQLITE_STATIC);
    });
}

bool SourceRegistry::set_state(const std::string& domain, SourceState state) {
    const char* sql =
        "INSERT INTO sources(domain, state) VALUES(?,?) "
        "ON CONFLICT(domain) DO UPDATE SET state=excluded.state";
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_text(s, 1, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(s,  2, static_cast<int>(state));
    });
}

bool SourceRegistry::set_topic_minhash(const std::string& domain, uint64_t minhash) {
    const char* sql =
        "INSERT INTO sources(domain, topic_minhash) VALUES(?,?) "
        "ON CONFLICT(domain) DO UPDATE SET topic_minhash=excluded.topic_minhash";
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_text(s, 1, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(s,2, static_cast<int64_t>(minhash));
    });
}

bool SourceRegistry::set_sitemap_url(const std::string& domain,
                                      const std::string& sitemap_url) {
    const char* sql =
        "INSERT INTO sources(domain, sitemap_url) VALUES(?,?) "
        "ON CONFLICT(domain) DO UPDATE SET sitemap_url=excluded.sitemap_url";
    return db_.exec_bound(sql, [&](void* raw) {
        auto* s = static_cast<sqlite3_stmt*>(raw);
        sqlite3_bind_text(s, 1, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 2, sitemap_url.c_str(), -1, SQLITE_STATIC);
    });
}

// ── Read ──────────────────────────────────────────────────────────────────────

std::optional<SourceRecord> SourceRegistry::find(const std::string& domain) const {
    std::string sql = std::string(kSelectAll) +
                      " FROM sources WHERE domain = ? LIMIT 1";
    std::optional<SourceRecord> result;
    db_.exec_query(sql.c_str(),
        [&](void* raw) {
            sqlite3_bind_text(static_cast<sqlite3_stmt*>(raw), 1,
                              domain.c_str(), -1, SQLITE_STATIC);
        },
        [&](void* raw) {
            if (!result) result = row_to_source(raw);
        });
    return result;
}

std::vector<SourceRecord> SourceRegistry::find_by_state(SourceState state,
                                                          uint32_t    limit) const {
    std::string sql = std::string(kSelectAll) +
                      " FROM sources WHERE state = ? ORDER BY authority DESC LIMIT ?";
    std::vector<SourceRecord> results;
    db_.exec_query(sql.c_str(),
        [&](void* raw) {
            auto* s = static_cast<sqlite3_stmt*>(raw);
            sqlite3_bind_int(s, 1, static_cast<int>(state));
            sqlite3_bind_int(s, 2, static_cast<int>(limit));
        },
        [&](void* raw) { results.push_back(row_to_source(raw)); });
    return results;
}

std::vector<SourceRecord> SourceRegistry::find_by_topic(uint64_t query_minhash,
                                                          uint32_t min_bits,
                                                          uint32_t limit) const {
    std::string sql = std::string(kSelectAll) +
                      " FROM sources WHERE state >= 2 ORDER BY authority DESC LIMIT 500";
    std::vector<SourceRecord> candidates;
    db_.exec_query(sql.c_str(), [](void*) {},
        [&](void* raw) { candidates.push_back(row_to_source(raw)); });

    std::vector<SourceRecord> result;
    for (auto& r : candidates) {
        if (minhash_similarity_bits(r.topic_minhash, query_minhash) >= min_bits) {
            result.push_back(std::move(r));
            if (static_cast<uint32_t>(result.size()) >= limit) break;
        }
    }
    std::sort(result.begin(), result.end(),
              [](const SourceRecord& a, const SourceRecord& b) {
                  return a.authority > b.authority;
              });
    return result;
}

std::vector<SourceRecord> SourceRegistry::top_by_authority(uint32_t limit) const {
    std::string sql = std::string(kSelectAll) +
                      " FROM sources ORDER BY authority DESC LIMIT ?";
    std::vector<SourceRecord> results;
    db_.exec_query(sql.c_str(),
        [&](void* raw) {
            sqlite3_bind_int(static_cast<sqlite3_stmt*>(raw), 1,
                             static_cast<int>(limit));
        },
        [&](void* raw) { results.push_back(row_to_source(raw)); });
    return results;
}

uint32_t SourceRegistry::total_count() const {
    return db_.count("sources");
}

// ── MinHash ───────────────────────────────────────────────────────────────────
// 64-bit MinHash using FNV-1a as the base hash.
// For each of 64 independent hash functions h_i, we record the LSB of
// min_{t in terms} h_i(t) as bit i of the sketch.

static uint64_t fnv1a_64(const std::string& s, uint64_t seed) noexcept {
    uint64_t h = seed ^ 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t SourceRegistry::compute_minhash(const std::vector<std::string>& terms) {
    if (terms.empty()) return 0;
    static constexpr uint64_t kSeeds[64] = {
        0x9368u,0x1a4du,0x7f2cu,0x8b01u,0x3e9fu,0xc45au,0x27beu,0xd107u,
        0x6539u,0x4e82u,0xf0d3u,0x5176u,0x9ae4u,0x0c8bu,0xba3cu,0x7250u,
        0xe91fu,0x3467u,0xab56u,0x18ceu,0x704du,0xdf21u,0x6bfau,0x9304u,
        0x2e8au,0x5641u,0xc7b9u,0x1e3du,0x8902u,0xf5cbu,0x4a67u,0x0d1eu,
        0x7349u,0xbc82u,0x2f5cu,0x91a0u,0x4e27u,0xd8f3u,0x6045u,0x1b9eu,
        0xa37cu,0x5e01u,0x8fd4u,0x2c6bu,0x70b5u,0xe348u,0x9a1fu,0x3782u,
        0xcd56u,0x0e9au,0x5243u,0xb70cu,0x69e1u,0x1c4fu,0xa825u,0x3db8u,
        0xf061u,0x7594u,0xc2a3u,0x4e16u,0x9b7du,0x28f0u,0x6e45u,0xd391u,
    };
    uint64_t sketch = 0;
    for (uint32_t bit = 0; bit < 64; ++bit) {
        uint64_t min_val = UINT64_MAX;
        for (const auto& t : terms) {
            const uint64_t h = fnv1a_64(t, kSeeds[bit]);
            if (h < min_val) min_val = h;
        }
        if (min_val & 1u) sketch |= (uint64_t(1) << bit);
    }
    return sketch;
}

uint32_t SourceRegistry::minhash_similarity_bits(uint64_t a, uint64_t b) noexcept {
    // Count matching bits = popcount of XNOR = popcount(~(a^b))
    return static_cast<uint32_t>(__builtin_popcountll(~(a ^ b)));
}

} // namespace ali
