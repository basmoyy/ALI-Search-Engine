// segment_writer.cpp — Immutable segment builder.
//
// Writes posting lists, term dictionary, block metadata and document table
// to a temporary directory, then publishes atomically via rename.
//
// Wire format (FORMAT_VERSION 1):
//   segment_header.bin  — magic, version, generation, counts, checksums
//   terms.bin           — sorted term dictionary
//   postings.bin        — compressed posting-list blocks
//   blocks.bin          — BlockMeta array (for Block-Max WAND)
//   docs.bin            — DocumentMeta array
//   content.bin         — optional title + excerpt blobs

#include "ali/storage/segment_writer.h"
#include "ali/compression/codec.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ali {

// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t  kSegMagic[4]  = {'A','L','I','S'};
static constexpr uint32_t kSegFmtVersion = 1;

struct InMemoryPosting {
    DocId    doc_id;
    uint32_t term_freq;
};

struct InMemoryTerm {
    std::string               text;
    std::vector<InMemoryPosting> postings;
    uint32_t                  doc_frequency = 0;
};

struct InMemoryDoc {
    DocId       doc_id;
    std::string url;
    std::string title;
    std::string excerpt;   // first 256 chars of body
    uint32_t    term_count = 0;
};

struct SegmentWriter::Impl {
    std::filesystem::path tmp_dir;
    SegmentWriterConfig   cfg;

    DocId next_doc_id = 0;
    std::unordered_map<std::string, InMemoryTerm> term_map;
    std::vector<InMemoryDoc> docs;
    uint64_t estimated_bytes = 0;

    bool full() const noexcept {
        return next_doc_id >= cfg.max_docs
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

    const DocId doc_id = impl_->next_doc_id++;

    InMemoryDoc doc;
    doc.doc_id     = doc_id;
    doc.url        = url;
    doc.title      = title;
    doc.excerpt    = std::string(text.substr(0, 256));
    doc.term_count = 0;

    for (const auto& [term, freq] : term_freqs) {
        if (term.empty()) continue;
        doc.term_count += freq;
        auto& entry = impl_->term_map[term];
        if (entry.text.empty()) entry.text = term;
        entry.postings.push_back({doc_id, freq});
        entry.doc_frequency++;
        impl_->estimated_bytes += term.size() + 8;
    }

    impl_->docs.push_back(std::move(doc));
    impl_->estimated_bytes += url.size() + title.size() + 64;
    return doc_id;
}

bool SegmentWriter::finish(const std::filesystem::path& dest_dir, Generation generation) {
    if (impl_->docs.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) return false;

    // ── Write docs.bin ───────────────────────────────────────────────────────
    {
        std::ofstream f(dest_dir / "docs.bin", std::ios::binary | std::ios::trunc);
        if (!f) return false;
        const uint32_t count = static_cast<uint32_t>(impl_->docs.size());
        f.write(reinterpret_cast<const char*>(&count), 4);
        for (const auto& d : impl_->docs) {
            f.write(reinterpret_cast<const char*>(&d.doc_id),     4);
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
                float max_score = 0.0f;

                for (uint32_t i = bstart; i < bend; ++i) {
                    doc_ids.push_back(postings[i].doc_id);
                    freqs.push_back(postings[i].term_freq);
                    // crude upper-bound score for this block (full BM25 computed in ranking)
                    max_score = std::max(max_score,
                        static_cast<float>(postings[i].term_freq) + 1.0f);
                }

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

                // Block metadata
                BlockMeta bm;
                bm.block_id        = static_cast<BlockId>(
                    (static_cast<uint64_t>(b) << 32) | doc_ids.front());
                bm.first_doc       = doc_ids.front();
                bm.last_doc        = doc_ids.back();
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
        const uint32_t doc_count = static_cast<uint32_t>(impl_->docs.size());
        hf.write(reinterpret_cast<const char*>(&doc_count), 4);
        const uint32_t term_count = static_cast<uint32_t>(impl_->term_map.size());
        hf.write(reinterpret_cast<const char*>(&term_count), 4);
        if (!hf) return false;
    }

    // Clear state so this writer is no longer usable.
    impl_->term_map.clear();
    impl_->docs.clear();
    impl_->estimated_bytes = 0;
    impl_->next_doc_id     = 0;

    return true;
}

uint32_t SegmentWriter::doc_count()       const noexcept { return impl_->next_doc_id; }
uint64_t SegmentWriter::estimated_bytes() const noexcept { return impl_->estimated_bytes; }
bool     SegmentWriter::is_full()         const noexcept { return impl_->full(); }

} // namespace ali
