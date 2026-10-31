// index_reader.cpp — Multi-segment unified index reader.

#include "ali/index/index_reader.h"

#include <numeric>

namespace ali {

struct IndexReader::Impl {
    const Manifest&               manifest;
    uint64_t                      loaded_version   = UINT64_MAX;
    std::vector<SegmentReader>    segments;
    uint32_t                      total_doc_count_ = 0;
    float                         avg_dl_          = 0.0f;

    explicit Impl(const Manifest& m) : manifest(m) {}
};

IndexReader::IndexReader(const Manifest& manifest)
    : impl_(new Impl(manifest)) {}
IndexReader::~IndexReader() = default;

bool IndexReader::open() {
    const auto snap = impl_->manifest.snapshot();
    impl_->segments.clear();

    for (const auto& entry : snap.segments) {
        if (entry.is_tombstone) continue;
        SegmentReader sr(entry.path);
        if (!sr.is_valid()) continue;
        impl_->segments.push_back(std::move(sr));
    }

    // Recompute global stats
    uint64_t total_terms = 0;
    impl_->total_doc_count_ = 0;
    for (const auto& s : impl_->segments) {
        impl_->total_doc_count_ += s.doc_count();
        total_terms += static_cast<uint64_t>(
            static_cast<float>(s.doc_count()) * s.avg_doc_length());
    }
    impl_->avg_dl_ = impl_->total_doc_count_ > 0
        ? static_cast<float>(total_terms) / static_cast<float>(impl_->total_doc_count_)
        : 0.0f;

    impl_->loaded_version = snap.manifest_version;
    return true;
}

void IndexReader::maybe_refresh() {
    const auto snap = impl_->manifest.snapshot();
    if (snap.manifest_version != impl_->loaded_version) {
        (void)open();
    }
}

std::optional<MultiSegmentTermInfo>
IndexReader::find_term(const std::string& term) const {
    MultiSegmentTermInfo info;
    info.total_doc_frequency = 0;

    for (std::size_t i = 0; i < impl_->segments.size(); ++i) {
        auto entry = impl_->segments[i].find_term(term);
        if (!entry) continue;
        info.total_doc_frequency += entry->doc_frequency;
        info.by_segment.push_back({i, *entry});
    }

    if (info.by_segment.empty()) return std::nullopt;
    return info;
}

uint32_t IndexReader::total_doc_count() const noexcept { return impl_->total_doc_count_; }
float    IndexReader::avg_doc_length()  const noexcept { return impl_->avg_dl_; }
std::size_t IndexReader::segment_count() const noexcept { return impl_->segments.size(); }

const SegmentReader& IndexReader::segment(std::size_t idx) const {
    return impl_->segments.at(idx);
}

} // namespace ali
