#pragma once
// ali/index/term_dictionary.h — In-memory sparse navigation index.
//
// Holds a sorted array of sampled (term, file_offset) pairs that act as
// a skip list into the on-disk terms.bin file. This avoids loading the
// full vocabulary into RAM.
//
// Phase 4: sparse dictionary. FST evaluation is deferred to Phase 4+
// after benchmarking shows whether the complexity is warranted.

#include "ali/storage/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ali {

struct SparseEntry {
    std::string term;
    uint64_t    file_offset = 0;  ///< byte offset into terms.bin
    uint32_t    entry_index = 0;  ///< sequential index in the sorted term list
};

/// Compact sparse navigation index over one segment's term dictionary.
/// Built once when a segment is opened; fits entirely in the navigation cache.
class TermDictionary {
public:
    TermDictionary() = default;

    /// Build from a sorted list of (term, offset, index) triplets.
    /// sample_rate controls how many entries are kept (1 = all, N = every Nth).
    void build(const std::vector<SparseEntry>& all_entries,
               uint32_t                        sample_rate = 8);

    /// Return the file offset of the last sampled entry whose term <= query_term.
    /// Returns nullopt if the dictionary is empty or query_term < first entry.
    [[nodiscard]] std::optional<uint64_t>
        lower_bound_offset(const std::string& query_term) const noexcept;

    [[nodiscard]] std::size_t entry_count()  const noexcept;
    [[nodiscard]] std::size_t size_bytes()   const noexcept;

private:
    std::vector<SparseEntry> entries_;  ///< sampled entries, sorted by term
};

} // namespace ali
