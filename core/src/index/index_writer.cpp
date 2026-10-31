// index_writer.cpp — Coordinates segment writers and manifest publication.

#include "ali/index/index_writer.h"
#include "ali/storage/segment_writer.h"
#include "ali/query/tokeniser.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace ali {

static std::string new_segment_id() {
    // Simple random hex ID for stable, unique segment identification.
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(16) << rng()
       << std::setw(16) << rng();
    return ss.str();
}

struct IndexWriter::Impl {
    IndexWriterConfig              cfg;
    Manifest&                      manifest;
    MetadataDb&                    db;
    Tokeniser                      tokeniser;

    std::unique_ptr<SegmentWriter> active_writer;
    std::string                    active_segment_id;
    Generation                     next_generation = 0;
    uint64_t                       total_indexed   = 0;

    explicit Impl(IndexWriterConfig c, Manifest& m, MetadataDb& d)
        : cfg(std::move(c)), manifest(m), db(d), tokeniser() {}

    bool ensure_active_writer() {
        if (active_writer && !active_writer->is_full()) return true;

        if (active_writer && active_writer->doc_count() > 0) {
            if (!flush_active()) return false;
        }

        active_segment_id = new_segment_id();
        SegmentWriterConfig wcfg;
        wcfg.max_docs          = cfg.docs_per_segment;
        wcfg.max_segment_bytes = cfg.max_segment_bytes;
        wcfg.include_full_text = cfg.include_full_text;

        auto tmp_dir = std::filesystem::path(cfg.index_root)
            / ("seg_" + active_segment_id + ".tmp");
        active_writer = std::make_unique<SegmentWriter>(tmp_dir, wcfg);
        return true;
    }

    bool flush_active() {
        if (!active_writer || active_writer->doc_count() == 0) return true;

        const auto dest_dir = std::filesystem::path(cfg.index_root)
            / ("seg_" + active_segment_id);

        // Save count before finish() resets the writer state.
        const uint32_t doc_count_in_segment = active_writer->doc_count();

        if (!active_writer->finish(dest_dir, next_generation)) return false;

        SegmentEntry entry;
        entry.generation      = next_generation++;
        entry.segment_id      = active_segment_id;
        entry.path            = dest_dir;
        entry.size_bytes      = active_writer->estimated_bytes();
        entry.created_at_unix = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // Populate doc_range so manifest.total_doc_count() is correct.
        if (doc_count_in_segment > 0) {
            entry.doc_range.first_doc = 0;
            entry.doc_range.last_doc  = doc_count_in_segment - 1;
        }
        // Populate size from actual files on disk.
        std::error_code size_ec;
        uint64_t actual_bytes = 0;
        for (auto const& f : std::filesystem::directory_iterator(dest_dir, size_ec)) {
            if (!size_ec) actual_bytes += std::filesystem::file_size(f.path(), size_ec);
        }
        if (!size_ec && actual_bytes > 0) entry.size_bytes = actual_bytes;

        if (!manifest.publish_segment(entry)) return false;

        active_writer.reset();
        active_segment_id.clear();
        return true;
    }
};

IndexWriter::IndexWriter(IndexWriterConfig cfg, Manifest& manifest, MetadataDb& db)
    : impl_(new Impl(std::move(cfg), manifest, db)) {}

IndexWriter::~IndexWriter() { close(); }

bool IndexWriter::open() {
    const auto snap = impl_->manifest.snapshot();
    impl_->next_generation = static_cast<Generation>(snap.manifest_version + 1);
    return true;
}

IndexResult IndexWriter::add_document(const InputDocument& doc) {
    IndexResult result;
    if (doc.url.empty()) {
        result.error_message = "empty URL";
        return result;
    }

    if (!impl_->ensure_active_writer()) {
        result.error_message = "failed to initialise segment writer";
        return result;
    }

    auto tokens     = impl_->tokeniser.tokenise_document(doc.body_text);
    auto term_freqs = Tokeniser::compute_term_freqs(tokens);

    const DocId doc_id = impl_->active_writer->add_document(
        doc.url, doc.title, doc.body_text, term_freqs);

    if (doc_id == kInvalidDocId) {
        result.error_message = "segment full or document rejected";
        return result;
    }

    // Register URL in metadata DB.
    UrlRecord rec;
    rec.url           = doc.url;
    rec.canonical_url = doc.url;
    rec.doc_id        = doc_id;
    rec.status        = CrawlStatus::Indexed;
    rec.content_hash  = doc.content_hash;
    (void)impl_->db.upsert_url(rec);

    ++impl_->total_indexed;
    result.accepted = true;
    result.doc_id   = doc_id;
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

} // namespace ali
