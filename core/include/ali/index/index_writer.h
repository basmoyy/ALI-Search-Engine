#pragma once
// ali/index/index_writer.h — High-level index build API.

#include "ali/storage/types.h"
#include "ali/storage/manifest.h"
#include "ali/storage/metadata_db.h"
#include "ali/index/temp_index.h"

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
    std::string index_root;
};

struct InputDocument {
    std::string url;
    std::string title;
    std::string body_text;
    uint32_t    content_hash = 0;
};

struct IndexResult {
    bool    accepted  = false;
    DocId   doc_id    = kInvalidDocId;
    bool    duplicate = false;
    std::string error_message;
};

class IndexWriter {
public:
    explicit IndexWriter(IndexWriterConfig cfg, Manifest& manifest, MetadataDb& db);
    ~IndexWriter();

    IndexWriter(const IndexWriter&)            = delete;
    IndexWriter& operator=(const IndexWriter&) = delete;

    [[nodiscard]] bool open();
    [[nodiscard]] IndexResult add_document(const InputDocument& doc);
    [[nodiscard]] bool flush();
    void close();
    [[nodiscard]] uint64_t total_docs_indexed() const noexcept;
    [[nodiscard]] TempIndex& temp_index() noexcept;
    [[nodiscard]] const TempIndex& temp_index() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
