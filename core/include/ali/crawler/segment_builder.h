#pragma once
// ali/crawler/segment_builder.h — Bridges crawl pipeline into IndexWriter.
//
// Receives parsed documents from the crawler pipeline and feeds them into
// the IndexWriter. Calls flush() when the active segment is full or when
// explicitly requested by the crawler lifecycle.

#include "ali/index/index_writer.h"
#include "ali/crawler/deduplicator.h"

#include <cstdint>
#include <string>

namespace ali {

struct SegmentBuilderConfig {
    uint32_t flush_every_n_docs = 1000;  ///< auto-flush after N documents
};

class SegmentBuilder {
public:
    explicit SegmentBuilder(IndexWriter&          writer,
                             Deduplicator&         dedup,
                             SegmentBuilderConfig  cfg = {});

    /// Submit a parsed document. Deduplication is checked first.
    /// Returns true if the document was accepted and indexed.
    bool submit(const std::string& canonical_url,
                const std::string& title,
                const std::string& body_text,
                uint32_t           content_hash);

    /// Flush any pending documents to a published segment.
    bool flush();

    [[nodiscard]] uint64_t total_submitted() const noexcept;
    [[nodiscard]] uint64_t total_indexed()   const noexcept;
    [[nodiscard]] uint64_t total_skipped()   const noexcept;

private:
    IndexWriter&         writer_;
    Deduplicator&        dedup_;
    SegmentBuilderConfig cfg_;
    uint64_t             submitted_ = 0;
    uint64_t             indexed_   = 0;
    uint64_t             skipped_   = 0;
    uint32_t             since_flush_ = 0;
};

} // namespace ali
