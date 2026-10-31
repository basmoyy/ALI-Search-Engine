#pragma once
// ali/ranking/bm25.h — BM25 lexical ranking.
//
// Parameters k1 and b use standard defaults; they must be held constant for
// a given index generation to keep results deterministic.

#include "ali/storage/types.h"

#include <cmath>
#include <cstdint>

namespace ali {

struct Bm25Params {
    float k1 = 1.2f;
    float b  = 0.75f;
};

/// Stateless BM25 scorer. Construct once per query with global statistics.
class Bm25Scorer {
public:
    Bm25Scorer(Bm25Params params,
               uint32_t   total_doc_count,
               float      avg_doc_length) noexcept;

    /// Score contribution of one term occurrence in a document.
    /// doc_freq   = number of documents containing this term (corpus-wide)
    /// term_freq  = occurrences of the term in this document
    /// doc_length = total terms in this document
    [[nodiscard]] float score(uint32_t doc_freq,
                               uint32_t term_freq,
                               uint32_t doc_length) const noexcept;

    /// IDF component only (used for Block-Max WAND upper-bound estimate).
    [[nodiscard]] float idf(uint32_t doc_freq) const noexcept;

    /// Maximum possible TF-normalised contribution for a block.
    /// Used to construct conservative upper bounds for WAND pruning.
    [[nodiscard]] float max_tf_contribution(uint32_t doc_freq,
                                             uint32_t max_block_doc_freq,
                                             uint32_t min_block_doc_length) const noexcept;

private:
    Bm25Params params_;
    uint32_t   N_;     ///< total documents in corpus
    float      avg_dl_;
};

} // namespace ali
