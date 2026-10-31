#pragma once
// ali/locality/locality_memory.h — Compact usage-signal store for cache policy.
//
// Locality Memory records which posting-list blocks have been accessed and
// whether they contributed to a query result ("useful access"). It informs
// cache admission and retention decisions.
//
// IMPORTANT: Locality Memory must never affect relevance ranking or result
// ordering. It is a cache-policy hint only.

#include "ali/storage/types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace ali {

/// One locality record as stored in memory and persisted periodically.
struct LocalityEntry {
    BlockId  block_id          = kInvalidBlockId;
    uint32_t access_count      = 0;
    uint32_t useful_access_count = 0;  ///< block contributed a candidate or result
    int64_t  last_access_time  = 0;    ///< unix seconds
    float    priority          = 0.0f; ///< computed cache priority
};

struct LocalityConfig {
    uint32_t max_entries        = 16384;   ///< hard cap; evicts when exceeded
    float    alpha              = 0.5f;    ///< weight for access_count
    float    beta               = 1.5f;    ///< weight for useful_access_count
    float    lambda             = 0.001f;  ///< time-decay rate (per second)
    uint32_t persist_interval_s = 300;     ///< flush to storage every N seconds
};

/// Thread-safe locality statistics store.
class LocalityMemory {
public:
    explicit LocalityMemory(LocalityConfig cfg = {});
    ~LocalityMemory();

    LocalityMemory(const LocalityMemory&)            = delete;
    LocalityMemory& operator=(const LocalityMemory&) = delete;

    // ── Update signals ───────────────────────────────────────────────────────

    void record_access(BlockId block_id, bool was_useful);
    void record_batch_access(const BlockId* block_ids,
                              const bool*    useful,
                              std::size_t    count);

    // ── Cache admission ──────────────────────────────────────────────────────

    /// Estimated cache priority for a block. Returns 0.0 for unknown blocks.
    [[nodiscard]] float priority(BlockId block_id) const noexcept;

    /// Check whether a block should be admitted into the bounded block cache.
    [[nodiscard]] bool should_admit(BlockId block_id,
                                    float   retrieval_cost_estimate) const noexcept;

    // ── Persistence ──────────────────────────────────────────────────────────

    [[nodiscard]] bool load(const std::filesystem::path& path);
    [[nodiscard]] bool save(const std::filesystem::path& path) const;

    /// Decay and evict low-priority entries to enforce the entry cap.
    void compact();

    [[nodiscard]] uint32_t entry_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
