// segment_reader.cpp — Read-only published segment accessor.
//
// Supports format v1 (legacy 4-byte DocIds) and v2 (8-byte global DocIds).
// For v1 segments, local 32-bit ids are promoted to global via
// global_doc_id(doc_id_base_, id32) where doc_id_base_=0.

#include "ali/storage/segment_reader.h"
#include "ali/compression/codec.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>

namespace ali {

static constexpr uint8_t  kSegMagic[4]   = {'A','L','I','S'};
static constexpr uint32_t kSegFmtVersion1 = 1;
static constexpr uint32_t kSegFmtVersion2 = 2;

// ── Sparse doc skip-list entry ─────────────────────────────────────────────────
static constexpr uint32_t DOC_INDEX_SKIP = 16;

struct DocSkipEntry {
    DocId    doc_id      = kInvalidDocId;
    uint32_t file_offset = 0;
    uint32_t term_count  = 0;
};

// ─────────────────────────────────────────────────────────────────────────────

struct SegmentReader::Impl {
    std::filesystem::path dir;
    bool valid = false;

    uint32_t   doc_count_   = 0;
    uint32_t   term_count_  = 0;
    Generation generation_  = 0;
    uint32_t   doc_id_base_ = 0;   ///< segment's global doc_id_base (v2); 0 for v1
    uint32_t   fmt_version_ = 0;
    SegmentRange doc_range_;
    float      avg_dl_      = 0.0f;

    // ── Term dictionary (fully in RAM) ────────────────────────────────────────
    struct TermCache {
        std::string text;
        uint32_t    doc_frequency    = 0;
        uint64_t    postings_offset  = 0;
        uint32_t    postings_length  = 0;
        uint32_t    block_count      = 0;
        uint64_t    block_meta_offset = 0;
    };
    std::vector<TermCache> term_dict;

    // ── Sparse doc skip-list ──────────────────────────────────────────────────
    std::vector<DocSkipEntry> doc_skip;

    std::filesystem::path postings_path() const { return dir / "postings.bin"; }
    std::filesystem::path blocks_path()   const { return dir / "blocks.bin";   }
    std::filesystem::path docs_path()     const { return dir / "docs.bin";     }

    explicit Impl(std::filesystem::path d) : dir(std::move(d)) {}

    // ── Read one doc record from docs.bin ─────────────────────────────────────
    // v2: doc_id[8] term_count[4] url_len[2] url title_len[2] title excerpt_len[2] excerpt
    // v1: doc_id[4] term_count[4] url_len[2] url title_len[2] title excerpt_len[2] excerpt
    struct DocRecord {
        DocId       doc_id     = kInvalidDocId;
        uint32_t    term_count = 0;
        std::string url;
        std::string title;
        std::string excerpt;
        bool        valid      = false;
    };

    DocRecord read_doc_record(std::ifstream& df) const {
        DocRecord r;
        if (fmt_version_ >= 2) {
            df.read(reinterpret_cast<char*>(&r.doc_id), 8);
        } else {
            uint32_t id32 = 0;
            df.read(reinterpret_cast<char*>(&id32), 4);
            r.doc_id = global_doc_id(doc_id_base_, id32);
        }
        df.read(reinterpret_cast<char*>(&r.term_count), 4);
        uint16_t ul = 0, tl = 0, el = 0;
        df.read(reinterpret_cast<char*>(&ul), 2);
        r.url.resize(ul);    df.read(r.url.data(), ul);
        df.read(reinterpret_cast<char*>(&tl), 2);
        r.title.resize(tl);  df.read(r.title.data(), tl);
        df.read(reinterpret_cast<char*>(&el), 2);
        r.excerpt.resize(el); df.read(r.excerpt.data(), el);
        r.valid = df.good() || df.eof();
        return r;
    }

    // ── Find doc by binary-searching skip-list then scanning forward ──────────
    DocRecord find_doc(DocId target_id) const {
        if (doc_skip.empty()) return {};

        auto it = std::upper_bound(doc_skip.begin(), doc_skip.end(), target_id,
                                    [](DocId id, const DocSkipEntry& e) {
                                        return id < e.doc_id;
                                    });
        if (it == doc_skip.begin()) return {};
        --it;

        std::ifstream df(docs_path(), std::ios::binary);
        if (!df) return {};
        df.seekg(static_cast<std::streamoff>(it->file_offset));
        if (!df) return {};

        for (uint32_t scan = 0; scan < DOC_INDEX_SKIP + 1; ++scan) {
            if (!df || df.eof()) break;
            DocRecord r = read_doc_record(df);
            if (!r.valid) break;
            if (r.doc_id == target_id) return r;
            if (r.doc_id >  target_id) break;
        }
        return {};
    }
};

// ─────────────────────────────────────────────────────────────────────────────

SegmentReader::SegmentReader(std::filesystem::path segment_dir)
    : impl_(new Impl(std::move(segment_dir))) {

    // ── Validate header ───────────────────────────────────────────────────────
    std::ifstream hf(impl_->dir / "segment_header.bin", std::ios::binary);
    if (!hf) return;

    uint8_t magic[4] = {};
    hf.read(reinterpret_cast<char*>(magic), 4);
    if (std::memcmp(magic, kSegMagic, 4) != 0) return;

    uint32_t ver = 0;
    hf.read(reinterpret_cast<char*>(&ver), 4);
    if (ver != kSegFmtVersion1 && ver != kSegFmtVersion2) return;

    impl_->fmt_version_ = ver;

    hf.read(reinterpret_cast<char*>(&impl_->generation_), 4);

    if (ver >= 2) {
        hf.read(reinterpret_cast<char*>(&impl_->doc_id_base_), 4);
    } else {
        impl_->doc_id_base_ = 0;
    }

    hf.read(reinterpret_cast<char*>(&impl_->doc_count_),  4);
    hf.read(reinterpret_cast<char*>(&impl_->term_count_), 4);
    if (!hf) return;

    // ── Load term dictionary fully into RAM ───────────────────────────────────
    std::ifstream tf(impl_->dir / "terms.bin", std::ios::binary);
    if (!tf) return;
    uint32_t tdcount = 0;
    tf.read(reinterpret_cast<char*>(&tdcount), 4);
    impl_->term_dict.resize(tdcount);
    for (auto& e : impl_->term_dict) {
        uint16_t tlen = 0;
        tf.read(reinterpret_cast<char*>(&tlen), 2);
        e.text.resize(tlen);
        tf.read(e.text.data(), tlen);
        tf.read(reinterpret_cast<char*>(&e.doc_frequency),     4);
        tf.read(reinterpret_cast<char*>(&e.postings_offset),   8);
        tf.read(reinterpret_cast<char*>(&e.postings_length),   4);
        tf.read(reinterpret_cast<char*>(&e.block_count),       4);
        tf.read(reinterpret_cast<char*>(&e.block_meta_offset), 8);
    }
    if (!tf) return;

    // ── Build sparse doc skip-list (one scan of docs.bin) ────────────────────
    std::ifstream df(impl_->dir / "docs.bin", std::ios::binary);
    if (!df) return;
    uint32_t dcount = 0;
    df.read(reinterpret_cast<char*>(&dcount), 4);

    impl_->doc_skip.reserve(dcount / DOC_INDEX_SKIP + 1);

    const uint32_t id_field_size = (ver >= 2) ? 8u : 4u;

    uint64_t total_terms   = 0;
    uint32_t offset        = 4;   // starts after the 4-byte count header
    DocId    first_doc     = kInvalidDocId;
    DocId    last_doc      = kInvalidDocId;

    for (uint32_t i = 0; i < dcount; ++i) {
        if (i % DOC_INDEX_SKIP == 0) {
            DocSkipEntry skip;
            skip.file_offset = offset;
            const auto saved_pos = df.tellg();
            DocId    peek_id    = kInvalidDocId;
            uint32_t peek_terms = 0;
            if (ver >= 2) {
                df.read(reinterpret_cast<char*>(&peek_id), 8);
            } else {
                uint32_t id32 = 0;
                df.read(reinterpret_cast<char*>(&id32), 4);
                peek_id = global_doc_id(impl_->doc_id_base_, id32);
            }
            df.read(reinterpret_cast<char*>(&peek_terms), 4);
            skip.doc_id     = peek_id;
            skip.term_count = peek_terms;
            df.seekg(saved_pos);
            impl_->doc_skip.push_back(skip);
        }

        // Scan past this doc record
        DocId    doc_id     = kInvalidDocId;
        uint32_t term_count = 0;
        if (ver >= 2) {
            df.read(reinterpret_cast<char*>(&doc_id), 8);
        } else {
            uint32_t id32 = 0;
            df.read(reinterpret_cast<char*>(&id32), 4);
            doc_id = global_doc_id(impl_->doc_id_base_, id32);
        }
        df.read(reinterpret_cast<char*>(&term_count), 4);
        total_terms += term_count;
        offset      += id_field_size + 4;

        if (i == 0)         first_doc = doc_id;
        if (i == dcount-1)  last_doc  = doc_id;

        for (int field = 0; field < 3; ++field) {
            uint16_t flen = 0;
            df.read(reinterpret_cast<char*>(&flen), 2);
            df.seekg(flen, std::ios::cur);
            offset += 2 + static_cast<uint32_t>(flen);
        }
        if (!df && !df.eof()) return;
    }

    if (dcount > 0) {
        impl_->doc_range_.first_doc = first_doc;
        impl_->doc_range_.last_doc  = last_doc;
        impl_->avg_dl_ = static_cast<float>(total_terms) / static_cast<float>(dcount);
    }

    impl_->doc_count_ = dcount;
    impl_->valid      = true;
}

SegmentReader::~SegmentReader()                           = default;
SegmentReader::SegmentReader(SegmentReader&&) noexcept    = default;
SegmentReader& SegmentReader::operator=(SegmentReader&&) noexcept = default;

bool SegmentReader::is_valid() const noexcept { return impl_->valid; }

// ── Term lookup ───────────────────────────────────────────────────────────────

std::optional<TermEntry> SegmentReader::find_term(const std::string& term) const {
    auto it = std::lower_bound(impl_->term_dict.begin(), impl_->term_dict.end(),
                                term,
                                [](const Impl::TermCache& e, const std::string& t) {
                                    return e.text < t;
                                });
    if (it == impl_->term_dict.end() || it->text != term) return std::nullopt;

    TermEntry e;
    e.term              = it->text;
    e.doc_frequency     = it->doc_frequency;
    e.postings_offset   = it->postings_offset;
    e.postings_length   = it->postings_length;
    e.block_count       = it->block_count;
    e.block_meta_offset = it->block_meta_offset;
    return e;
}

// ── Block reads ───────────────────────────────────────────────────────────────

std::vector<BlockMeta> SegmentReader::read_block_metadata(const TermEntry& entry) const {
    std::vector<BlockMeta> blocks;
    blocks.reserve(entry.block_count);

    std::ifstream bf(impl_->blocks_path(), std::ios::binary);
    if (!bf) return blocks;
    bf.seekg(static_cast<std::streamoff>(entry.block_meta_offset));

    for (uint32_t i = 0; i < entry.block_count; ++i) {
        BlockMeta bm;
        bf.read(reinterpret_cast<char*>(&bm), sizeof(bm));
        if (!bf) break;
        blocks.push_back(bm);
    }
    return blocks;
}

std::optional<CompressedBlock>
SegmentReader::read_block(const BlockMeta& block) const {
    std::ifstream pf(impl_->postings_path(), std::ios::binary);
    if (!pf) return std::nullopt;
    pf.seekg(static_cast<std::streamoff>(block.file_offset));

    CompressedBlock cb;
    cb.meta = block;
    cb.data.resize(block.compressed_len);
    pf.read(reinterpret_cast<char*>(cb.data.data()),
            static_cast<std::streamsize>(block.compressed_len));
    if (!pf) return std::nullopt;
    return cb;
}

// ── Doc metadata ──────────────────────────────────────────────────────────────

std::optional<DocumentMeta> SegmentReader::get_doc_meta(DocId doc_id) const {
    if (impl_->doc_skip.empty()) return std::nullopt;

    auto it = std::upper_bound(impl_->doc_skip.begin(), impl_->doc_skip.end(),
                                doc_id,
                                [](DocId id, const DocSkipEntry& e) {
                                    return id < e.doc_id;
                                });
    if (it == impl_->doc_skip.begin()) return std::nullopt;
    --it;

    DocumentMeta m;
    m.doc_id     = doc_id;
    m.term_count = it->term_count;
    return m;
}

// ── Doc display ───────────────────────────────────────────────────────────────

bool SegmentReader::get_doc_display(DocId        doc_id,
                                     std::string& out_url,
                                     std::string& out_title,
                                     std::string& out_excerpt) const {
    auto r = impl_->find_doc(doc_id);
    if (!r.valid) return false;
    out_url     = std::move(r.url);
    out_title   = std::move(r.title);
    out_excerpt = std::move(r.excerpt);
    return true;
}

// ── Statistics ────────────────────────────────────────────────────────────────

uint32_t   SegmentReader::doc_count()      const noexcept { return impl_->doc_count_; }
float      SegmentReader::avg_doc_length() const noexcept { return impl_->avg_dl_; }
Generation SegmentReader::generation()     const noexcept { return impl_->generation_; }
SegmentRange SegmentReader::doc_range()    const noexcept { return impl_->doc_range_; }

} // namespace ali
