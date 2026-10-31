#pragma once
// ali/ranking/result_set.h — Bounded top-K min-heap for query results.
//
// Maintains exactly `k` candidates. Adding a new candidate only displaces
// the lowest-scoring current candidate when the new score is higher.
// Memory usage is O(k) with no dynamic reallocation after construction.

#include "ali/storage/types.h"

#include <cstdint>
#include <vector>

namespace ali {

struct Candidate {
    DocId   doc_id = kInvalidDocId;
    float   score  = 0.0f;

    bool operator<(const Candidate& o) const noexcept { return score < o.score; }
};

class ResultSet {
public:
    explicit ResultSet(uint32_t k);

    /// Attempt to insert a candidate. Returns true if it entered the top-K.
    bool try_insert(DocId doc_id, float score) noexcept;

    /// Minimum score currently in the top-K (threshold for WAND pruning).
    [[nodiscard]] float threshold() const noexcept;

    [[nodiscard]] bool     full() const noexcept;
    [[nodiscard]] uint32_t size() const noexcept;
    [[nodiscard]] uint32_t capacity() const noexcept;

    /// Return candidates sorted descending by score.
    [[nodiscard]] std::vector<Candidate> top_k() const;

    void clear() noexcept;

private:
    uint32_t              k_;
    std::vector<Candidate> heap_;  ///< min-heap, size <= k
};

} // namespace ali
