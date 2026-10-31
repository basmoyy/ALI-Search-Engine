// query_planner.cpp — Builds a QueryPlan from a parsed Query and index stats.

#include "ali/query/query_planner.h"
#include "ali/ranking/bm25.h"

#include <cmath>

namespace ali {

struct QueryPlanner::Impl {
    const IndexReader&  reader;
    QueryPlannerConfig  cfg;
};

QueryPlanner::QueryPlanner(const IndexReader& reader, QueryPlannerConfig cfg)
    : impl_(new Impl{reader, cfg}) {}
QueryPlanner::~QueryPlanner() = default;

QueryPlan QueryPlanner::plan(const Query& query) const {
    QueryPlan p;
    p.query = query;

    const uint32_t total_docs = impl_->reader.total_doc_count();
    const float avg_dl       = impl_->reader.avg_doc_length();
    Bm25Scorer scorer({}, total_docs, avg_dl);

    bool all_have_block_meta = !query.tokens.empty();

    for (const auto& token : query.tokens) {
        TermPlan tp;
        tp.term = token.text;

        auto info = impl_->reader.find_term(token.text);
        if (info) {
            tp.estimated_doc_freq = info->total_doc_frequency;
            tp.idf_estimate       = scorer.idf(tp.estimated_doc_freq);

            const float ratio = (total_docs > 0)
                ? static_cast<float>(tp.estimated_doc_freq) / static_cast<float>(total_docs)
                : 0.0f;
            tp.skip_if_broad = (ratio > impl_->cfg.broad_term_threshold);

            // Check whether ALL segments have block metadata for this term.
            for (const auto& seg_entry : info->by_segment) {
                if (seg_entry.entry.block_count == 0) {
                    all_have_block_meta = false;
                }
            }
        } else {
            // Term not found; mark as broad so it is deprioritised.
            tp.skip_if_broad    = false;
            tp.estimated_doc_freq = 0;
            all_have_block_meta = false;
        }
        p.term_plans.push_back(std::move(tp));
    }

    p.use_block_max_wand =
        impl_->cfg.prefer_block_max_wand
        && all_have_block_meta
        && !p.is_exhaustive;

    return p;
}

} // namespace ali
