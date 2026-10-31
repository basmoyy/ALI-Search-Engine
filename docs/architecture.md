# ALI — Adaptive Locality Indexing: Architecture

> **Version:** 0.2 (cross-platform update)  
> **Status:** Initial implementation — targets not yet validated by benchmarks

---

## 1. Core design principle

> Do not optimise how quickly the device processes unnecessary information.  
> Optimise how little information it needs to process in the first place.

---

## 2. Component map

```
┌──────────────────────────────────────────────────────┐
│                  Platform shell                       │
│  Android (Kotlin)  macOS/iOS (Swift)  Windows/Linux  │
└───────────────────────┬──────────────────────────────┘
                        │  Stable C API (ali.h)
┌───────────────────────▼──────────────────────────────┐
│                   C++ Core (ali_core)                 │
│                                                       │
│  Query interface → Query interpreter → Query planner  │
│        ↓                                              │
│  Navigation index (term dictionary)                   │
│        ↓                                              │
│  ALI Scanner (bounded block retrieval)                │
│        ↓              ↑                               │
│  Compression     Locality Memory                      │
│        ↓                                              │
│  Ranking engine (BM25 + Block-Max WAND)               │
│                                                       │
│  Background: Crawler → Parser → SegmentWriter         │
│              ResourceController                       │
│                                                       │
│  Persistent: Manifest  |  MetadataDb (SQLite)         │
│              Binary segment files                     │
└──────────────────────────────────────────────────────┘
```

---

## 3. Data flows

### Query path
1. Platform shell calls `ali_search()` via C API.
2. `QueryInterpreter` normalises and tokenises.
3. `QueryPlanner` reads term statistics; decides retrieval strategy.
4. `AliScanner` reads required compressed blocks only.
5. `Bm25Scorer` scores candidates; `ResultSet` maintains top-K heap.
6. Locality Memory records block access and usefulness signals.
7. Result metadata assembled; response returned to platform shell.

### Crawl and index path
1. `ResourceController` gates whether crawling may proceed.
2. `Scheduler` selects URLs from `MetadataDb` frontier.
3. `Fetcher` retrieves content within size and timeout limits.
4. `HtmlParser` extracts URL, title, body text, outbound links.
5. `Deduplicator` checks canonical URL and content fingerprint.
6. `SegmentWriter` builds an immutable segment in a tmp directory.
7. `Manifest::publish_segment()` atomically makes it visible.
8. `IndexReader::maybe_refresh()` picks up new segments on next query.

---

## 4. Memory budget (initial target — unvalidated)

| Region                 | Budget  |
|------------------------|---------|
| Navigation cache       |  2 MB   |
| Retrieval (block) cache |  4 MB  |
| Candidate / ranking    |  4 MB   |
| Query / decode buffers |  2 MB   |
| **Total working set**  | **12 MB** |

These are experimental targets. Measure actual RSS/PSS separately.

---

## 5. URL lifecycle states (v0.3)

The crawler distinguishes knowing a URL exists from being permitted and able to retrieve it. A blocked or unindexed URL is never equivalent to a nonexistent URL.

| State | Meaning | Terminal? |
|---|---|---|
| `Discovered` | Known; not yet scheduled | No |
| `Queued` | Eligible; waiting for fetch attempt | No |
| `Fetching` | Actively being retrieved (short-lived) | No |
| `Fetched` | Response received; processing may still fail | No |
| `Indexed` | Published into the searchable index | No |
| `Restricted` | Robots/policy/access controls blocked retrieval | **Yes** |
| `AuthRequired` | Content requires authentication | **Yes** |
| `RateLimited` | Host requested reduced frequency; in backoff | No |
| `TemporarilyUnavailable` | Transient network/server/DNS failure | No |
| `Stale` | Previously indexed; may need refresh | No |
| `Removed` | Deleted, withdrawn, or removed from ALI | **Yes** |

Terminal states are never returned by `pop_crawl_queue`. Do not retry `Restricted`, `AuthRequired`, or `Removed` URLs aggressively.

---

## 6. Open decisions

See `docs/decisions/` for individual decision records.

| Decision              | Default for prototype                          |
|-----------------------|------------------------------------------------|
| Languages supported   | English; tokeniser interface extensible        |
| Query semantics       | AND/OR top-K lexical; no phrase in v0.1        |
| Document retention    | Tier 2 (URL + title + excerpt); Tier 3 optional |
| Deduplication         | Canonical URL + content fingerprint            |
| Index update          | Immutable segments + background compaction     |
| Crawl scope           | User-configured seeds and allowed domains      |

---

## 6. Performance targets (hypotheses, unvalidated)

| Metric           | Target          |
|------------------|-----------------|
| Corpus           | 1,000,000 docs  |
| Query p95        | < 100 ms        |
| Working set      | 12 MB budget    |
| Idle CPU         | ~0              |
| Offline search   | Must work       |

See `docs/benchmark-methodology.md` for measurement protocol.

---

## 7. Repository layout

```
ALI/
├── core/               # Portable C++20 search engine
│   ├── include/ali/    # Public headers
│   └── src/            # Implementations
├── interfaces/c_api/   # Stable C ABI (ali.h)
├── platform/
│   ├── android/        # Kotlin + JNI
│   ├── ios/            # Swift (planned)
│   ├── macos/          # Swift or Qt (planned)
│   ├── windows/        # Qt/C++ or C# (planned)
│   └── linux/          # Qt/C++ (planned)
├── storage/            # Index and DB data (runtime, not source)
├── tests/              # Unit, integration, recovery
├── benchmarks/         # Benchmark harness and reports
├── tools/              # Developer CLI tools
├── docs/               # Architecture and policy documents
└── format/             # Format version registry
```
