#pragma once
// ali/storage/segment_writer.h — Immutable segment builder.
//
// Segments are written to a temporary path, flushed, validated and then
// published atomically via the manifest. A partially written segment is never
// visible to readers.

#include "ali/storage/types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ali {

/// Configuration knobs for a single segment build.
struct SegmentWriterConfig {
    uint32_t max_docs          = 65536;    ///< Soft cap; writer rejects docs beyond this.
    uint32_t block_size_docs   = 128;      ///< Target number of postings per block.
    uint64_t max_segment_bytes = 256ULL * 1024 * 1024;  ///< 256 MB hard limit.
    bool     include_full_text = false;    ///< Tier-3 content retention.
};

/// Incrementally builds one immutable segment in a temporary directory.
/// Call finish() to flush and atomically publish to the final path.
class SegmentWriter {
public:
    explicit SegmentWriter(std::filesystem::path tmp_dir,
                           SegmentWriterConfig  cfg = {});
    ~SegmentWriter();

    // Non-copyable, movable.
    SegmentWriter(const SegmentWriter&)            = delete;
    SegmentWriter& operator=(const SegmentWriter&) = delete;
    SegmentWriter(SegmentWriter&&)                 noexcept;
    SegmentWriter& operator=(SegmentWriter&&)      noexcept;

    /// Add a document. Returns the assigned DocId on success.
    /// Returns kInvalidDocId if the segment is full or the document is invalid.
    [[nodiscard]] DocId add_document(std::string_view url,
                                     std::string_view title,
                                     std::string_view text,
                                     const std::vector<std::pair<std::string, uint32_t>>& term_freqs);

    /// Finalise the segment: flush all structures, write checksums and
    /// publish to `dest_dir`. On success the writer is consumed and the
    /// temporary files are removed. On failure dest_dir is untouched.
    [[nodiscard]] bool finish(const std::filesystem::path& dest_dir,
                              Generation generation);

    [[nodiscard]] uint32_t doc_count()        const noexcept;
    [[nodiscard]] uint64_t estimated_bytes()  const noexcept;
    [[nodiscard]] bool     is_full()          const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
