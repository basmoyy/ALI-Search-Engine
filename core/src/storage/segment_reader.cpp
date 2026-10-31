// segment_reader.cpp — Read-only published segment accessor.

#include "ali/storage/segment_reader.h"
#include "ali/compression/codec.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>

namespace ali {

static constexpr uint8_t  kSegMagic[4]   = {'A','L','I','S'};
static constexpr uint32_t kSegFmtVersion  = 1;

// ─────────────────────────────────────────────────────────────────────────────

struct SegmentReader::Impl {
    std::filesystem::path dir;
    bool valid = false;

    uint32_t   doc_count_   = 0;
    uint32_t   term_count_  = 0;
    Generation generation_  = 0;
    SegmentRange doc_range_;
    float      avg_dl_      = 0.0f;

    struct TermCache {
        std::string text;
        uint32_t    doc_frequency    = 0;
        uint64_t    postings_offset  = 0;
        uint32_t    postings_length  = 0;
        uint32_t    block_count      = 0;
        uint64_t    block_meta_offset = 0;
    };
    std::vector<TermCache> term_dict;  // sorted by text

    struct DocCache {
        DocId       doc_id     = kInvalidDocId;
        uint32_t    term_count = 0;
        std::string url;
        std::string title;
        std::string excerpt;
    };
    std::vector<DocCache> doc_table;

    std::filesystem::path postings_path() const { return dir / "postings.bin"; }
    std::filesystem::path blocks_path()   const { return dir / "blocks.bin";   }
};

SegmentReader::SegmentReader(std::filesystem::path segment_dir)
    : impl_(new Impl{std::move(segment_dir)}) {

    // Validate header
    std::ifstream hf(impl_->dir / "segment_header.bin", std::ios::binary);
    if (!hf) return;

    uint8_t magic[4] = {};
    hf.read(reinterpret_cast<char*>(magic), 4);
    if (std::memcmp(magic, kSegMagic, 4) != 0) return;

    uint32_t ver = 0;
    hf.read(reinterpret_cast<char*>(&ver), 4);
    if (ver != kSegFmtVersion) return;

    hf.read(reinterpret_cast<char*>(&impl_->generation_),  4);
    hf.read(reinterpret_cast<char*>(&impl_->doc_count_),   4);
    hf.read(reinterpret_cast<char*>(&impl_->term_count_),  4);

    // Load term dictionary
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
        tf.read(reinterpret_cast<char*>(&e.doc_frequency),    4);
        tf.read(reinterpret_cast<char*>(&e.postings_offset),  8);
        tf.read(reinterpret_cast<char*>(&e.postings_length),  4);
        tf.read(reinterpret_cast<char*>(&e.block_count),      4);
        tf.read(reinterpret_cast<char*>(&e.block_meta_offset), 8);
    }
    if (!tf) return;

    // Load document table
    std::ifstream df(impl_->dir / "docs.bin", std::ios::binary);
    if (!df) return;
    uint32_t dcount = 0;
    df.read(reinterpret_cast<char*>(&dcount), 4);
    impl_->doc_table.resize(dcount);
    uint64_t total_terms = 0;
    for (auto& d : impl_->doc_table) {
        df.read(reinterpret_cast<char*>(&d.doc_id),     4);
        df.read(reinterpret_cast<char*>(&d.term_count), 4);
        total_terms += d.term_count;
        uint16_t ul = 0, tl = 0, el = 0;
        df.read(reinterpret_cast<char*>(&ul), 2);
        d.url.resize(ul); df.read(d.url.data(), ul);
        df.read(reinterpret_cast<char*>(&tl), 2);
        d.title.resize(tl); df.read(d.title.data(), tl);
        df.read(reinterpret_cast<char*>(&el), 2);
        d.excerpt.resize(el); df.read(d.excerpt.data(), el);
    }
    if (!df) return;

    if (!impl_->doc_table.empty()) {
        impl_->doc_range_.first_doc = impl_->doc_table.front().doc_id;
        impl_->doc_range_.last_doc  = impl_->doc_table.back().doc_id;
        impl_->avg_dl_ = static_cast<float>(total_terms) /
                         static_cast<float>(impl_->doc_table.size());
    }

    impl_->valid = true;
}

SegmentReader::~SegmentReader()                           = default;
SegmentReader::SegmentReader(SegmentReader&&) noexcept    = default;
SegmentReader& SegmentReader::operator=(SegmentReader&&) noexcept = default;

bool SegmentReader::is_valid() const noexcept { return impl_->valid; }

std::optional<TermEntry> SegmentReader::find_term(const std::string& term) const {
    // Binary search over sorted term_dict.
    auto it = std::lower_bound(impl_->term_dict.begin(), impl_->term_dict.end(),
                                term,
                                [](const Impl::TermCache& e, const std::string& t) {
                                    return e.text < t;
                                });
    if (it == impl_->term_dict.end() || it->text != term) return std::nullopt;

    TermEntry e;
    e.term             = it->text;
    e.doc_frequency    = it->doc_frequency;
    e.postings_offset  = it->postings_offset;
    e.postings_length  = it->postings_length;
    e.block_count      = it->block_count;
    e.block_meta_offset = it->block_meta_offset;
    return e;
}

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

std::optional<DocumentMeta> SegmentReader::get_doc_meta(DocId doc_id) const {
    for (const auto& d : impl_->doc_table) {
        if (d.doc_id == doc_id) {
            DocumentMeta m;
            m.doc_id     = d.doc_id;
            m.term_count = d.term_count;
            return m;
        }
    }
    return std::nullopt;
}

bool SegmentReader::get_doc_display(DocId doc_id,
                                     std::string& out_url,
                                     std::string& out_title,
                                     std::string& out_excerpt) const {
    for (const auto& d : impl_->doc_table) {
        if (d.doc_id == doc_id) {
            out_url     = d.url;
            out_title   = d.title;
            out_excerpt = d.excerpt;
            return true;
        }
    }
    return false;
}

uint32_t   SegmentReader::doc_count()      const noexcept { return impl_->doc_count_; }
float      SegmentReader::avg_doc_length() const noexcept { return impl_->avg_dl_; }
Generation SegmentReader::generation()     const noexcept { return impl_->generation_; }
SegmentRange SegmentReader::doc_range()    const noexcept { return impl_->doc_range_; }

} // namespace ali
