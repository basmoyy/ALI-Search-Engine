#pragma once
// ali/index/index_writer.h — High-level index build API.
//
// IndexWriter coordinates SegmentWriter instances. When a segment is full
// or flush() is called explicitly, the writer finalises the segment and
// registers it in the Manifest. The caller never needs to manage
// SegmentWriter directly during normal indexing.

#include "ali/storage/types.h"
#include "ali/storage/manifest.h"
#include "ali/storage/metadata_db.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ali {

struct IndexWriterConfig {
    uint32_t    docs_per_segment     = 65536;
    uint64_t    max_segment_bytes    = 256ULL * 1024 * 1024;
    bool        include_full_text    = false;
    std::string index_root;    ///< parent directory for all segment directories
};

/// Document input for indexing.
struct InputDocument {
    std::string url;
    std::string title;
    std::string body_text;  ///< extracted, boilerplate-removed plain text
    uint32_t    content_hash = 0;
};

/// Result of indexing one document.
struct IndexResult {
    bool    accepted  = false;
    DocId   doc_id    = kInvalidDocId;
    bool    duplicate = false;   ///< skipped because content_hash matched existing
    std::string error_message;
};

/// Writes documents into a segmented index.
class IndexWriter {
public:
    explicit IndexWriter(IndexWriterConfig cfg,
                         Manifest&         manifest,
                         MetadataDb&       db);
    ~IndexWriter();

    IndexWriter(const IndexWriter&)            = delete;
    IndexWriter& operator=(const IndexWriter&) = delete;

    [[nodiscard]] bool open();

    /// Index a document. Thread-unsafe; call from one thread or with external locking.
    [[nodiscard]] IndexResult add_document(const InputDocument& doc);

    /// Flush the active segment (if any documents are pending) and publish it.
    [[nodiscard]] bool flush();

    void close();

    [[nodiscard]] uint64_t total_docs_indexed() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
