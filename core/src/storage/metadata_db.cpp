// metadata_db.cpp — SQLite metadata store (WAL mode).
//
// URL lifecycle states match CrawlStatus enum values exactly.
// See metadata_db.h for the full state table (ALI Architecture Brief v0.3 §5).

#include "ali/storage/metadata_db.h"

#include <sqlite3.h>

#include <cassert>
#include <stdexcept>

namespace ali {

// ── DDL ──────────────────────────────────────────────────────────────────────

static const char* kSchemaSQL = R"sql(
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS urls (
    url_id          INTEGER PRIMARY KEY,
    url             TEXT    NOT NULL UNIQUE,
    canonical_url   TEXT    NOT NULL,
    doc_id          INTEGER NOT NULL DEFAULT 18446744073709551615,
    status          INTEGER NOT NULL DEFAULT 0,
    last_fetched_at INTEGER NOT NULL DEFAULT 0,
    next_fetch_at   INTEGER NOT NULL DEFAULT 0,
    fetch_failures  INTEGER NOT NULL DEFAULT 0,
    content_hash    INTEGER NOT NULL DEFAULT 0,
    access_note     TEXT    NOT NULL DEFAULT '',
    crawl_depth     INTEGER NOT NULL DEFAULT 0
);

-- Index for the crawl scheduler: eligible URLs ordered by next_fetch_at.
CREATE INDEX IF NOT EXISTS idx_urls_next_fetch ON urls(next_fetch_at, status);

-- Index for doc_id lookups (tombstone / remove_doc).
CREATE INDEX IF NOT EXISTS idx_urls_doc_id ON urls(doc_id);

CREATE TABLE IF NOT EXISTS robots (
    host            TEXT    PRIMARY KEY,
    rules_text      TEXT    NOT NULL DEFAULT '',
    fetched_at      INTEGER NOT NULL DEFAULT 0,
    expires_at      INTEGER NOT NULL DEFAULT 0
);
)sql";

static const char* kEligibleStatusList = "0,1,7,8,9";

// ─────────────────────────────────────────────────────────────────────────────

struct MetadataDb::Impl {
    std::filesystem::path path;
    sqlite3* db = nullptr;

    bool exec(const char* sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    bool prepare(const char* sql, sqlite3_stmt** stmt) const {
        return sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) == SQLITE_OK;
    }

    static UrlRecord row_to_record(sqlite3_stmt* stmt) {
        UrlRecord rec;
        rec.url_id          = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        rec.url             = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.canonical_url   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.doc_id          = static_cast<DocId>(sqlite3_column_int64(stmt, 3));
        rec.status          = static_cast<CrawlStatus>(sqlite3_column_int(stmt, 4));
        rec.last_fetched_at = sqlite3_column_int64(stmt, 5);
        rec.next_fetch_at   = sqlite3_column_int64(stmt, 6);
        rec.fetch_failures  = static_cast<uint32_t>(sqlite3_column_int64(stmt, 7));
        rec.content_hash    = static_cast<uint32_t>(sqlite3_column_int64(stmt, 8));
        const char* note    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        rec.access_note     = note ? note : "";
        rec.crawl_depth     = static_cast<uint32_t>(sqlite3_column_int64(stmt, 10));
        return rec;
    }
};

MetadataDb::MetadataDb(std::filesystem::path db_path)
    : impl_(new Impl{std::move(db_path)}) {}

MetadataDb::~MetadataDb() { close(); }

bool MetadataDb::open() {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    int rc = sqlite3_open_v2(impl_->path.c_str(), &impl_->db, flags, nullptr);
    if (rc != SQLITE_OK) return false;
    if (!impl_->exec(kSchemaSQL)) return false;
    // Safe migration: add crawl_depth to existing DBs (ignore if already exists)
    impl_->exec("ALTER TABLE urls ADD COLUMN crawl_depth INTEGER NOT NULL DEFAULT 0");
    return true;
}

void MetadataDb::close() {
    if (impl_->db) {
        sqlite3_close_v2(impl_->db);
        impl_->db = nullptr;
    }
}

// ── URL / document records ────────────────────────────────────────────────────

bool MetadataDb::upsert_url(const UrlRecord& rec) {
    if (!impl_->db) return false;

    const char* sql =
        "INSERT INTO urls(url, canonical_url, doc_id, status, "
        "last_fetched_at, next_fetch_at, fetch_failures, content_hash, access_note, crawl_depth) "
        "VALUES(?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(url) DO UPDATE SET "
        "canonical_url=excluded.canonical_url, doc_id=excluded.doc_id, "
        "status=excluded.status, last_fetched_at=excluded.last_fetched_at, "
        "next_fetch_at=excluded.next_fetch_at, "
        "fetch_failures=excluded.fetch_failures, "
        "content_hash=excluded.content_hash, "
        "access_note=excluded.access_note, "
        "crawl_depth=excluded.crawl_depth";

    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return false;

    sqlite3_bind_text(stmt,  1, rec.url.c_str(),          -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,  2, rec.canonical_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(rec.doc_id));
    sqlite3_bind_int(stmt,   4, static_cast<int>(rec.status));
    sqlite3_bind_int64(stmt, 5, rec.last_fetched_at);
    sqlite3_bind_int64(stmt, 6, rec.next_fetch_at);
    sqlite3_bind_int64(stmt, 7, static_cast<int64_t>(rec.fetch_failures));
    sqlite3_bind_int64(stmt, 8, static_cast<int64_t>(rec.content_hash));
    sqlite3_bind_text(stmt,  9, rec.access_note.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 10, static_cast<int64_t>(rec.crawl_depth));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool MetadataDb::update_status(const std::string& url,
                                CrawlStatus         new_status,
                                const std::string&  access_note) {
    if (!impl_->db) return false;
    const char* sql =
        "UPDATE urls SET status = ?, access_note = ? WHERE url = ?";
    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return false;
    sqlite3_bind_int(stmt,  1, static_cast<int>(new_status));
    sqlite3_bind_text(stmt, 2, access_note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, url.c_str(),         -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

std::optional<UrlRecord> MetadataDb::find_url(const std::string& url) const {
    if (!impl_->db) return std::nullopt;

    const char* sql =
        "SELECT url_id, url, canonical_url, doc_id, status, "
        "last_fetched_at, next_fetch_at, fetch_failures, content_hash, access_note, crawl_depth "
        "FROM urls WHERE url = ? LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return std::nullopt;
    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_STATIC);

    std::optional<UrlRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Impl::row_to_record(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<UrlRecord> MetadataDb::find_by_doc_id(DocId doc_id) const {
    if (!impl_->db) return std::nullopt;

    const char* sql =
        "SELECT url_id, url, canonical_url, doc_id, status, "
        "last_fetched_at, next_fetch_at, fetch_failures, content_hash, access_note, crawl_depth "
        "FROM urls WHERE doc_id = ? LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return std::nullopt;
    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(doc_id));

    std::optional<UrlRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Impl::row_to_record(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<UrlRecord> MetadataDb::find_by_status(CrawlStatus status,
                                                    uint32_t    limit) const {
    if (!impl_->db) return {};
    const char* sql =
        "SELECT url_id, url, canonical_url, doc_id, status, "
        "last_fetched_at, next_fetch_at, fetch_failures, content_hash, access_note, crawl_depth "
        "FROM urls WHERE status = ? LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return {};
    sqlite3_bind_int(stmt, 1, static_cast<int>(status));
    sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

    std::vector<UrlRecord> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(Impl::row_to_record(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

bool MetadataDb::remove_doc(DocId doc_id) {
    if (!impl_->db) return false;
    const char* sql =
        "UPDATE urls SET status = ?, access_note = 'removed' WHERE doc_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return false;
    sqlite3_bind_int(stmt,   1, static_cast<int>(CrawlStatus::Removed));
    sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(doc_id));
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool MetadataDb::tombstone_doc(DocId doc_id) {
    return remove_doc(doc_id);
}

// ── Robots rules ──────────────────────────────────────────────────────────────

bool MetadataDb::upsert_robots(const RobotsRecord& rec) {
    if (!impl_->db) return false;
    const char* sql =
        "INSERT INTO robots(host, rules_text, fetched_at, expires_at) "
        "VALUES(?,?,?,?) "
        "ON CONFLICT(host) DO UPDATE SET "
        "rules_text=excluded.rules_text, "
        "fetched_at=excluded.fetched_at, expires_at=excluded.expires_at";
    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return false;
    sqlite3_bind_text(stmt,  1, rec.host.c_str(),       -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,  2, rec.rules_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, rec.fetched_at);
    sqlite3_bind_int64(stmt, 4, rec.expires_at);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

std::optional<RobotsRecord> MetadataDb::find_robots(const std::string& host) const {
    if (!impl_->db) return std::nullopt;
    const char* sql =
        "SELECT host, rules_text, fetched_at, expires_at FROM robots WHERE host = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return std::nullopt;
    sqlite3_bind_text(stmt, 1, host.c_str(), -1, SQLITE_STATIC);
    std::optional<RobotsRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        RobotsRecord rec;
        rec.host       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.rules_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.fetched_at = sqlite3_column_int64(stmt, 2);
        rec.expires_at = sqlite3_column_int64(stmt, 3);
        result = rec;
    }
    sqlite3_finalize(stmt);
    return result;
}

// ── Crawl frontier ────────────────────────────────────────────────────────────

std::vector<UrlRecord> MetadataDb::pop_crawl_queue(uint32_t limit,
                                                    int64_t  now_unix) const {
    if (!impl_->db) return {};

    char sql[512];
    std::snprintf(sql, sizeof(sql),
        "SELECT url_id, url, canonical_url, doc_id, status, "
        "last_fetched_at, next_fetch_at, fetch_failures, content_hash, access_note, crawl_depth "
        "FROM urls "
        "WHERE status IN (%s) AND next_fetch_at <= ? "
        "ORDER BY next_fetch_at ASC LIMIT ?",
        kEligibleStatusList);

    sqlite3_stmt* stmt = nullptr;
    if (!impl_->prepare(sql, &stmt)) return {};
    sqlite3_bind_int64(stmt, 1, now_unix);
    sqlite3_bind_int(stmt,   2, static_cast<int>(limit));

    std::vector<UrlRecord> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(Impl::row_to_record(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

// ── Transactions ──────────────────────────────────────────────────────────────

bool MetadataDb::begin_transaction()    { return impl_->db && impl_->exec("BEGIN");    }
bool MetadataDb::commit_transaction()   { return impl_->db && impl_->exec("COMMIT");   }
bool MetadataDb::rollback_transaction() { return impl_->db && impl_->exec("ROLLBACK"); }

} // namespace ali
