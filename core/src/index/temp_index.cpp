// temp_index.cpp — Temporary in-memory index implementation.

#include "ali/index/temp_index.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace ali {

struct TempIndex::Impl {
    TempIndexConfig cfg;
    LocalityMemory* locality;  // may be nullptr

    std::vector<TempDoc>  docs;
    std::unordered_map<DocId, std::size_t> doc_index;  // doc_id → index in docs

    TermPostingMap term_posting_map;
    uint64_t used_bytes_ = 0;
    uint64_t total_terms_ = 0;

    explicit Impl(TempIndexConfig c, LocalityMemory* loc)
        : cfg(c), locality(loc) {}
};

TempIndex::TempIndex(TempIndexConfig cfg, LocalityMemory* locality)
    : impl_(new Impl(cfg, locality)) {}

TempIndex::~TempIndex() = default;

bool TempIndex::add_document(
    DocId global_id,
    const std::string& url,
    const std::string& title,
    const std::string& excerpt,
    const std::vector<std::pair<std::string, uint32_t>>& term_freqs)
{
    if (is_full()) return false;
    if (url.empty()) return false;

    // Estimate bytes for this document
    uint64_t doc_bytes = url.size() + title.size() + excerpt.size() + sizeof(TempDoc);
    for (const auto& [term, freq] : term_freqs) {
        doc_bytes += term.size() + sizeof(TempPosting) + 8;
    }

    TempDoc doc;
    doc.doc_id     = global_id;
    doc.url        = url;
    doc.title      = title;
    doc.excerpt    = excerpt;
    doc.term_count = 0;
    doc.added_at   = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& [term, freq] : term_freqs) {
        if (term.empty()) continue;
        doc.term_count += freq;
        impl_->term_posting_map[term].push_back({global_id, freq});
    }

    impl_->total_terms_ += doc.term_count;
    impl_->doc_index[global_id] = impl_->docs.size();
    impl_->docs.push_back(std::move(doc));
    impl_->used_bytes_ += doc_bytes;
    return true;
}

const std::vector<TempPosting>*
TempIndex::find_term_postings(const std::string& term) const {
    auto it = impl_->term_posting_map.find(term);
    if (it == impl_->term_posting_map.end()) return nullptr;
    return &it->second;
}

uint32_t TempIndex::term_doc_frequency(const std::string& term) const {
    auto it = impl_->term_posting_map.find(term);
    if (it == impl_->term_posting_map.end()) return 0;
    return static_cast<uint32_t>(it->second.size());
}

bool TempIndex::get_doc_display(DocId doc_id,
                                  std::string& out_url,
                                  std::string& out_title,
                                  std::string& out_excerpt) const {
    auto it = impl_->doc_index.find(doc_id);
    if (it == impl_->doc_index.end()) return false;
    const auto& doc = impl_->docs[it->second];
    out_url     = doc.url;
    out_title   = doc.title;
    out_excerpt = doc.excerpt;
    return true;
}

std::optional<DocumentMeta> TempIndex::get_doc_meta(DocId doc_id) const {
    auto it = impl_->doc_index.find(doc_id);
    if (it == impl_->doc_index.end()) return std::nullopt;
    const auto& doc = impl_->docs[it->second];
    DocumentMeta m;
    m.doc_id     = doc_id;
    m.term_count = doc.term_count;
    return m;
}

void TempIndex::clear() {
    impl_->docs.clear();
    impl_->doc_index.clear();
    impl_->term_posting_map.clear();
    impl_->used_bytes_  = 0;
    impl_->total_terms_ = 0;
}

uint32_t TempIndex::evict_stale(int64_t now_unix, int64_t max_age_seconds) {
    const int64_t cutoff = now_unix - max_age_seconds;
    uint32_t evicted = 0;

    std::vector<DocId> to_remove;
    for (const auto& doc : impl_->docs) {
        if (doc.added_at < cutoff) {
            to_remove.push_back(doc.doc_id);
        }
    }

    for (DocId id : to_remove) {
        auto it = impl_->doc_index.find(id);
        if (it == impl_->doc_index.end()) continue;
        const auto& doc = impl_->docs[it->second];
        impl_->total_terms_ -= doc.term_count;
        impl_->doc_index.erase(it);
        ++evicted;
    }

    // Remove stale docs from docs vector
    if (evicted > 0) {
        std::vector<TempDoc> remaining;
        remaining.reserve(impl_->docs.size() - evicted);
        for (auto& doc : impl_->docs) {
            if (impl_->doc_index.count(doc.doc_id)) {
                remaining.push_back(std::move(doc));
            }
        }
        // Rebuild index
        impl_->doc_index.clear();
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            impl_->doc_index[remaining[i].doc_id] = i;
        }
        impl_->docs = std::move(remaining);

        // Clean term posting maps
        for (auto& [term, postings] : impl_->term_posting_map) {
            postings.erase(
                std::remove_if(postings.begin(), postings.end(),
                    [&](const TempPosting& p) {
                        return !impl_->doc_index.count(p.doc_id);
                    }),
                postings.end());
        }
        // Remove empty term entries
        for (auto it = impl_->term_posting_map.begin();
             it != impl_->term_posting_map.end(); ) {
            if (it->second.empty()) {
                it = impl_->term_posting_map.erase(it);
            } else {
                ++it;
            }
        }

        // Recompute used_bytes (approximate)
        impl_->used_bytes_ = 0;
        for (const auto& doc : impl_->docs) {
            impl_->used_bytes_ += doc.url.size() + doc.title.size()
                                + doc.excerpt.size() + sizeof(TempDoc)
                                + doc.term_count * 16;
        }
    }

    return evicted;
}

bool TempIndex::should_flush() const noexcept {
    const uint64_t threshold = (static_cast<uint64_t>(impl_->cfg.max_bytes)
                                * impl_->cfg.flush_threshold_pct) / 100;
    return impl_->used_bytes_ >= threshold
        || impl_->docs.size() >= impl_->cfg.max_docs;
}

uint32_t TempIndex::doc_count()  const noexcept { return static_cast<uint32_t>(impl_->docs.size()); }
uint64_t TempIndex::used_bytes() const noexcept { return impl_->used_bytes_; }
uint32_t TempIndex::max_bytes()  const noexcept { return impl_->cfg.max_bytes; }
bool     TempIndex::is_full()    const noexcept {
    return impl_->docs.size() >= impl_->cfg.max_docs
        || impl_->used_bytes_ >= impl_->cfg.max_bytes;
}

uint32_t TempIndex::total_doc_frequency() const noexcept {
    uint32_t total = 0;
    for (const auto& [term, postings] : impl_->term_posting_map) {
        total += static_cast<uint32_t>(postings.size());
    }
    return total;
}

float TempIndex::avg_doc_length() const noexcept {
    if (impl_->docs.empty()) return 0.0f;
    return static_cast<float>(impl_->total_terms_) / static_cast<float>(impl_->docs.size());
}

const std::vector<TempDoc>& TempIndex::docs() const noexcept {
    return impl_->docs;
}

const TempIndex::TermPostingMap& TempIndex::term_map() const noexcept {
    return impl_->term_posting_map;
}

} // namespace ali
