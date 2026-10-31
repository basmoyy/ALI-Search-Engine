// bm25.cpp — BM25 lexical ranking implementation.

#include "ali/ranking/bm25.h"

#include <cmath>
#include <algorithm>

namespace ali {

Bm25Scorer::Bm25Scorer(Bm25Params params,
                        uint32_t   total_doc_count,
                        float      avg_doc_length) noexcept
    : params_(params)
    , N_(total_doc_count)
    , avg_dl_(avg_doc_length > 0.0f ? avg_doc_length : 1.0f)
{}

float Bm25Scorer::idf(uint32_t doc_freq) const noexcept {
    if (doc_freq == 0 || N_ == 0) return 0.0f;
    // Robertson–Spärck Jones smoothed IDF.
    const float df  = static_cast<float>(doc_freq);
    const float n   = static_cast<float>(N_);
    return std::log((n - df + 0.5f) / (df + 0.5f) + 1.0f);
}

float Bm25Scorer::score(uint32_t doc_freq,
                         uint32_t term_freq,
                         uint32_t doc_length) const noexcept {
    if (doc_freq == 0 || term_freq == 0) return 0.0f;

    const float tf  = static_cast<float>(term_freq);
    const float dl  = static_cast<float>(doc_length);
    const float k1  = params_.k1;
    const float b   = params_.b;

    const float tf_norm = tf * (k1 + 1.0f) /
        (tf + k1 * (1.0f - b + b * dl / avg_dl_));

    return idf(doc_freq) * tf_norm;
}

float Bm25Scorer::max_tf_contribution(uint32_t doc_freq,
                                       uint32_t /*max_block_doc_freq*/,
                                       uint32_t min_block_doc_length) const noexcept {
    // Conservative upper bound: assume TF = doc_length (every term is this one)
    // and doc_length = min_block_doc_length (shortest document in block).
    const float tf  = static_cast<float>(std::max(1u, min_block_doc_length));
    const float dl  = static_cast<float>(std::max(1u, min_block_doc_length));
    const float k1  = params_.k1;
    const float b   = params_.b;

    const float tf_norm = tf * (k1 + 1.0f) /
        (tf + k1 * (1.0f - b + b * dl / avg_dl_));

    return idf(doc_freq) * tf_norm;
}

} // namespace ali
