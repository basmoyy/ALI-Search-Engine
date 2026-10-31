// ali_engine.cpp — C API implementation bridging ali.h to the C++ core.
//
// Each AliEngine wraps the core objects needed for search and indexing.
// All C-API functions validate their inputs and translate C++ exceptions
// to AliStatus codes so callers never see C++ exceptions across the ABI.

#include "ali.h"

#include "ali/storage/manifest.h"
#include "ali/storage/metadata_db.h"
#include "ali/index/index_reader.h"
#include "ali/index/index_writer.h"
#include "ali/query/tokeniser.h"
#include "ali/query/query_planner.h"
#include "ali/scanner/ali_scanner.h"
#include "ali/locality/locality_memory.h"
#include "ali/crawler/crawler.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// ─── Internal structs ─────────────────────────────────────────────────────────

struct AliResults_ {
    ali::SearchResponse response;
};

struct AliEngine_ {
    std::string index_root;
    std::string db_path;

    std::unique_ptr<ali::Manifest>       manifest;
    std::unique_ptr<ali::MetadataDb>     db;
    std::unique_ptr<ali::IndexWriter>    writer;
    std::unique_ptr<ali::IndexReader>    reader;
    std::unique_ptr<ali::LocalityMemory> locality;
    std::unique_ptr<ali::AliScanner>     scanner;
    std::unique_ptr<ali::Tokeniser>      tokeniser;
    std::unique_ptr<ali::QueryPlanner>   planner;

    std::atomic<bool> cancel_flag{false};
};

// ─── Helpers ──────────────────────────────────────────────────────────────────

static AliStatus translate_exception() noexcept {
    try { throw; }
    catch (const std::invalid_argument&) { return ALI_ERR_INVALID_ARG; }
    catch (const std::ios_base::failure&) { return ALI_ERR_IO; }
    catch (...) { return ALI_ERR_INTERNAL; }
}

// ─── Engine lifecycle ─────────────────────────────────────────────────────────

AliStatus ali_engine_open(const char* index_root,
                           const char* db_path,
                           AliEngine** out_engine) {
    if (!index_root || !db_path || !out_engine) return ALI_ERR_INVALID_ARG;
    *out_engine = nullptr;

    try {
        auto engine = std::make_unique<AliEngine_>();
        engine->index_root = index_root;
        engine->db_path    = db_path;

        // Manifest
        engine->manifest = std::make_unique<ali::Manifest>(index_root);
        if (!engine->manifest->open()) return ALI_ERR_CORRUPT;

        // Metadata DB
        engine->db = std::make_unique<ali::MetadataDb>(db_path);
        if (!engine->db->open()) return ALI_ERR_IO;

        // Index writer
        ali::IndexWriterConfig wcfg;
        wcfg.index_root = index_root;
        engine->writer = std::make_unique<ali::IndexWriter>(
            wcfg, *engine->manifest, *engine->db);
        if (!engine->writer->open()) return ALI_ERR_IO;

        // Index reader
        engine->reader = std::make_unique<ali::IndexReader>(*engine->manifest);
        if (!engine->reader->open()) return ALI_ERR_IO;

        // Locality Memory
        engine->locality = std::make_unique<ali::LocalityMemory>();

        // Scanner
        engine->scanner = std::make_unique<ali::AliScanner>(
            *engine->reader, *engine->locality);

        // Query processing
        engine->tokeniser = std::make_unique<ali::Tokeniser>();
        engine->planner   = std::make_unique<ali::QueryPlanner>(*engine->reader);

        *out_engine = engine.release();
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

void ali_engine_close(AliEngine* engine) {
    if (!engine) return;
    try {
        engine->cancel_flag.store(true);
        if (engine->writer) engine->writer->close();
        if (engine->db)     engine->db->close();
    } catch (...) {
        // Swallow; we are shutting down.
    }
    delete engine;
}

// ─── Search ───────────────────────────────────────────────────────────────────

AliStatus ali_search(AliEngine*   engine,
                     const char*  query_text,
                     uint32_t     top_k,
                     uint32_t     timeout_ms,
                     AliResults** out_results) {
    if (!engine || !query_text || !out_results) return ALI_ERR_INVALID_ARG;
    if (top_k == 0 || top_k > 100) return ALI_ERR_INVALID_ARG;

    *out_results = nullptr;
    engine->cancel_flag.store(false);

    try {
        engine->reader->maybe_refresh();

        ali::Query q;
        q.raw_text  = query_text;
        q.tokens    = engine->tokeniser->tokenise_query(query_text);
        q.top_k     = top_k;
        q.timeout_ms = (timeout_ms > 0) ? timeout_ms : 200;

        ali::QueryPlan plan = engine->planner->plan(q);
        ali::SearchResponse resp =
            engine->scanner->execute(plan, engine->cancel_flag);

        auto* results = new AliResults_();
        results->response = std::move(resp);
        *out_results = results;

        if (engine->cancel_flag.load()) return ALI_ERR_CANCELLED;
        if (results->response.timed_out) return ALI_ERR_TIMEOUT;
        return ALI_OK;

    } catch (...) {
        return translate_exception();
    }
}

void ali_engine_cancel(AliEngine* engine) {
    if (engine) engine->cancel_flag.store(true);
}

// ─── Results ──────────────────────────────────────────────────────────────────

uint32_t ali_results_count(const AliResults* results) {
    if (!results) return 0;
    return static_cast<uint32_t>(results->response.results.size());
}

AliStatus ali_results_get(const AliResults* results,
                           uint32_t          index,
                           const char**      out_url,
                           const char**      out_title,
                           const char**      out_excerpt,
                           float*            out_score) {
    if (!results || index >= results->response.results.size())
        return ALI_ERR_INVALID_ARG;

    const auto& r = results->response.results[index];
    if (out_url)     *out_url     = r.url.c_str();
    if (out_title)   *out_title   = r.title.c_str();
    if (out_excerpt) *out_excerpt = r.excerpt.c_str();
    if (out_score)   *out_score   = r.score;
    return ALI_OK;
}

AliStatus ali_results_metrics(const AliResults* results,
                               uint32_t*         out_candidates,
                               uint32_t*         out_blocks_read,
                               uint64_t*         out_bytes_read,
                               uint32_t*         out_elapsed_ms,
                               int*              out_timed_out) {
    if (!results) return ALI_ERR_INVALID_ARG;
    const auto& r = results->response;
    if (out_candidates)  *out_candidates  = r.total_candidates_evaluated;
    if (out_blocks_read) *out_blocks_read = r.blocks_read;
    if (out_bytes_read)  *out_bytes_read  = r.bytes_read;
    if (out_elapsed_ms)  *out_elapsed_ms  = r.elapsed_ms;
    if (out_timed_out)   *out_timed_out   = r.timed_out ? 1 : 0;
    return ALI_OK;
}

void ali_results_free(AliResults* results) {
    delete results;
}

// ─── Index management ─────────────────────────────────────────────────────────

AliStatus ali_index_document(AliEngine*  engine,
                              const char* url,
                              const char* title,
                              const char* body_text) {
    if (!engine || !url || !title || !body_text) return ALI_ERR_INVALID_ARG;

    try {
        auto tokens    = engine->tokeniser->tokenise_document(body_text);
        auto term_freqs = ali::Tokeniser::compute_term_freqs(tokens);

        ali::InputDocument doc;
        doc.url       = url;
        doc.title     = title;
        doc.body_text = body_text;

        auto result = engine->writer->add_document(doc);
        if (!result.accepted) return ALI_ERR_INTERNAL;
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

AliStatus ali_index_flush(AliEngine* engine) {
    if (!engine) return ALI_ERR_INVALID_ARG;
    try {
        if (!engine->writer->flush()) return ALI_ERR_IO;
        engine->reader->maybe_refresh();
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

// ─── Crawler ─────────────────────────────────────────────────────────────────

AliStatus ali_crawler_start(AliEngine* engine) {
    (void)engine; // Crawler initialisation deferred; stub for now.
    return ALI_OK;
}

AliStatus ali_crawler_pause(AliEngine* engine) {
    (void)engine;
    return ALI_OK;
}

AliStatus ali_crawler_stop(AliEngine* engine) {
    (void)engine;
    return ALI_OK;
}

AliStatus ali_crawler_enqueue_url(AliEngine* engine, const char* url) {
    if (!engine || !url) return ALI_ERR_INVALID_ARG;
    return ALI_OK;
}

// ─── Statistics ───────────────────────────────────────────────────────────────

AliStatus ali_stats_doc_count(AliEngine* engine, uint64_t* out_count) {
    if (!engine || !out_count) return ALI_ERR_INVALID_ARG;
    // Use the reader's view (reflects all published segments) rather than
    // the session-scoped writer counter.
    *out_count = static_cast<uint64_t>(engine->reader->total_doc_count());
    return ALI_OK;
}

AliStatus ali_stats_segment_count(AliEngine* engine, uint32_t* out_count) {
    if (!engine || !out_count) return ALI_ERR_INVALID_ARG;
    *out_count = static_cast<uint32_t>(engine->reader->segment_count());
    return ALI_OK;
}

AliStatus ali_stats_index_bytes(AliEngine* engine, uint64_t* out_bytes) {
    if (!engine || !out_bytes) return ALI_ERR_INVALID_ARG;
    const auto snap = engine->manifest->snapshot();
    *out_bytes = snap.total_size_bytes();
    return ALI_OK;
}
