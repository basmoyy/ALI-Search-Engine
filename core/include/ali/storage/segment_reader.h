#pragma once
// ali/storage/segment_reader.h — Read-only view of a published segment.
//
// Callers access a segment through this interface. The reader validates
// the segment header and checksums on open and exposes selective block reads
// to the ALI Scanner.

#include "ali/storage/types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ali {

/// Compressed block of postings as stored on disk.
struct CompressedBlock {
    BlockMeta              meta;
    std::vector<uint8_t>   data;   ///< compressed bytes
};

/// Decoded postings from one block, lifetime managed by the scanner.
struct DecodedBlock {
    BlockMeta            meta;
    std::vector<Posting> postings;
};

/// Read-only handle to a single immutable segment.
class SegmentReader {
public:
    explicit SegmentReader(std::filesystem::path segment_dir);
    ~SegmentReader();

    SegmentReader(const SegmentReader&)            = delete;
    SegmentReader& operator=(const SegmentReader&) = delete;
    SegmentReader(SegmentReader&&)                 noexcept;
    SegmentReader& operator=(SegmentReader&&)      noexcept;

    /// Returns false if the segment is missing, corrupt or version-incompatible.
    [[nodiscard]] bool is_valid() const noexcept;

    // ── Term lookup ──────────────────────────────────────────────────────────

    /// Look up a term. Returns nullopt if the term is not in this segment.
    [[nodiscard]] std::optional<TermEntry> find_term(const std::string& term) const;

    // ── Block reads ─────────────────────────────────────────────────────────

    /// Read block metadata for a term (used by Block-Max WAND planner).
    [[nodiscard]] std::vector<BlockMeta> read_block_metadata(const TermEntry& entry) const;

    /// Read and return one compressed block from disk.
    /// Returns nullopt on I/O or checksum error.
    [[nodiscard]] std::optional<CompressedBlock>
        read_block(const BlockMeta& block) const;

    // ── Document metadata ────────────────────────────────────────────────────

    [[nodiscard]] std::optional<DocumentMeta> get_doc_meta(DocId doc_id) const;

    /// Fetch stored title and excerpt for result assembly.
    [[nodiscard]] bool get_doc_display(DocId            doc_id,
                                       std::string&     out_url,
                                       std::string&     out_title,
                                       std::string&     out_excerpt) const;

    // ── Segment statistics ───────────────────────────────────────────────────

    [[nodiscard]] uint32_t   doc_count()          const noexcept;
    [[nodiscard]] float      avg_doc_length()     const noexcept;
    [[nodiscard]] Generation generation()         const noexcept;
    [[nodiscard]] SegmentRange doc_range()        const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
