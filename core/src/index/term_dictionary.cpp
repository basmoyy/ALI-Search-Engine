// term_dictionary.cpp — Sparse navigation index over a segment's term dictionary.
//
// Builds a compact skip-list of sampled (term, file_offset) pairs from the
// full sorted term list loaded by SegmentReader. A lookup does a binary search
// to find the last sampled entry whose term is <= the query term, returning its
// file offset so the caller can seek to that position in terms.bin and scan
// forward — at most sample_rate entries — to find the exact term entry.
//
// Memory trade-off:
//   sample_rate = 1  → full dictionary in RAM (maximum speed, most memory)
//   sample_rate = 8  → 12.5% of entries in RAM (default)
//   sample_rate = 32 → 3%   of entries in RAM  (minimum memory)
//
// Benchmark FST vs this approach per Phase 4 guidance before changing.

#include "ali/index/term_dictionary.h"

#include <algorithm>

namespace ali {

void TermDictionary::build(const std::vector<SparseEntry>& all_entries,
                            uint32_t                        sample_rate) {
    entries_.clear();
    if (all_entries.empty() || sample_rate == 0) return;

    entries_.reserve(all_entries.size() / sample_rate + 1);
    for (std::size_t i = 0; i < all_entries.size(); i += sample_rate) {
        entries_.push_back(all_entries[i]);
    }
}

std::optional<uint64_t>
TermDictionary::lower_bound_offset(const std::string& query_term) const noexcept {
    if (entries_.empty()) return std::nullopt;

    // Binary search for the last entry whose term <= query_term.
    auto it = std::upper_bound(
        entries_.begin(), entries_.end(), query_term,
        [](const std::string& term, const SparseEntry& e) {
            return term < e.term;
        });

    if (it == entries_.begin()) return std::nullopt;
    --it;
    return it->file_offset;
}

std::size_t TermDictionary::entry_count() const noexcept {
    return entries_.size();
}

std::size_t TermDictionary::size_bytes() const noexcept {
    std::size_t total = sizeof(entries_);
    for (const auto& e : entries_) {
        total += sizeof(SparseEntry) + e.term.capacity();
    }
    return total;
}

} // namespace ali
