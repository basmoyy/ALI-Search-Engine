add_library(ali_core STATIC
  # ── Storage ────────────────────────────────────────────────────────────────
  src/storage/segment_writer.cpp
  src/storage/segment_reader.cpp
  src/storage/manifest.cpp
  src/storage/metadata_db.cpp
  src/storage/content_store.cpp

  # ── Compression ────────────────────────────────────────────────────────────
  src/compression/vbyte.cpp
  src/compression/delta.cpp

  # ── Index ──────────────────────────────────────────────────────────────────
  src/index/document.cpp
  src/index/posting_list.cpp
  src/index/term_dictionary.cpp
  src/index/index_writer.cpp
  src/index/index_reader.cpp

  # ── Query ──────────────────────────────────────────────────────────────────
  src/query/tokeniser.cpp
  src/query/query_parser.cpp
  src/query/query_planner.cpp

  # ── Scanner ────────────────────────────────────────────────────────────────
  src/scanner/ali_scanner.cpp
  src/scanner/block_cache.cpp

  # ── Ranking ────────────────────────────────────────────────────────────────
  src/ranking/bm25.cpp
  src/ranking/result_set.cpp

  # ── Locality Memory ────────────────────────────────────────────────────────
  src/locality/locality_memory.cpp

  # ── Crawler ────────────────────────────────────────────────────────────────
  src/crawler/url_frontier.cpp
  src/crawler/scheduler.cpp
  src/crawler/fetcher.cpp
  src/crawler/html_parser.cpp
  src/crawler/deduplicator.cpp
  src/crawler/segment_builder.cpp
  src/crawler/crawler.cpp
  src/crawler/resource_controller.cpp

  # ── Index (Phase 4 navigation + Phase 10 compaction + TempIndex) ──────────
  src/index/compaction.cpp
  src/index/temp_index.cpp

  # ── Discovery (SR + TNI/NG + Grammar Registry) ────────────────────────────
  src/discovery/source_registry.cpp
  src/discovery/topic_nav_index.cpp
  src/discovery/grammar_registry.cpp
)

target_include_directories(ali_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ali_core
  PUBLIC  SQLite3::SQLite3
          OpenSSL::SSL
          OpenSSL::Crypto
  PRIVATE ali_compile_options
)
