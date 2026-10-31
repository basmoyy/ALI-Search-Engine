#pragma once
// ali/query/query_planner.h — Produces a QueryPlan from a parsed query
// and index statistics.

#include "ali/query/query.h"
#include "ali/index/index_reader.h"

#include <memory>

namespace ali {

struct QueryPlannerConfig {
    /// Terms with doc_frequency / total_docs above this ratio are flagged broad.
    float broad_term_threshold = 0.25f;

    /// Only enable Block-Max WAND when all query terms have block metadata
    /// AND the query is a ranked (not exhaustive) top-K query.
    bool  prefer_block_max_wand = true;
};

class QueryPlanner {
public:
    explicit QueryPlanner(const IndexReader& reader,
                          QueryPlannerConfig cfg = {});
    ~QueryPlanner();

    /// Build a plan for the given query. Reads term statistics from the index.
    [[nodiscard]] QueryPlan plan(const Query& query) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
