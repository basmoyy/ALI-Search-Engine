// segment_builder.cpp — Bridges the crawl pipeline into IndexWriter.
//
// Receives parsed documents from the crawler, runs deduplication, and
// feeds accepted documents into the IndexWriter. Auto-flushes the active
// segment after every flush_every_n_docs documents to keep segment size
// bounded without requiring explicit flush calls from the crawler.
//
// Backpressure: if IndexWriter::add_document() rejects a document (segment
// full), flush() is called automatically and the document is retried once.
// If the retry also fails the document is counted as skipped and crawling
// continues — the crawler must not stall on indexing failures.

#include "ali/crawler/segment_builder.h"

namespace ali {

SegmentBuilder::SegmentBuilder(IndexWriter&         writer,
                                Deduplicator&        dedup,
                                SegmentBuilderConfig cfg)
    : writer_(writer), dedup_(dedup), cfg_(std::move(cfg)) {}

bool SegmentBuilder::submit(const std::string& canonical_url,
                             const std::string& title,
                             const std::string& body_text,
                             uint32_t           content_hash) {
    ++submitted_;

    // Deduplication check — skip exact duplicates.
    if (dedup_.is_duplicate(canonical_url, content_hash)) {
        ++skipped_;
        return false;
    }

    InputDocument doc;
    doc.url          = canonical_url;
    doc.title        = title;
    doc.body_text    = body_text;
    doc.content_hash = content_hash;

    auto result = writer_.add_document(doc);

    // If the segment was full, flush and retry once.
    if (!result.accepted && !result.duplicate) {
        flush();
        result = writer_.add_document(doc);
    }

    if (!result.accepted) {
        ++skipped_;
        return false;
    }

    ++indexed_;
    ++since_flush_;

    // Auto-flush when the batch threshold is reached.
    if (since_flush_ >= cfg_.flush_every_n_docs) {
        flush();
    }

    return true;
}

bool SegmentBuilder::flush() {
    since_flush_ = 0;
    return writer_.flush();
}

uint64_t SegmentBuilder::total_submitted() const noexcept { return submitted_; }
uint64_t SegmentBuilder::total_indexed()   const noexcept { return indexed_;   }
uint64_t SegmentBuilder::total_skipped()   const noexcept { return skipped_;   }

} // namespace ali
