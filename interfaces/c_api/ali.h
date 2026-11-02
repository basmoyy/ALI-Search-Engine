/*
 * ali.h — Stable C-compatible public API for the ALI search engine.
 *
 * This is the only header that platform shells (Kotlin/JNI, Swift, Qt)
 * need to include. The C++ internals are hidden behind opaque handles.
 *
 * Versioning
 * ----------
 * ALI_API_VERSION_MAJOR and MINOR follow semantic versioning.
 * Breaking changes increment MAJOR. Additions increment MINOR.
 *
 * Thread safety
 * -------------
 * ali_engine_* functions are NOT thread-safe unless noted.
 * ali_search() is NOT re-entrant on the same engine handle.
 * ali_engine_cancel() is safe to call from any thread.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ─────────────────────────────────────────────────────────────── */

#define ALI_API_VERSION_MAJOR 0
#define ALI_API_VERSION_MINOR 1

/* ── Opaque handles ──────────────────────────────────────────────────────── */

typedef struct AliEngine_   AliEngine;
typedef struct AliResults_  AliResults;

/* ── Status codes ────────────────────────────────────────────────────────── */

typedef enum AliStatus {
    ALI_OK              =  0,
    ALI_ERR_INVALID_ARG = -1,
    ALI_ERR_IO          = -2,
    ALI_ERR_CORRUPT     = -3,
    ALI_ERR_NO_SPACE    = -4,
    ALI_ERR_TIMEOUT     = -5,
    ALI_ERR_CANCELLED   = -6,
    ALI_ERR_INTERNAL    = -99,
} AliStatus;

/* ── Engine lifecycle ────────────────────────────────────────────────────── */

/**
 * Create and open an ALI engine.
 *
 * @param index_root  Absolute path to the index directory. Created if absent.
 * @param db_path     Absolute path to the SQLite metadata database.
 * @param out_engine  Receives the engine handle on success.
 * @return ALI_OK on success; error code otherwise.
 */
AliStatus ali_engine_open(const char*  index_root,
                           const char*  db_path,
                           AliEngine**  out_engine);

/**
 * Close the engine and release all resources.
 * engine may be NULL (no-op).
 */
void ali_engine_close(AliEngine* engine);

/* ── Search ──────────────────────────────────────────────────────────────── */

/**
 * Execute a synchronous search query.
 *
 * @param engine      Open engine handle.
 * @param query_text  UTF-8 query string (null-terminated).
 * @param top_k       Maximum number of results to return (1–100).
 * @param timeout_ms  Hard time limit in milliseconds (0 = use engine default).
 * @param out_results Receives a result handle that must be freed with
 *                    ali_results_free().
 * @return ALI_OK on success; ALI_ERR_TIMEOUT if the query timed out with
 *         partial results available (out_results is still valid in this case).
 */
AliStatus ali_search(AliEngine*   engine,
                     const char*  query_text,
                     uint32_t     top_k,
                     uint32_t     timeout_ms,
                     AliResults** out_results);

/**
 * Signal the current query to cancel. Safe to call from any thread.
 * ali_search() returns ALI_ERR_CANCELLED after cancellation.
 */
void ali_engine_cancel(AliEngine* engine);

/* ── Results ─────────────────────────────────────────────────────────────── */

/** Number of results in a result set. */
uint32_t ali_results_count(const AliResults* results);

/**
 * Access one result by zero-based index.
 * Pointers are valid until ali_results_free() is called.
 */
AliStatus ali_results_get(const AliResults* results,
                           uint32_t          index,
                           const char**      out_url,
                           const char**      out_title,
                           const char**      out_excerpt,
                           float*            out_score);

/** Metrics from the last search (may be NULL to ignore any field). */
AliStatus ali_results_metrics(const AliResults* results,
                               uint32_t*         out_candidates_evaluated,
                               uint32_t*         out_blocks_read,
                               uint64_t*         out_bytes_read,
                               uint32_t*         out_elapsed_ms,
                               int*              out_timed_out);

/** Free a result set. Safe to call with NULL. */
void ali_results_free(AliResults* results);

/* ── Index management ────────────────────────────────────────────────────── */

/**
 * Index a single document synchronously.
 * Intended for testing and small corpus imports; use the crawler for
 * large-scale indexing.
 */
AliStatus ali_index_document(AliEngine*  engine,
                              const char* url,
                              const char* title,
                              const char* body_text);

/** Flush any pending segment to disk and publish it. */
AliStatus ali_index_flush(AliEngine* engine);

/* ── Crawler control ─────────────────────────────────────────────────────── */

AliStatus ali_crawler_start(AliEngine* engine);
AliStatus ali_crawler_pause(AliEngine* engine);
AliStatus ali_crawler_stop(AliEngine* engine);
AliStatus ali_crawler_enqueue_url(AliEngine* engine, const char* url);

/* ── Device conditions bridge ────────────────────────────────────────────── */

AliStatus ali_update_device_conditions(AliEngine* engine,
                                        int        network_available,
                                        int        network_metered,
                                        int        charging,
                                        int        battery_level_pct,
                                        int        thermal_throttling,
                                        int        memory_pressure,
                                        uint64_t   free_storage_bytes);

/* ── Statistics ──────────────────────────────────────────────────────────── */

AliStatus ali_stats_doc_count(AliEngine* engine, uint64_t* out_count);
AliStatus ali_stats_segment_count(AliEngine* engine, uint32_t* out_count);
AliStatus ali_stats_index_bytes(AliEngine* engine, uint64_t* out_bytes);

/** Number of domain-level sources tracked in the Source Registry. */
AliStatus ali_stats_source_count(AliEngine* engine, uint32_t* out_count);

/** Number of navigation edges in the Navigation Graph. */
AliStatus ali_stats_nav_edge_count(AliEngine* engine, uint32_t* out_count);

/** Number of URL grammars loaded in the Grammar Registry. */
AliStatus ali_stats_grammar_count(AliEngine* engine, uint32_t* out_count);

#ifdef __cplusplus
} /* extern "C" */
#endif
