// ali_scanner.cpp — Bounded selective retrieval engine.
//
// Implements the ALI Scanner stages: Locate → Plan → Read → Decode → Score
// → Assemble → Release, with Block-Max WAND support and BlockCache.

#include "ali/scanner/ali_scanner.h"
#include "ali/scanner/block_cache.h"
#include "ali/ranking/bm25.h"
#include "ali/ranking/result_set.h"
#include "ali/storage/types.h"
#include "ali/compression/codec.h"
#include "ali/storage/segment_reader.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ali {

static uint32_t elapsed_ms_since(
    std::chrono::steady_clock::time_point start) noexcept {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

static std::vector<Posting> decode_block_data(
    const CompressedBlock& cb,
    DocId segment_first_doc)
{
    auto& codec = compression::codec_for_id(compression::default_codec());
    const auto& data = cb.data;
    if (data.size() < 8) return {};

    uint32_t doc_count = 0;
    std::memcpy(&doc_count, data.data(), 4);

    std::vector<uint32_t> doc_deltas(doc_count), freqs(doc_count);
    std::span<const uint8_t> rest(data.data() + 8, data.size() - 8);

    const std::size_t dc = codec.decode(rest, doc_count, doc_deltas);
    if (dc == 0) return {};

    std::span<const uint8_t> freq_span(rest.data() + dc, rest.size() - dc);
    if (codec.decode(freq_span, doc_count, freqs) == 0) return {};

    auto local_ids = compression::decode_deltas(doc_deltas);
    const uint32_t seg_base = doc_id_base(segment_first_doc);

    std::vector<Posting> out;
    out.reserve(doc_count);
    for (uint32_t i = 0; i < doc_count; ++i) {
        const DocId global_id = global_doc_id(seg_base, local_ids[i]);
        out.push_back({global_id, freqs[i]});
    }
    return out;
}

struct AliScanner::Impl {
    IndexReader&    reader;
    LocalityMemory& locality;
    ScannerConfig   cfg;
};

AliScanner::AliScanner(IndexReader& reader, LocalityMemory& locality, ScannerConfig cfg)
    : impl_(new Impl{reader, locality, cfg}) {}
AliScanner::~AliScanner() = default;

SearchResponse AliScanner::execute(const QueryPlan& plan,
                                    std::atomic<bool>& cancel_flag) {
    SearchResponse resp;
    const auto start_time = std::chrono::steady_clock::now();

    const uint32_t top_k    = plan.query.top_k;
    const uint32_t timeout  = plan.query.timeout_ms;

    ResultSet results(top_k);
    const uint32_t total_docs = impl_->reader.total_doc_count();
    const float    avg_dl     = impl_->reader.avg_doc_length();
    Bm25Scorer scorer({}, total_docs, avg_dl);

    std::unordered_map<DocId, float>    doc_scores;
    std::unordered_map<DocId, uint32_t> doc_term_hits;  // for AND semantics
    doc_scores.reserve(std::min(4096u, impl_->cfg.max_candidates));

    uint32_t blocks_read   = 0;
    uint64_t bytes_read    = 0;
    uint64_t bytes_decoded = 0;

    // Block cache: scoped to this query execution
    BlockCache block_cache(impl_->cfg.retrieval_cache_bytes, impl_->locality);

    const uint32_t required_terms = (plan.query.op == Query::Operator::AND)
        ? static_cast<uint32_t>(plan.term_plans.size())
        : 0;

    for (const auto& tp : plan.term_plans) {
        if (cancel_flag.load()) {
            resp.timed_out = false;
            break;
        }
        if (timeout > 0 && elapsed_ms_since(start_time) >= timeout) {
            resp.timed_out       = true;
            resp.partial_results = true;
            break;
        }
        if (tp.skip_if_broad && results.full()) continue;

        auto term_info = impl_->reader.find_term(tp.term);
        if (!term_info) continue;

        for (const auto& seg_entry : term_info->by_segment) {
            const SegmentReader& seg = impl_->reader.segment(seg_entry.segment_index);
            const TermEntry&     te  = seg_entry.entry;

            auto block_metas = seg.read_block_metadata(te);
            const float idf  = scorer.idf(term_info->total_doc_frequency);

            const DocId seg_first_doc = seg.doc_range().first_doc;

            for (const auto& bm : block_metas) {
                if (cancel_flag.load()) break;
                if (blocks_read >= impl_->cfg.max_blocks_per_query) break;

                if (plan.use_block_max_wand && results.full()) {
                    const float upper_bound = bm.max_score * idf;
                    if (upper_bound <= results.threshold()) {
                        ++resp.blocks_read;
                        continue;
                    }
                }

                // Try block cache first
                const CompressedBlock* cached = block_cache.find(bm.block_id);
                if (!cached) {
                    auto cb = seg.read_block(bm);
                    if (!cb) continue;
                    block_cache.insert(bm.block_id, *cb);
                    cached = block_cache.find(bm.block_id);
                    if (!cached) continue;
                }

                ++blocks_read;
                bytes_read += cached->data.size();

                auto postings = decode_block_data(*cached, seg_first_doc);
                bytes_decoded += postings.size() * sizeof(Posting);

                for (const auto& p : postings) {
                    if (doc_scores.size() >= impl_->cfg.max_candidates) break;

                    auto meta = seg.get_doc_meta(p.doc_id);
                    if (!meta) continue;

                    const float s = scorer.score(
                        term_info->total_doc_frequency,
                        p.term_freq,
                        meta->term_count);

                    doc_scores[p.doc_id] += s;
                    if (required_terms > 0) ++doc_term_hits[p.doc_id];
                }

                const bool useful = !doc_scores.empty();
                impl_->locality.record_access(bm.block_id, useful);
            }
        }
    }

    // AND semantics: remove docs that don't have all required terms
    if (required_terms > 1) {
        for (auto it = doc_scores.begin(); it != doc_scores.end(); ) {
            auto hit_it = doc_term_hits.find(it->first);
            if (hit_it == doc_term_hits.end() || hit_it->second < required_terms) {
                it = doc_scores.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Populate result set from accumulators.
    for (auto& [doc_id, score] : doc_scores) {
        results.try_insert(doc_id, score);
    }

    // Assemble top-K results.
    auto candidates = results.top_k();
    resp.results.reserve(candidates.size());

    for (uint32_t rank = 0; rank < candidates.size(); ++rank) {
        const DocId doc_id = candidates[rank].doc_id;
        SearchResult sr;
        sr.rank  = rank + 1;
        sr.score = candidates[rank].score;

        for (std::size_t si = 0; si < impl_->reader.segment_count(); ++si) {
            const SegmentReader& seg = impl_->reader.segment(si);
            const auto range = seg.doc_range();
            if (doc_id < range.first_doc || doc_id > range.last_doc) continue;
            if (seg.get_doc_display(doc_id, sr.url, sr.title, sr.excerpt)) break;
        }
        resp.results.push_back(std::move(sr));
    }

    resp.total_candidates_evaluated = static_cast<uint32_t>(doc_scores.size());
    resp.blocks_read   = blocks_read;
    resp.bytes_read    = bytes_read;
    resp.bytes_decoded = bytes_decoded;
    resp.elapsed_ms    = elapsed_ms_since(start_time);

    return resp;
}

void AliScanner::purge_caches() {}

const ScannerConfig& AliScanner::config() const noexcept { return impl_->cfg; }

} // namespace ali
