// compaction.cpp — Background segment compaction.
//
// Merges two smallest segments via proper posting-list merge. Skips documents
// whose URL has status == Removed. Writes format v2 (8-byte global DocIds).

#include "ali/index/compaction.h"
#include "ali/storage/segment_reader.h"
#include "ali/storage/segment_writer.h"
#include "ali/compression/codec.h"
#include "ali/ranking/bm25.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_set>

namespace ali {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string new_segment_id() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(16) << rng()
       << std::setw(16) << rng();
    return ss.str();
}

static int64_t now_unix() {
    using namespace std::chrono;
    return duration_cast<seconds>(
        system_clock::now().time_since_epoch()).count();
}

// ── Compaction ────────────────────────────────────────────────────────────────

Compaction::Compaction(Manifest&           manifest,
                       MetadataDb&         db,
                       ResourceController& resources,
                       CompactionConfig    cfg)
    : manifest_(manifest), db_(db), resources_(resources), cfg_(std::move(cfg)) {}

bool Compaction::needs_compaction() const {
    const auto snap = manifest_.snapshot();
    uint32_t live = 0;
    for (const auto& e : snap.segments) {
        if (!e.is_tombstone) ++live;
    }
    return live > cfg_.max_segments_before_compact;
}

CompactionResult Compaction::run(std::atomic<bool>& cancel_flag) {
    CompactionResult result;

    if (!resources_.may_compact()) {
        result.error_detail = "Resource conditions do not permit compaction";
        return result;
    }

    const auto snap = manifest_.snapshot();

    std::vector<const SegmentEntry*> live;
    for (const auto& e : snap.segments) {
        if (!e.is_tombstone) live.push_back(&e);
    }

    if (live.size() <= cfg_.max_segments_before_compact) {
        result.completed = true;
        return result;
    }

    std::sort(live.begin(), live.end(),
              [](const SegmentEntry* a, const SegmentEntry* b) {
                  return a->doc_range.count() < b->doc_range.count();
              });

    if (live.size() < 2) { result.completed = true; return result; }
    const SegmentEntry* seg_a = live[0];
    const SegmentEntry* seg_b = live[1];

    if (cancel_flag.load()) {
        result.error_detail = "Cancelled before merge started";
        return result;
    }

    SegmentReader reader_a(seg_a->path);
    SegmentReader reader_b(seg_b->path);
    if (!reader_a.is_valid() || !reader_b.is_valid()) {
        result.error_detail = "Cannot open one or both source segments";
        return result;
    }

    // Compute new doc_id_base: max(both bases + their doc counts)
    const uint32_t base_a = doc_id_base(seg_a->doc_range.first_doc);
    const uint32_t base_b = doc_id_base(seg_b->doc_range.first_doc);
    // new base = one past the last used base+count in either segment
    const uint32_t new_base = std::max(
        base_a + reader_a.doc_count(),
        base_b + reader_b.doc_count()
    );

    // Build set of removed URLs for quick lookup
    std::unordered_set<std::string> removed_urls;
    {
        auto removed = db_.find_by_status(CrawlStatus::Removed, 1000000);
        for (const auto& r : removed) removed_urls.insert(r.url);
    }

    // Collect all terms from both segments (union)
    std::unordered_set<std::string> all_terms_set;
    {
        // Read terms from both segment directories
        auto read_terms = [&](const std::filesystem::path& seg_path) {
            std::ifstream tf(seg_path / "terms.bin", std::ios::binary);
            if (!tf) return;
            uint32_t count = 0;
            tf.read(reinterpret_cast<char*>(&count), 4);
            for (uint32_t i = 0; i < count; ++i) {
                uint16_t tlen = 0;
                tf.read(reinterpret_cast<char*>(&tlen), 2);
                std::string term(tlen, '\0');
                tf.read(term.data(), tlen);
                all_terms_set.insert(term);
                // Skip the rest of the entry
                tf.seekg(4 + 8 + 4 + 4 + 8, std::ios::cur);
            }
        };
        read_terms(seg_a->path);
        read_terms(seg_b->path);
    }

    // Build document display data from both segments
    // Map: global DocId → (url, title, excerpt, term_count)
    struct DocInfo {
        std::string url;
        std::string title;
        std::string excerpt;
        uint32_t    term_count = 0;
    };
    std::unordered_map<DocId, DocInfo> doc_info_map;
    {
        auto collect_docs = [&](const SegmentReader& reader) {
            const uint32_t count = reader.doc_count();
            const SegmentRange range = reader.doc_range();
            for (DocId gid = range.first_doc;
                 gid <= range.last_doc && gid != kInvalidDocId;
                 ++gid) {
                DocInfo info;
                if (!reader.get_doc_display(gid, info.url, info.title, info.excerpt))
                    continue;
                if (removed_urls.count(info.url)) {
                    ++result.docs_removed;
                    continue;
                }
                auto meta = reader.get_doc_meta(gid);
                if (meta) info.term_count = meta->term_count;
                doc_info_map[gid] = std::move(info);
            }
        };
        collect_docs(reader_a);
        collect_docs(reader_b);
    }

    // Assign new local ids to surviving docs
    // Sort by old global DocId for determinism
    std::vector<std::pair<DocId, DocId>> old_to_new;  // old global → new global
    old_to_new.reserve(doc_info_map.size());
    {
        std::vector<DocId> sorted_ids;
        sorted_ids.reserve(doc_info_map.size());
        for (const auto& [id, _] : doc_info_map) sorted_ids.push_back(id);
        std::sort(sorted_ids.begin(), sorted_ids.end());
        uint32_t local = 0;
        for (DocId old_id : sorted_ids) {
            old_to_new.push_back({old_id, global_doc_id(new_base, local++)});
        }
    }
    // Build reverse map: old → new
    std::unordered_map<DocId, DocId> id_remap;
    id_remap.reserve(old_to_new.size());
    for (const auto& [old_id, new_id] : old_to_new) {
        id_remap[old_id] = new_id;
    }

    // Prepare output directory
    const std::string merged_id = new_segment_id();
    const auto index_root = seg_a->path.parent_path();
    const auto tmp_dir    = index_root / ("seg_" + merged_id + ".tmp");
    const auto dest_dir   = index_root / ("seg_" + merged_id);

    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);
    if (ec) {
        result.error_detail = "Cannot create tmp directory";
        return result;
    }

    const uint32_t merged_doc_count = static_cast<uint32_t>(old_to_new.size());

    // Compute avg_dl for merged segment
    uint64_t total_terms_sum = 0;
    for (const auto& [gid, info] : doc_info_map) total_terms_sum += info.term_count;
    const float avg_dl = (merged_doc_count > 0)
        ? static_cast<float>(total_terms_sum) / static_cast<float>(merged_doc_count)
        : 1.0f;

    Bm25Scorer scorer({}, merged_doc_count, avg_dl);
    auto& codec = compression::codec_for_id(compression::default_codec());
    const uint32_t block_size = cfg_.block_size_docs;

    // ── Write docs.bin ───────────────────────────────────────────────────────
    {
        std::ofstream df(tmp_dir / "docs.bin", std::ios::binary | std::ios::trunc);
        if (!df) { std::filesystem::remove_all(tmp_dir); result.error_detail = "docs.bin write failed"; return result; }
        df.write(reinterpret_cast<const char*>(&merged_doc_count), 4);
        for (const auto& [old_id, new_id] : old_to_new) {
            const auto& info = doc_info_map.at(old_id);
            const uint32_t tc = info.term_count;
            df.write(reinterpret_cast<const char*>(&new_id), 8);
            df.write(reinterpret_cast<const char*>(&tc), 4);
            const uint16_t ul = static_cast<uint16_t>(info.url.size());
            const uint16_t tl = static_cast<uint16_t>(info.title.size());
            const uint16_t el = static_cast<uint16_t>(info.excerpt.size());
            df.write(reinterpret_cast<const char*>(&ul), 2); df.write(info.url.data(), ul);
            df.write(reinterpret_cast<const char*>(&tl), 2); df.write(info.title.data(), tl);
            df.write(reinterpret_cast<const char*>(&el), 2); df.write(info.excerpt.data(), el);
        }
        if (!df) { std::filesystem::remove_all(tmp_dir); result.error_detail = "docs.bin flush failed"; return result; }
    }

    // ── Collect and merge posting lists per term ─────────────────────────────
    std::vector<std::string> sorted_terms(all_terms_set.begin(), all_terms_set.end());
    std::sort(sorted_terms.begin(), sorted_terms.end());

    auto read_postings = [&](const SegmentReader& reader, const std::string& term)
        -> std::vector<std::pair<DocId, uint32_t>> {
        std::vector<std::pair<DocId, uint32_t>> postings;
        auto te = reader.find_term(term);
        if (!te) return postings;
        auto block_metas = reader.read_block_metadata(*te);
        for (const auto& bm : block_metas) {
            auto cb = reader.read_block(bm);
            if (!cb) continue;
            // Decode block
            const auto& data = cb->data;
            if (data.size() < 8) continue;
            uint32_t doc_count_in_block = 0;
            std::memcpy(&doc_count_in_block, data.data(), 4);
            std::vector<uint32_t> doc_deltas(doc_count_in_block), freqs(doc_count_in_block);
            std::span<const uint8_t> rest(data.data() + 8, data.size() - 8);
            const std::size_t dc = codec.decode(rest, doc_count_in_block, doc_deltas);
            if (dc == 0) continue;
            std::span<const uint8_t> freq_span(rest.data() + dc, rest.size() - dc);
            if (codec.decode(freq_span, doc_count_in_block, freqs) == 0) continue;
            auto ids = compression::decode_deltas(doc_deltas);
            // ids are local ids from the segment; reconstruct global ids
            const uint32_t seg_base = doc_id_base(reader.doc_range().first_doc);
            for (uint32_t i = 0; i < doc_count_in_block; ++i) {
                const DocId global_id = global_doc_id(seg_base, ids[i]);
                postings.push_back({global_id, freqs[i]});
            }
        }
        return postings;
    };

    std::ofstream pf(tmp_dir / "postings.bin", std::ios::binary | std::ios::trunc);
    std::ofstream bf(tmp_dir / "blocks.bin",   std::ios::binary | std::ios::trunc);
    std::ofstream tf(tmp_dir / "terms.bin",    std::ios::binary | std::ios::trunc);
    if (!pf || !bf || !tf) {
        std::filesystem::remove_all(tmp_dir);
        result.error_detail = "Cannot open output files";
        return result;
    }

    const uint32_t term_count = static_cast<uint32_t>(sorted_terms.size());
    tf.write(reinterpret_cast<const char*>(&term_count), 4);

    for (const auto& term : sorted_terms) {
        if (cancel_flag.load()) break;
        if (!resources_.may_compact()) break;

        // Read and merge postings from both segments
        auto posts_a = read_postings(reader_a, term);
        auto posts_b = read_postings(reader_b, term);

        // Combine, remap to new ids, filter removed
        std::vector<std::pair<DocId, uint32_t>> merged;
        merged.reserve(posts_a.size() + posts_b.size());
        for (auto& [old_id, freq] : posts_a) {
            auto it = id_remap.find(old_id);
            if (it != id_remap.end()) merged.push_back({it->second, freq});
        }
        for (auto& [old_id, freq] : posts_b) {
            auto it = id_remap.find(old_id);
            if (it != id_remap.end()) merged.push_back({it->second, freq});
        }

        if (merged.empty()) continue;

        // Sort by new DocId
        std::sort(merged.begin(), merged.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        const uint64_t postings_offset   = static_cast<uint64_t>(pf.tellp());
        const uint64_t block_meta_offset = static_cast<uint64_t>(bf.tellp());

        const uint32_t total    = static_cast<uint32_t>(merged.size());
        const uint32_t n_blocks = (total + block_size - 1) / block_size;
        uint32_t written_blocks = 0;

        for (uint32_t b = 0; b < n_blocks; ++b) {
            const uint32_t bstart = b * block_size;
            const uint32_t bend   = std::min(bstart + block_size, total);
            const uint32_t bcount = bend - bstart;

            std::vector<uint32_t> local_ids, freqs;
            local_ids.reserve(bcount);
            freqs.reserve(bcount);

            // Find min term_count for WAND bound
            uint32_t min_term_count = UINT32_MAX;
            for (uint32_t i = bstart; i < bend; ++i) {
                const DocId gid = merged[i].first;
                auto it = id_remap.end();
                // Find in doc_info_map via old_to_new reverse
                // new_id -> old_id lookup via old_to_new
                for (const auto& [oid, nid] : old_to_new) {
                    if (nid == gid) {
                        auto dit = doc_info_map.find(oid);
                        if (dit != doc_info_map.end())
                            min_term_count = std::min(min_term_count, dit->second.term_count);
                        break;
                    }
                }
                local_ids.push_back(local_doc_id(gid));
                freqs.push_back(merged[i].second);
            }
            if (min_term_count == UINT32_MAX) min_term_count = 1;

            const float max_score = scorer.max_tf_contribution(
                static_cast<uint32_t>(merged.size()), 0, min_term_count);

            auto deltas = compression::encode_deltas(local_ids);
            std::vector<uint8_t> enc_doc(codec.max_encoded_size(deltas.size()));
            std::vector<uint8_t> enc_freq(codec.max_encoded_size(freqs.size()));
            const std::size_t doc_bytes  = codec.encode(deltas, enc_doc);
            const std::size_t freq_bytes = codec.encode(freqs,  enc_freq);

            const uint64_t block_file_offset = static_cast<uint64_t>(pf.tellp());
            const uint32_t compressed_len    =
                static_cast<uint32_t>(4 + 4 + doc_bytes + freq_bytes);

            pf.write(reinterpret_cast<const char*>(&bcount), 4);
            pf.write(reinterpret_cast<const char*>(&bcount), 4);
            pf.write(reinterpret_cast<const char*>(enc_doc.data()),  static_cast<std::streamsize>(doc_bytes));
            pf.write(reinterpret_cast<const char*>(enc_freq.data()), static_cast<std::streamsize>(freq_bytes));

            const DocId first_global = merged[bstart].first;
            const DocId last_global  = merged[bend - 1].first;

            BlockMeta bm;
            bm.block_id        = (static_cast<uint64_t>(new_base) << 32) | b;
            bm.first_doc       = first_global;
            bm.last_doc        = last_global;
            bm.max_score       = max_score;
            bm.file_offset     = block_file_offset;
            bm.compressed_len  = compressed_len;
            bm.doc_count       = bcount;
            bf.write(reinterpret_cast<const char*>(&bm), sizeof(bm));

            ++written_blocks;
        }

        const uint32_t postings_length =
            static_cast<uint32_t>(static_cast<uint64_t>(pf.tellp()) - postings_offset);
        const uint32_t df_count = static_cast<uint32_t>(merged.size());

        const uint16_t tlen = static_cast<uint16_t>(term.size());
        tf.write(reinterpret_cast<const char*>(&tlen), 2);
        tf.write(term.data(), tlen);
        tf.write(reinterpret_cast<const char*>(&df_count), 4);
        tf.write(reinterpret_cast<const char*>(&postings_offset),   8);
        tf.write(reinterpret_cast<const char*>(&postings_length),   4);
        tf.write(reinterpret_cast<const char*>(&written_blocks),    4);
        tf.write(reinterpret_cast<const char*>(&block_meta_offset), 8);
    }

    if (!pf || !bf || !tf) {
        std::filesystem::remove_all(tmp_dir);
        result.error_detail = "Output file error during posting merge";
        return result;
    }
    pf.close(); bf.close(); tf.close();

    if (cancel_flag.load()) {
        std::filesystem::remove_all(tmp_dir);
        result.error_detail = "Cancelled during merge";
        return result;
    }

    // ── Write segment_header.bin ─────────────────────────────────────────────
    {
        static constexpr uint8_t  kSegMagic[4]   = {'A','L','I','S'};
        static constexpr uint32_t kSegFmtVersion2 = 2;
        const Generation gen = static_cast<Generation>(snap.manifest_version + 1);

        std::ofstream hf(tmp_dir / "segment_header.bin", std::ios::binary | std::ios::trunc);
        if (!hf) { std::filesystem::remove_all(tmp_dir); result.error_detail = "header write failed"; return result; }
        hf.write(reinterpret_cast<const char*>(kSegMagic), 4);
        hf.write(reinterpret_cast<const char*>(&kSegFmtVersion2), 4);
        hf.write(reinterpret_cast<const char*>(&gen), 4);
        hf.write(reinterpret_cast<const char*>(&new_base), 4);
        hf.write(reinterpret_cast<const char*>(&merged_doc_count), 4);
        hf.write(reinterpret_cast<const char*>(&term_count), 4);
        if (!hf) { std::filesystem::remove_all(tmp_dir); result.error_detail = "header flush failed"; return result; }
    }

    // Atomic rename tmp → dest
    std::filesystem::rename(tmp_dir, dest_dir, ec);
    if (ec) {
        std::filesystem::remove_all(tmp_dir);
        result.error_detail = "Rename failed: " + ec.message();
        return result;
    }

    // Compute doc range for the merged entry
    DocId first_new = kInvalidDocId, last_new = kInvalidDocId;
    if (!old_to_new.empty()) {
        first_new = old_to_new.front().second;
        last_new  = old_to_new.back().second;
    }

    SegmentEntry merged_entry;
    merged_entry.generation           = static_cast<Generation>(snap.manifest_version + 1);
    merged_entry.segment_id           = merged_id;
    merged_entry.path                 = dest_dir;
    merged_entry.created_at_unix      = now_unix();
    merged_entry.doc_range.first_doc  = first_new;
    merged_entry.doc_range.last_doc   = last_new;

    // Measure actual file sizes
    std::error_code size_ec;
    uint64_t actual_bytes = 0;
    for (auto const& f : std::filesystem::directory_iterator(dest_dir, size_ec)) {
        if (!size_ec) actual_bytes += std::filesystem::file_size(f.path(), size_ec);
    }
    merged_entry.size_bytes = actual_bytes;

    const bool ok = manifest_.replace_segments(
        {seg_a->segment_id, seg_b->segment_id}, merged_entry);

    if (!ok) {
        result.error_detail = "Manifest update failed";
        std::filesystem::remove_all(dest_dir);
        return result;
    }

    result.segments_merged  = 2;
    result.segments_created = 1;
    result.completed        = true;
    return result;
}

} // namespace ali
