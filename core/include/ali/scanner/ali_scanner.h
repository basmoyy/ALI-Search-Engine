#pragma once
// ali/scanner/ali_scanner.h — Bounded selective retrieval engine.
//
// The scanner is the heart of ALI. Given a QueryPlan it reads ONLY the
// posting-list blocks needed to produce top-K results. It never loads an
// entire segment into memory. All buffer sizes are bounded.
//
// Design invariants:
//   - No unbounded allocation during a query.
//   - cache miss → disk read (bounded); never a full-segment load.
//   - Block-Max WAND upper bounds are used for skipping only when correct.
//   - Locality Memory is updated after each query; never consulted for ranking.
//   - All temporary buffers are returned to pools after use.

#include "ali/query/query.h"
#include "ali/index/index_reader.h"
#include "ali/locality/locality_memory.h"

#include <atomic>
#include <memory>

namespace ali {

struct ScannerConfig {
    uint32_t max_working_set_bytes   = 12 * 1024 * 1024; ///< 12 MB initial budget
    uint32_t navigation_cache_bytes  =  2 * 1024 * 1024; ///<  2 MB
    uint32_t retrieval_cache_bytes   =  4 * 1024 * 1024; ///<  4 MB
    uint32_t candidate_workspace_bytes = 4 * 1024 * 1024; ///< 4 MB
    uint32_t decode_buffer_bytes     =  2 * 1024 * 1024; ///<  2 MB
    uint32_t max_candidates          = 1024;
    uint32_t max_blocks_per_query    = 256;
};

/// Instrumentation counters collected during one scan.
struct ScanMetrics {
    uint32_t blocks_read       = 0;
    uint32_t blocks_skipped    = 0;  ///< skipped by Block-Max WAND
    uint64_t bytes_read        = 0;
    uint64_t bytes_decoded     = 0;
    uint32_t candidates_scored = 0;
    uint32_t peak_buffer_bytes = 0;
    uint32_t elapsed_ms        = 0;
};

/// The ALI Scanner.
class AliScanner {
public:
    explicit AliScanner(IndexReader&     reader,
                        LocalityMemory&  locality,
                        ScannerConfig    cfg = {});
    ~AliScanner();

    AliScanner(const AliScanner&)            = delete;
    AliScanner& operator=(const AliScanner&) = delete;

    /// Execute a query. Thread-unsafe; one interactive query at a time on
    /// low-end devices. Returns results plus instrumentation metrics.
    [[nodiscard]] SearchResponse execute(const QueryPlan&     plan,
                                         std::atomic<bool>&   cancel_flag);

    /// Reset all pools and caches (e.g. on memory-pressure signal).
    void purge_caches();

    [[nodiscard]] const ScannerConfig& config() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
