// url_frontier.cpp — Persistent URL frontier backed by MetadataDb.
// Frontier state lives in SQLite so crawl progress survives process death.

#include "ali/storage/metadata_db.h"
// Frontier logic is implemented through MetadataDb::pop_crawl_queue.
// This translation unit is reserved for priority scoring and host-level bucketing.
