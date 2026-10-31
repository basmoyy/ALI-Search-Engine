#pragma once
// ali/crawler/deduplicator.h — Content deduplication.
//
// Detects exact duplicates via canonical URL and a fast content fingerprint
// (FNV-1a over the extracted body text). Near-duplicate detection is deferred.

#include "ali/storage/metadata_db.h"

#include <cstdint>
#include <string>

namespace ali {

class Deduplicator {
public:
    explicit Deduplicator(MetadataDb& db);

    /// Compute a content fingerprint for a body string.
    [[nodiscard]] static uint32_t fingerprint(const std::string& body) noexcept;

    /// Returns true if this content has been seen before (exact duplicate).
    /// Uses canonical URL check first, then fingerprint.
    [[nodiscard]] bool is_duplicate(const std::string& canonical_url,
                                     uint32_t            content_hash) const;

private:
    MetadataDb& db_;
};

} // namespace ali
