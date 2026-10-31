#pragma once
// ali/query/query.h — Query representation and planning structures.

#include <cstdint>
#include <string>
#include <vector>

namespace ali {

/// A single normalised token produced by the tokeniser.
struct Token {
    std::string text;
    uint32_t    position = 0;
};

/// A parsed query. v0.1 supports AND/OR term queries and top-K retrieval.
struct Query {
    enum class Operator : uint8_t { AND, OR };

    std::string       raw_text;
    std::vector<Token> tokens;
    Operator          op     = Operator::AND;
    uint32_t          top_k  = 10;

    /// Hard time limit for this query in milliseconds (0 = no limit).
    uint32_t timeout_ms = 200;
};

/// Per-term retrieval plan produced by the query planner.
struct TermPlan {
    std::string term;
    uint32_t    estimated_doc_freq = 0;
    float       idf_estimate       = 0.0f;
    bool        skip_if_broad      = false; ///< term appears in > 25% of docs
};

/// Full query execution plan.
struct QueryPlan {
    Query                  query;
    std::vector<TermPlan>  term_plans;
    bool                   use_block_max_wand = false;  ///< safe if all terms have block metadata
    bool                   is_exhaustive      = false;  ///< forced exhaustive scan
};

/// One result entry.
struct SearchResult {
    uint32_t    rank      = 0;
    float       score     = 0.0f;
    std::string url;
    std::string title;
    std::string excerpt;
};

/// Top-K result set returned to the caller.
struct SearchResponse {
    std::vector<SearchResult> results;
    uint32_t    total_candidates_evaluated = 0;
    uint32_t    blocks_read                = 0;
    uint64_t    bytes_read                 = 0;
    uint64_t    bytes_decoded              = 0;
    uint32_t    elapsed_ms                 = 0;
    bool        timed_out                  = false;
    bool        partial_results            = false;
};

} // namespace ali
