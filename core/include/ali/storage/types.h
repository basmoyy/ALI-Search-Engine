#pragma once
// ali/storage/types.h — Fundamental storage-layer types shared across the engine.

#include <cstdint>
#include <string>
#include <vector>

namespace ali {

// ─── Identifiers ─────────────────────────────────────────────────────────────

/// Stable internal document identifier. Never derived from a URL.
using DocId = uint32_t;

/// Term identifier within a single segment's dictionary.
using TermId = uint32_t;

/// Opaque block identifier used by Locality Memory and the block cache.
using BlockId = uint64_t;

/// Index-segment generation counter.
using Generation = uint32_t;

// ─── Constants ───────────────────────────────────────────────────────────────

static constexpr DocId  kInvalidDocId  = UINT32_MAX;
static constexpr TermId kInvalidTermId = UINT32_MAX;
static constexpr BlockId kInvalidBlockId = UINT64_MAX;

// ─── Segment bounds ───────────────────────────────────────────────────────────

struct SegmentRange {
    DocId first_doc = 0;
    DocId last_doc  = 0;       ///< inclusive

    [[nodiscard]] uint32_t count() const noexcept {
        return (last_doc >= first_doc) ? (last_doc - first_doc + 1) : 0;
    }
};

// ─── Document metadata ───────────────────────────────────────────────────────

struct DocumentMeta {
    DocId       doc_id     = kInvalidDocId;
    uint32_t    term_count = 0;   ///< total terms in document (for BM25 length)
    uint64_t    content_offset = 0;
    uint32_t    content_length = 0;
};

// ─── Posting ─────────────────────────────────────────────────────────────────

struct Posting {
    DocId    doc_id   = kInvalidDocId;
    uint32_t term_freq = 0;
};

// ─── Block metadata (for Block-Max WAND) ─────────────────────────────────────

struct BlockMeta {
    BlockId  block_id       = kInvalidBlockId;
    DocId    first_doc      = kInvalidDocId;
    DocId    last_doc       = kInvalidDocId;
    float    max_score      = 0.0f;   ///< conservative upper bound for BM25 within block
    uint64_t file_offset    = 0;
    uint32_t compressed_len = 0;
    uint32_t doc_count      = 0;
};

// ─── Term entry in dictionary ─────────────────────────────────────────────────

struct TermEntry {
    std::string term;
    TermId      term_id          = kInvalidTermId;
    uint32_t    doc_frequency    = 0;
    uint64_t    postings_offset  = 0;   ///< byte offset in segment postings file
    uint32_t    postings_length  = 0;   ///< compressed byte length
    uint32_t    block_count      = 0;
    uint64_t    block_meta_offset = 0;  ///< byte offset into block-metadata section
};

} // namespace ali
