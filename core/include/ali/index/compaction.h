#pragma once
// ali/index/compaction.h — Background segment compaction.
//
// Merges small segments and applies Removed-status deletions to keep
// segment count bounded. Only runs within the background resource budget.
// Never runs while interactive search is active.

#include "ali/storage/manifest.h"
#include "ali/storage/metadata_db.h"
#include "ali/crawler/resource_controller.h"

#include <atomic>
#include <cstdint>

namespace ali {

struct CompactionConfig {
    uint32_t max_segments_before_compact = 8;   ///< trigger when live segments exceed this
    uint32_t target_docs_per_segment     = 65536;
    uint64_t max_segment_bytes           = 256ULL * 1024 * 1024;
};

/// Result of one compaction run.
struct CompactionResult {
    uint32_t segments_merged  = 0;
    uint32_t segments_created = 0;
    uint32_t docs_removed     = 0;  ///< tombstoned during merge
    bool     completed        = false;
    std::string error_detail;
};

class Compaction {
public:
    explicit Compaction(Manifest&           manifest,
                        MetadataDb&         db,
                        ResourceController& resources,
                        CompactionConfig    cfg = {});

    /// Run one compaction pass if resource conditions permit.
    /// cancel_flag allows the caller to abort mid-merge.
    [[nodiscard]] CompactionResult run(std::atomic<bool>& cancel_flag);

    /// Returns true if compaction is needed based on current segment count.
    [[nodiscard]] bool needs_compaction() const;

private:
    Manifest&           manifest_;
    MetadataDb&         db_;
    ResourceController& resources_;
    CompactionConfig    cfg_;
};

} // namespace ali
