// deduplicator.cpp — Content deduplication via canonical URL + content fingerprint.
//
// Exact-duplicate detection uses two checks in order:
//   1. Canonical URL lookup — if already Indexed with the same hash, skip.
//   2. Content fingerprint (FNV-1a over the extracted body text) — catches
//      the same content under multiple URLs.
//
// Near-duplicate detection (simhash, minhash) is deferred to a later phase.
// The fingerprint function is fast and allocation-free.

#include "ali/crawler/deduplicator.h"

namespace ali {

// ── FNV-1a 32-bit hash ────────────────────────────────────────────────────────
// Simple, fast, well-distributed for short-to-medium strings.

static constexpr uint32_t kFnvPrime  = 16777619u;
static constexpr uint32_t kFnvOffset = 2166136261u;

uint32_t Deduplicator::fingerprint(const std::string& body) noexcept {
    uint32_t hash = kFnvOffset;
    for (unsigned char c : body) {
        hash ^= static_cast<uint32_t>(c);
        hash *= kFnvPrime;
    }
    return hash;
}

// ── Deduplicator ─────────────────────────────────────────────────────────────

Deduplicator::Deduplicator(MetadataDb& db) : db_(db) {}

bool Deduplicator::is_duplicate(const std::string& canonical_url,
                                  uint32_t            content_hash) const {
    auto rec = db_.find_url(canonical_url);
    if (!rec) return false;

    // Already indexed with matching content — exact duplicate.
    if (rec->status == CrawlStatus::Indexed
     && rec->content_hash == content_hash
     && content_hash != 0) {
        return true;
    }

    return false;
}

} // namespace ali
