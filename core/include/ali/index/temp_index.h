#pragma once
// ali/index/temp_index.h — Temporary in-memory index (the ALI working set).
//
// The TempIndex is the live, queryable working set:
//   Discover → Fetch → Parse → TempIndex (immediately queryable)
//   ↓ (background, when resources allow)
//   Persistent SegmentWriter (flush to immutable segment)

#include "ali/storage/types.h"
#include "ali/locality/locality_memory.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ali {

struct TempIndexConfig {
    uint32_t max_bytes           = 8 * 1024 * 1024;
    uint32_t max_docs            = 8192;
    uint32_t flush_threshold_pct = 75;
};

struct TempDoc {
    DocId       doc_id     = kInvalidDocId;
    std::string url;
    std::string title;
    std::string excerpt;
    uint32_t    term_count = 0;
    int64_t     added_at   = 0;
};

struct TempPosting {
    DocId    doc_id    = kInvalidDocId;
    uint32_t term_freq = 0;
};

class TempIndex {
public:
    explicit TempIndex(TempIndexConfig cfg = {}, LocalityMemory* locality = nullptr);
    ~TempIndex();

    TempIndex(const TempIndex&)            = delete;
    TempIndex& operator=(const TempIndex&) = delete;

    bool add_document(DocId global_id,
                      const std::string& url,
                      const std::string& title,
                      const std::string& excerpt,
                      const std::vector<std::pair<std::string, uint32_t>>& term_freqs);

    [[nodiscard]] const std::vector<TempPosting>*
        find_term_postings(const std::string& term) const;
    [[nodiscard]] uint32_t term_doc_frequency(const std::string& term) const;
    [[nodiscard]] bool get_doc_display(DocId doc_id, std::string& out_url,
                                        std::string& out_title, std::string& out_excerpt) const;
    [[nodiscard]] std::optional<DocumentMeta> get_doc_meta(DocId doc_id) const;

    void     clear();
    uint32_t evict_stale(int64_t now_unix, int64_t max_age_seconds = 3600);
    [[nodiscard]] bool should_flush() const noexcept;

    [[nodiscard]] uint32_t doc_count()     const noexcept;
    [[nodiscard]] uint64_t used_bytes()    const noexcept;
    [[nodiscard]] uint32_t max_bytes()     const noexcept;
    [[nodiscard]] bool     is_full()       const noexcept;
    [[nodiscard]] uint32_t total_doc_frequency() const noexcept;
    [[nodiscard]] float    avg_doc_length() const noexcept;

    [[nodiscard]] const std::vector<TempDoc>& docs() const noexcept;

    using TermPostingMap = std::unordered_map<std::string, std::vector<TempPosting>>;
    [[nodiscard]] const TermPostingMap& term_map() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
