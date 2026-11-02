// index_writer.cpp — Coordinates segment writers and manifest publication.

#include "ali/index/index_writer.h"
#include "ali/index/temp_index.h"
#include "ali/storage/segment_writer.h"
#include "ali/query/tokeniser.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace ali {

static std::string new_segment_id() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(16) << rng()
       << std::setw(16) << rng();
    return ss.str();
}

struct IndexWriter::Impl {
    IndexWriterConfig cfg;
    Manifest&         manifest;
    MetadataDb&       db;
    Tokeniser         tokeniser;

    TempIndex         temp_index;
    uint32_t          next_doc_id_base = 0;
    Generation        next_generation  = 0;
    uint64_t          total_indexed    = 0;

    explicit Impl(IndexWriterConfig c, Manifest& m, MetadataDb& d)
        : cfg(std::move(c)), manifest(m), db(d), tokeniser() {}

    bool flush_active() {
        if (temp_index.doc_count() == 0) return true;

        const std::string seg_id = new_segment_id();
        const auto dest_dir = std::filesystem::path(cfg.index_root)
            / ("seg_" + seg_id);
        const auto tmp_dir  = std::filesystem::path(cfg.index_root)
            / ("seg_" + seg_id + ".tmp");

        SegmentWriterConfig wcfg;
        wcfg.max_docs          = cfg.docs_per_segment;
        wcfg.max_segment_bytes = cfg.max_segment_bytes;
        wcfg.include_full_text = cfg.include_full_text;

        SegmentWriter seg_writer(tmp_dir, wcfg);

        // Write all docs from TempIndex into segment
        const auto& tm = temp_index.term_map();
        const auto& docs = temp_index.docs();

        for (const auto& doc : docs) {
            // Collect term_freqs for this doc from term_map
            std::vector<std::pair<std::string, uint32_t>> term_freqs;
            for (const auto& [term, postings] : tm) {
                for (const auto& tp : postings) {
                    if (tp.doc_id == doc.doc_id) {
                        term_freqs.push_back({term, tp.term_freq});
                        break;
                    }
                }
            }

            seg_writer.add_document(doc.url, doc.title, doc.excerpt, term_freqs);
        }

        const uint32_t base = next_doc_id_base;
        if (!seg_writer.finish(dest_dir, next_generation, base)) return false;

        // Compute doc range
        const uint32_t doc_count = static_cast<uint32_t>(docs.size());
        const DocId first_doc = global_doc_id(base, 0);
        const DocId last_doc  = (doc_count > 0)
            ? global_doc_id(base, doc_count - 1) : first_doc;

        SegmentEntry entry;
        entry.generation           = next_generation++;
        entry.segment_id           = seg_id;
        entry.path                 = dest_dir;
        entry.created_at_unix      = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        entry.doc_range.first_doc  = first_doc;
        entry.doc_range.last_doc   = last_doc;

        // Measure actual file sizes
        std::error_code size_ec;
        uint64_t actual_bytes = 0;
        for (auto const& f : std::filesystem::directory_iterator(dest_dir, size_ec)) {
            if (!size_ec) actual_bytes += std::filesystem::file_size(f.path(), size_ec);
        }
        entry.size_bytes = actual_bytes;

        if (!manifest.publish_segment(entry)) return false;

        // Advance the doc_id_base by the number of docs written
        next_doc_id_base += doc_count;

        temp_index.clear();
        return true;
    }
};

IndexWriter::IndexWriter(IndexWriterConfig cfg, Manifest& manifest, MetadataDb& db)
    : impl_(new Impl(std::move(cfg), manifest, db)) {}

IndexWriter::~IndexWriter() { close(); }

bool IndexWriter::open() {
    const auto snap = impl_->manifest.snapshot();
    impl_->next_generation = static_cast<Generation>(snap.manifest_version + 1);

    // Restore next_doc_id_base from existing segments
    for (const auto& e : snap.segments) {
        if (!e.is_tombstone && e.doc_range.count() > 0) {
            const uint32_t seg_base = doc_id_base(e.doc_range.last_doc);
            const uint32_t seg_local = local_doc_id(e.doc_range.last_doc);
            const uint32_t next = seg_base + seg_local + 1;
            if (next > impl_->next_doc_id_base) {
                impl_->next_doc_id_base = next;
            }
        }
    }
    return true;
}

IndexResult IndexWriter::add_document(const InputDocument& doc) {
    IndexResult result;
    if (doc.url.empty()) {
        result.error_message = "empty URL";
        return result;
    }

    auto tokens     = impl_->tokeniser.tokenise_document(doc.body_text);
    auto term_freqs = Tokeniser::compute_term_freqs(tokens);

    // Flush if TempIndex is full, then retry
    if (impl_->temp_index.is_full()) {
        if (!impl_->flush_active()) {
            result.error_message = "failed to flush segment";
            return result;
        }
    }

    const DocId global_id = global_doc_id(
        impl_->next_doc_id_base,
        impl_->temp_index.doc_count());

    const std::string excerpt = doc.body_text.substr(0, 256);
    if (!impl_->temp_index.add_document(global_id, doc.url, doc.title,
                                         excerpt, term_freqs)) {
        result.error_message = "TempIndex rejected document";
        return result;
    }

    // Auto-flush when threshold reached
    if (impl_->temp_index.should_flush()) {
        (void)impl_->flush_active();
    }

    // Register URL in metadata DB
    UrlRecord rec;
    rec.url           = doc.url;
    rec.canonical_url = doc.url;
    rec.doc_id        = global_id;
    rec.status        = CrawlStatus::Indexed;
    rec.content_hash  = doc.content_hash;
    (void)impl_->db.upsert_url(rec);

    ++impl_->total_indexed;
    result.accepted = true;
    result.doc_id   = global_id;
    return result;
}

bool IndexWriter::flush() {
    return impl_->flush_active();
}

void IndexWriter::close() {
    try { impl_->flush_active(); } catch (...) {}
}

uint64_t IndexWriter::total_docs_indexed() const noexcept {
    return impl_->total_indexed;
}

TempIndex& IndexWriter::temp_index() noexcept {
    return impl_->temp_index;
}

const TempIndex& IndexWriter::temp_index() const noexcept {
    return impl_->temp_index;
}

} // namespace ali
