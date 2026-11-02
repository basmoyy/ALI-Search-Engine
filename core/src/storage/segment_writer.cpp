// segment_writer.cpp — Immutable segment builder.
//
// Writes posting lists, term dictionary, block metadata and document table
// to a temporary directory, then publishes atomically via rename.
//
// Wire format (FORMAT_VERSION 2):
//   segment_header.bin  — magic, version, generation, doc_id_base, doc_count, term_count
//   terms.bin           — sorted term dictionary
//   postings.bin        — compressed posting-list blocks
//   blocks.bin          — BlockMeta array (for Block-Max WAND)
//   docs.bin            — DocumentMeta array (8-byte global DocIds)
//   content.bin         — optional title + excerpt blobs

#include "ali/storage/segment_writer.h"
#include "ali/compression/codec.h"
#include "ali/ranking/bm25.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ali {

// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t  kSegMagic[4]  = {'A','L','I','S'};
static constexpr uint32_t kSegFmtVersion = 2;

struct InMemoryPosting {
    uint32_t local_id;   // per-segment local id (0..N-1)
    uint32_t term_freq;
};

struct InMemoryTerm {
    std::string               text;
    std::vector<InMemoryPosting> postings;
    uint32_t                  doc_frequency = 0;
};

struct InMemoryDoc {
    uint32_t    local_id;
    std::string url;
    std::string title;
    std::string excerpt;   // first 256 chars of body
    uint32_t    term_count = 0;
};

struct SegmentWriter::Impl {
    std::filesystem::path tmp_dir;
    SegmentWriterConfig   cfg;

    uint32_t next_local_id = 0;
    std::unordered_map<std::string, InMemoryTerm> term_map;
    std::vector<InMemoryDoc> docs;
    uint64_t estimated_bytes = 0;

    bool full() const noexcept {
        return next_local_id >= cfg.max_docs
            || estimated_bytes >= cfg.max_segment_bytes;
    }

    explicit Impl(std::filesystem::path d, SegmentWriterConfig c)
        : tmp_dir(std::move(d)), cfg(std::move(c)) {}
};

SegmentWriter::SegmentWriter(std::filesystem::path tmp_dir, SegmentWriterConfig cfg)
    : impl_(new Impl(std::move(tmp_dir), cfg)) {}

SegmentWriter::~SegmentWriter() = default;
SegmentWriter::SegmentWriter(SegmentWriter&&) noexcept = default;
SegmentWriter& SegmentWriter::operator=(SegmentWriter&&) noexcept = default;

DocId SegmentWriter::add_document(
    std::string_view url,
    std::string_view title,
    std::string_view text,
    const std::vector<std::pair<std::string, uint32_t>>& term_freqs)
{
    if (impl_->full() || url.empty()) return kInvalidDocId;

    const uint32_t local_id = impl_->next_local_id++;

    InMemoryDoc doc;
    doc.local_id   = local_id;
    doc.url        = url;
    doc.title      = title;
    doc.excerpt    = std::string(text.substr(0, 256));
    doc.term_count = 0;

    for (const auto& [term, freq] : term_freqs) {
        if (term.empty()) continue;
        doc.term_count += freq;
        auto& entry = impl_->term_map[term];
        if (entry.text.empty()) entry.text = term;
        entry.postings.push_back({local_id, freq});
        entry.doc_frequency++;
        impl_->estimated_bytes += term.size() + 8;
    }

    impl_->docs.push_back(std::move(doc));
    impl_->estimated_bytes += url.size() + title.size() + 64;
    // Return a placeholder DocId using local_id; caller uses doc_id_base to form global id
    return static_cast<DocId>(local_id);
}

bool SegmentWriter::finish(const std::filesystem::path& dest_dir,
                            Generation generation,
                            uint32_t   doc_id_base) {
    if (impl_->docs.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) return false;

    const uint32_t doc_count = static_cast<uint32_t>(impl_->docs.size());

    // Compute avg doc length for WAND max_score
    uint64_t total_terms = 0;
    for (const auto& d : impl_->docs) total_terms += d.term_count;
    const float avg_dl = (doc_count > 0)
        ? static_cast<float>(total_terms) / static_cast<float>(doc_count)
        : 1.0f;

    // ── Write docs.bin ───────────────────────────────────────────────────────
    {
        std::ofstream f(dest_dir / "docs.bin", std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(&doc_count), 4);
        for (const auto& d : impl_->docs) {
            // Convert local_id to global DocId
            const DocId global_id = global_doc_id(doc_id_base, d.local_id);
            f.write(reinterpret_cast<const char*>(&global_id),   8);
            f.write(reinterpret_cast<const char*>(&d.term_count), 4);
            const uint16_t url_len  = static_cast<uint16_t>(d.url.size());
            const uint16_t ttl_len  = static_cast<uint16_t>(d.title.size());
            const uint16_t exc_len  = static_cast<uint16_t>(d.excerpt.size());
            f.write(reinterpret_cast<const char*>(&url_len), 2);
            f.write(d.url.data(), url_len);
            f.write(reinterpret_cast<const char*>(&ttl_len), 2);
            f.write(d.title.data(), ttl_len);
            f.write(reinterpret_cast<const char*>(&exc_len), 2);
            f.write(d.excerpt.data(), exc_len);
        }
        if (!f) return false;
    }

    // ── Build sorted term list ───────────────────────────────────────────────
    std::vector<InMemoryTerm*> sorted_terms;
    sorted_terms.reserve(impl_->term_map.size());
    for (auto& [k, v] : impl_->term_map) sorted_terms.push_back(&v);
    std::sort(sorted_terms.begin(), sorted_terms.end(),
              [](const InMemoryTerm* a, const InMemoryTerm* b) {
                  return a->text < b->text;
              });

    // ── Write postings.bin + blocks.bin + terms.bin ──────────────────────────
    {
        std::ofstream pf(dest_dir / "postings.bin", std::ios::binary | std::ios::trunc);
        std::ofstream bf(dest_dir / "blocks.bin",   std::ios::binary | std::ios::trunc);
        std::ofstream tf(dest_dir / "terms.bin",    std::ios::binary | std::ios::trunc);
        if (!pf || !bf || !tf) return false;

        const uint32_t term_count = static_cast<uint32_t>(sorted_terms.size());
        tf.write(reinterpret_cast<const char*>(&term_count), 4);

        auto& codec = compression::codec_for_id(compression::default_codec());
        const uint32_t block_size = impl_->cfg.block_size_docs;

        // Build scorer for WAND max_score computation
        Bm25Scorer scorer({}, doc_count, avg_dl);

        for (InMemoryTerm* t : sorted_terms) {
            const uint64_t postings_offset = static_cast<uint64_t>(pf.tellp());
            const uint64_t block_meta_offset = static_cast<uint64_t>(bf.tellp());

            // Split postings into blocks
            auto& postings = t->postings;
            const uint32_t total     = static_cast<uint32_t>(postings.size());
            const uint32_t n_blocks  = (total + block_size - 1) / block_size;
            uint32_t written_blocks  = 0;

            for (uint32_t b = 0; b < n_blocks; ++b) {
                const uint32_t bstart = b * block_size;
                const uint32_t bend   = std::min(bstart + block_size, total);
                const uint32_t bcount = bend - bstart;

                std::vector<uint32_t> doc_ids, freqs;
                doc_ids.reserve(bcount);
                freqs.reserve(bcount);

                // Find min term_count in block for WAND upper bound
                uint32_t min_term_count = UINT32_MAX;
                for (uint32_t i = bstart; i < bend; ++i) {
                    const uint32_t local_id = postings[i].local_id;
                    if (local_id < impl_->docs.size()) {
                        min_term_count = std::min(min_term_count,
                            impl_->docs[local_id].term_count);
                    }
                    doc_ids.push_back(local_id);
                    freqs.push_back(postings[i].term_freq);
                }
                if (min_term_count == UINT32_MAX) min_term_count = 1;

                // Compute max_score using BM25 upper bound
                const float max_score = scorer.max_tf_contribution(
                    t->doc_frequency, 0, min_term_count);

                auto deltas = compression::encode_deltas(doc_ids);

                std::vector<uint8_t> enc_doc(codec.max_encoded_size(deltas.size()));
                std::vector<uint8_t> enc_freq(codec.max_encoded_size(freqs.size()));

                const std::size_t doc_bytes  = codec.encode(deltas, enc_doc);
                const std::size_t freq_bytes = codec.encode(freqs,  enc_freq);

                const uint64_t block_file_offset = static_cast<uint64_t>(pf.tellp());
                const uint32_t compressed_len    =
                    static_cast<uint32_t>(4 + 4 + doc_bytes + freq_bytes);

                // Block payload: doc_count[4] freq_count[4] doc_bytes freq_bytes
                pf.write(reinterpret_cast<const char*>(&bcount), 4);
                pf.write(reinterpret_cast<const char*>(&bcount), 4);
                pf.write(reinterpret_cast<const char*>(enc_doc.data()),  static_cast<std::streamsize>(doc_bytes));
                pf.write(reinterpret_cast<const char*>(enc_freq.data()), static_cast<std::streamsize>(freq_bytes));

                // Block metadata — use global DocIds
                const DocId first_global = global_doc_id(doc_id_base, doc_ids.front());
                const DocId last_global  = global_doc_id(doc_id_base, doc_ids.back());

                BlockMeta bm;
                bm.block_id        = (static_cast<uint64_t>(doc_id_base) << 32) | b;
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

            // Term dictionary entry
            const uint16_t tlen = static_cast<uint16_t>(t->text.size());
            tf.write(reinterpret_cast<const char*>(&tlen), 2);
            tf.write(t->text.data(), tlen);
            tf.write(reinterpret_cast<const char*>(&t->doc_frequency), 4);
            tf.write(reinterpret_cast<const char*>(&postings_offset),  8);
            tf.write(reinterpret_cast<const char*>(&postings_length),  4);
            tf.write(reinterpret_cast<const char*>(&written_blocks),   4);
            tf.write(reinterpret_cast<const char*>(&block_meta_offset), 8);
        }

        if (!pf || !bf || !tf) return false;
    }

    // ── Write segment_header.bin ─────────────────────────────────────────────
    {
        std::ofstream hf(dest_dir / "segment_header.bin",
                         std::ios::binary | std::ios::trunc);
        if (!hf) return false;
        hf.write(reinterpret_cast<const char*>(kSegMagic), 4);
        hf.write(reinterpret_cast<const char*>(&kSegFmtVersion), 4);
        hf.write(reinterpret_cast<const char*>(&generation), 4);
        hf.write(reinterpret_cast<const char*>(&doc_id_base), 4);
        hf.write(reinterpret_cast<const char*>(&doc_count), 4);
        const uint32_t term_count = static_cast<uint32_t>(impl_->term_map.size());
        hf.write(reinterpret_cast<const char*>(&term_count), 4);
        if (!hf) return false;
    }

    // Clear state so this writer is no longer usable.
    impl_->term_map.clear();
    impl_->docs.clear();
    impl_->estimated_bytes = 0;
    impl_->next_local_id   = 0;

    return true;
}

uint32_t SegmentWriter::doc_count()       const noexcept { return impl_->next_local_id; }
uint64_t SegmentWriter::estimated_bytes() const noexcept { return impl_->estimated_bytes; }
bool     SegmentWriter::is_full()         const noexcept { return impl_->full(); }

} // namespace ali
