// ali_engine.cpp — C API implementation bridging ali.h to the C++ core.

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
#include "ali/crawler/resource_controller.h"
#include "ali/discovery/source_registry.h"
#include "ali/discovery/topic_nav_index.h"
#include "ali/discovery/grammar_registry.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// ─── MutableResourceController ────────────────────────────────────────────────

class MutableResourceController : public ali::ResourceController {
public:
    MutableResourceController() = default;

    void update(const ali::DeviceConditions& cond) {
        std::lock_guard lock(mtx_);
        conditions_ = cond;
    }

    [[nodiscard]] ali::DeviceConditions conditions() const override {
        std::lock_guard lock(mtx_);
        return conditions_;
    }

private:
    mutable std::mutex    mtx_;
    ali::DeviceConditions conditions_;
};

// ─── Internal structs ─────────────────────────────────────────────────────────

struct AliResults_ {
    ali::SearchResponse response;
};

struct AliEngine_ {
    std::string index_root;
    std::string db_path;

    std::unique_ptr<ali::Manifest>               manifest;
    std::unique_ptr<ali::MetadataDb>             db;
    std::unique_ptr<ali::IndexWriter>            writer;
    std::unique_ptr<ali::IndexReader>            reader;
    std::unique_ptr<ali::LocalityMemory>         locality;
    std::unique_ptr<ali::AliScanner>             scanner;
    std::unique_ptr<ali::Tokeniser>              tokeniser;
    std::unique_ptr<ali::QueryPlanner>           planner;
    std::unique_ptr<MutableResourceController>   resource_ctrl;
    std::unique_ptr<ali::Crawler>                crawler;

    // V1 discovery layer
    std::unique_ptr<ali::SourceRegistry>         source_registry;
    std::unique_ptr<ali::TopicNavIndex>          topic_nav;
    ali::GrammarRegistry                         grammar_registry;

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

        engine->manifest = std::make_unique<ali::Manifest>(index_root);
        if (!engine->manifest->open()) return ALI_ERR_CORRUPT;

        engine->db = std::make_unique<ali::MetadataDb>(db_path);
        if (!engine->db->open()) return ALI_ERR_IO;

        ali::IndexWriterConfig wcfg;
        wcfg.index_root = index_root;
        engine->writer = std::make_unique<ali::IndexWriter>(
            wcfg, *engine->manifest, *engine->db);
        if (!engine->writer->open()) return ALI_ERR_IO;

        engine->reader = std::make_unique<ali::IndexReader>(*engine->manifest);
        if (!engine->reader->open()) return ALI_ERR_IO;

        engine->locality = std::make_unique<ali::LocalityMemory>();

        engine->scanner = std::make_unique<ali::AliScanner>(
            *engine->reader, *engine->locality);

        engine->tokeniser = std::make_unique<ali::Tokeniser>();
        engine->planner   = std::make_unique<ali::QueryPlanner>(*engine->reader);

        // Resource controller and crawler
        engine->resource_ctrl = std::make_unique<MutableResourceController>();
        ali::CrawlerConfig ccfg;
        engine->crawler = std::make_unique<ali::Crawler>(
            ccfg, *engine->db, *engine->writer, *engine->resource_ctrl);

        // V1 discovery layer
        engine->source_registry = std::make_unique<ali::SourceRegistry>(*engine->db);
        (void)engine->source_registry->ensure_schema();

        engine->topic_nav = std::make_unique<ali::TopicNavIndex>(*engine->db);
        (void)engine->topic_nav->ensure_schema();

        engine->grammar_registry.load_defaults();

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
        if (engine->crawler) engine->crawler->stop();
        if (engine->writer)  engine->writer->close();
        if (engine->db)      engine->db->close();
    } catch (...) {}
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
        q.raw_text   = query_text;
        q.tokens     = engine->tokeniser->tokenise_query(query_text);
        q.top_k      = top_k;
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
    if (!engine) return ALI_ERR_INVALID_ARG;
    try {
        engine->crawler->start();
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

AliStatus ali_crawler_pause(AliEngine* engine) {
    if (!engine) return ALI_ERR_INVALID_ARG;
    try {
        engine->crawler->pause();
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

AliStatus ali_crawler_stop(AliEngine* engine) {
    if (!engine) return ALI_ERR_INVALID_ARG;
    try {
        engine->crawler->stop();
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

AliStatus ali_crawler_enqueue_url(AliEngine* engine, const char* url) {
    if (!engine || !url) return ALI_ERR_INVALID_ARG;
    try {
        engine->crawler->enqueue_urls({url});
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

// ─── Device conditions ────────────────────────────────────────────────────────

AliStatus ali_update_device_conditions(AliEngine* engine,
                                        int        network_available,
                                        int        network_metered,
                                        int        charging,
                                        int        battery_level_pct,
                                        int        thermal_throttling,
                                        int        memory_pressure,
                                        uint64_t   free_storage_bytes) {
    if (!engine) return ALI_ERR_INVALID_ARG;
    try {
        ali::DeviceConditions cond;
        cond.network_available  = (network_available  != 0);
        cond.network_metered    = (network_metered    != 0);
        cond.charging           = (charging           != 0);
        cond.battery_level_pct  = static_cast<uint8_t>(
            std::max(0, std::min(100, battery_level_pct)));
        cond.thermal_throttling = (thermal_throttling != 0);
        cond.memory_pressure    = (memory_pressure    != 0);
        cond.free_storage_bytes = free_storage_bytes;
        engine->resource_ctrl->update(cond);
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

// ─── Statistics ───────────────────────────────────────────────────────────────

AliStatus ali_stats_doc_count(AliEngine* engine, uint64_t* out_count) {
    if (!engine || !out_count) return ALI_ERR_INVALID_ARG;
    *out_count = static_cast<uint64_t>(engine->reader->total_doc_count())
               + engine->writer->temp_index().doc_count();
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
    *out_bytes = snap.total_size_bytes()
               + engine->writer->temp_index().used_bytes();
    return ALI_OK;
}

// ─── Discovery stats ──────────────────────────────────────────────────────────

AliStatus ali_stats_source_count(AliEngine* engine, uint32_t* out_count) {
    if (!engine || !out_count) return ALI_ERR_INVALID_ARG;
    *out_count = engine->source_registry ? engine->source_registry->total_count() : 0;
    return ALI_OK;
}

AliStatus ali_stats_nav_edge_count(AliEngine* engine, uint32_t* out_count) {
    if (!engine || !out_count) return ALI_ERR_INVALID_ARG;
    *out_count = engine->topic_nav ? engine->topic_nav->edge_count() : 0;
    return ALI_OK;
}

AliStatus ali_stats_grammar_count(AliEngine* engine, uint32_t* out_count) {
    if (!engine || !out_count) return ALI_ERR_INVALID_ARG;
    *out_count = engine->grammar_registry.size();
    return ALI_OK;
}

// ─── Grammar-guided discovery ─────────────────────────────────────────────────

struct AliGrammarResult_ {
    std::vector<ali::GrammarResult> items;
};

AliStatus ali_grammar_construct(AliEngine*         engine,
                                 const char*        query,
                                 uint32_t           max_results,
                                 AliGrammarResult** out_results,
                                 uint32_t*          out_count) {
    if (!engine || !query || !out_results || !out_count) return ALI_ERR_INVALID_ARG;
    if (max_results == 0 || max_results > 20) max_results = 5;
    try {
        auto items = engine->grammar_registry.construct(
            std::string(query), max_results);
        auto* r = new AliGrammarResult_();
        r->items = std::move(items);
        *out_results = r;
        *out_count   = static_cast<uint32_t>(r->items.size());
        return ALI_OK;
    } catch (...) {
        return translate_exception();
    }
}

AliStatus ali_grammar_result_get(const AliGrammarResult* results,
                                  uint32_t                index,
                                  const char**            out_grammar_name,
                                  const char**            out_url,
                                  const char**            out_domain,
                                  float*                  out_confidence) {
    if (!results || index >= results->items.size()) return ALI_ERR_INVALID_ARG;
    const auto& r = results->items[index];
    if (out_grammar_name) *out_grammar_name = r.grammar_name.c_str();
    if (out_url)          *out_url          = r.url.c_str();
    if (out_domain)       *out_domain       = r.domain.c_str();
    if (out_confidence)   *out_confidence   = r.confidence;
    return ALI_OK;
}

void ali_grammar_results_free(AliGrammarResult* results) {
    delete results;
}
