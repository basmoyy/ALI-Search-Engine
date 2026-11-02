#pragma once
// ali/discovery/source_registry.h — Source Registry (SR)
//
// Tracks discovered sources at the domain level: reliability, authority,
// freshness, topic fingerprint, and crawl history. This is the SR component
// from the V1 SDE architecture.
//
// Design:
//   - Persisted in SQLite (sources table in MetadataDb).
//   - Keyed by domain string (e.g. "en.wikipedia.org").
//   - Topic fingerprint uses a 64-bit MinHash sketch computed from the
//     most significant terms seen across fetched pages from that domain.
//   - Authority score is a uint8 (0–255). Starts at 128 for unknown sources.
//     Increases when the domain produces results that match queries; decreases
//     when fetches fail or produce irrelevant content.
//   - SourceState mirrors the URL CrawlStatus idea but at the source level.

#include "ali/storage/metadata_db.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ali {

/// State of a known source (domain-level).
enum class SourceState : uint8_t {
    Unknown     = 0,  ///< Seen in links/DNS but never profiled
    Promising   = 1,  ///< Estimated relevant from structural signals
    Profiled    = 2,  ///< Homepage/sitemap fetched; topic fingerprint known
    Reliable    = 3,  ///< Consistently produced useful results
    Stale       = 4,  ///< Was reliable but not checked recently
    Irrelevant  = 5,  ///< Profiled but topic fingerprint doesn't match queries
    Restricted  = 6,  ///< robots.txt / 403 / auth-required
    Unavailable = 7,  ///< Repeated fetch failures
};

/// Domain-level source record.
struct SourceRecord {
    uint64_t    source_id       = 0;         ///< internal rowid
    std::string domain;                       ///< e.g. "en.wikipedia.org"
    SourceState state           = SourceState::Unknown;
    uint8_t     authority       = 128;        ///< 0–255; 128 = neutral/unknown
    uint64_t    topic_minhash   = 0;          ///< 64-bit MinHash topic sketch
    int64_t     first_seen_at   = 0;          ///< unix seconds
    int64_t     last_fetched_at = 0;          ///< unix seconds; 0 = never
    int64_t     last_useful_at  = 0;          ///< unix seconds of last useful result
    uint32_t    fetch_count     = 0;
    uint32_t    useful_count    = 0;
    uint32_t    fail_count      = 0;
    std::string sitemap_url;                  ///< discovered sitemap URL if any
    std::string homepage_excerpt;             ///< first 256 chars of homepage text
};

/// Reliability estimate from a SourceRecord (0.0–1.0).
inline float source_reliability(const SourceRecord& r) noexcept {
    if (r.fetch_count == 0) return 0.5f;   // neutral for unknown
    const float denom = static_cast<float>(r.fetch_count);
    return static_cast<float>(r.useful_count) / denom;
}

/// Manages domain-level source records in the MetadataDb.
class SourceRegistry {
public:
    explicit SourceRegistry(MetadataDb& db);

    /// Ensure the sources table exists (called once at engine open).
    [[nodiscard]] bool ensure_schema();

    // ── Write ──────────────────────────────────────────────────────────────

    [[nodiscard]] bool upsert(const SourceRecord& rec);

    /// Bump the fetch counter and optionally mark as useful.
    [[nodiscard]] bool record_fetch(const std::string& domain,
                                     bool               was_useful,
                                     int64_t            now_unix);

    /// Update the authority score (called after ranking feedback).
    [[nodiscard]] bool update_authority(const std::string& domain,
                                         int8_t             delta);

    [[nodiscard]] bool set_state(const std::string& domain, SourceState state);
    [[nodiscard]] bool set_topic_minhash(const std::string& domain, uint64_t minhash);
    [[nodiscard]] bool set_sitemap_url(const std::string& domain,
                                        const std::string& sitemap_url);

    // ── Read ───────────────────────────────────────────────────────────────

    [[nodiscard]] std::optional<SourceRecord> find(const std::string& domain) const;

    /// All sources in a given state, up to limit.
    [[nodiscard]] std::vector<SourceRecord>
        find_by_state(SourceState state, uint32_t limit = 256) const;

    /// Sources whose topic_minhash shares at least min_bits bits with the
    /// query's MinHash. Returns up to limit results sorted by authority desc.
    [[nodiscard]] std::vector<SourceRecord>
        find_by_topic(uint64_t query_minhash,
                      uint32_t min_matching_bits = 32,
                      uint32_t limit             = 20) const;

    /// Top N sources by authority score.
    [[nodiscard]] std::vector<SourceRecord>
        top_by_authority(uint32_t limit = 50) const;

    [[nodiscard]] uint32_t total_count() const;

    // ── MinHash helpers ────────────────────────────────────────────────────

    /// Compute a 64-bit MinHash sketch from a list of terms.
    static uint64_t compute_minhash(const std::vector<std::string>& terms);

    /// Count matching bits between two MinHash sketches (popcount of XNOR).
    static uint32_t minhash_similarity_bits(uint64_t a, uint64_t b) noexcept;

private:
    MetadataDb& db_;
};

} // namespace ali
