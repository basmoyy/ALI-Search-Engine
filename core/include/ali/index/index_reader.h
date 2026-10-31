#pragma once
// ali/index/index_reader.h — Multi-segment read-only index view.
//
// Opens all live segments from the manifest and presents a unified
// interface to the query planner and ALI Scanner. Refreshes on demand
// when the manifest generation advances.

#include "ali/storage/types.h"
#include "ali/storage/manifest.h"
#include "ali/storage/segment_reader.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ali {

/// Per-term information aggregated across segments.
struct MultiSegmentTermInfo {
    struct SegmentTermEntry {
        std::size_t segment_index = 0;
        TermEntry   entry;
    };
    uint32_t total_doc_frequency = 0;
    std::vector<SegmentTermEntry> by_segment;
};

/// Read-only view across all live segments.
class IndexReader {
public:
    explicit IndexReader(const Manifest& manifest);
    ~IndexReader();

    IndexReader(const IndexReader&)            = delete;
    IndexReader& operator=(const IndexReader&) = delete;

    /// Open / refresh from current manifest snapshot.
    [[nodiscard]] bool open();

    /// Re-open if the manifest generation has advanced. Cheap no-op otherwise.
    void maybe_refresh();

    // ── Term lookup ──────────────────────────────────────────────────────────

    [[nodiscard]] std::optional<MultiSegmentTermInfo>
        find_term(const std::string& term) const;

    // ── Global statistics ────────────────────────────────────────────────────

    [[nodiscard]] uint32_t total_doc_count()   const noexcept;
    [[nodiscard]] float    avg_doc_length()    const noexcept;

    // ── Segment access ───────────────────────────────────────────────────────

    [[nodiscard]] std::size_t segment_count() const noexcept;
    [[nodiscard]] const SegmentReader& segment(std::size_t idx) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
