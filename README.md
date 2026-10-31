# ALI — Adaptive Locality Indexing

An experimental on-device search engine for low-end Android phones (and other platforms).  
Target: up to 1,000,000 indexed webpages per device. Search works offline. No central server required.

> **Status:** Phase 1 — Core structure and baseline implementation complete.  
> Performance targets are hypotheses. No benchmark results exist yet.

---

## Design in one sentence

A compact navigation layer, selective compressed-block retrieval, a bounded 12 MB working set, useful-access locality memory and an opportunistic on-device crawler — all sharing one C++ core across Android, iOS, macOS, Windows and Linux.

---

## Repository layout

```
ALI/
├── core/               # Portable C++20 search engine (ali_core)
│   ├── include/ali/    # Public headers (query, index, scanner, ranking, …)
│   └── src/            # Implementations
├── interfaces/c_api/   # Stable C ABI: ali.h + ali_capi library
├── platform/
│   ├── android/        # Kotlin app + JNI bridge
│   ├── ios/            # Swift (planned)
│   ├── macos/          # Swift or Qt (planned)
│   ├── windows/        # Qt/C++ or C# (planned)
│   └── linux/          # Qt/C++ (planned)
├── tests/
│   ├── unit/           # Component-level correctness tests
│   ├── integration/    # Full index-search round-trip tests
│   ├── recovery/       # Crash-safe publication tests
│   └── fixtures/       # Deterministic test corpus and queries
├── benchmarks/
│   ├── queries/        # Query files for latency benchmarks
│   └── reports/        # Benchmark reports (device, corpus, results)
├── tools/
│   ├── index_inspect/  # CLI: inspect index segments and manifest
│   ├── corpus_import/  # CLI: bulk import TSV corpus
│   └── benchmark/      # CLI: query latency harness
├── docs/               # Architecture, format spec, crawler policy
├── format/             # Format version registry
└── CMakeLists.txt
```

---

## Building

### Prerequisites

- CMake ≥ 3.22
- C++20 compiler (Clang 15+, GCC 12+, MSVC 2022)
- SQLite3 (system library or bundled)

### Desktop build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

### Run tests

```sh
cmake --build build --target test
# or
cd build && ctest --output-on-failure
```

### With AddressSanitizer

```sh
cmake -B build -DALI_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### Android

```sh
# From platform/android/
./gradlew assembleDebug
```

---

## Quick start: import and search

```sh
# Import a TSV corpus (url TAB title TAB body)
./build/tools/corpus_import/ali_import /tmp/ali_index /tmp/ali.db tests/fixtures/corpus_small.tsv

# Inspect the index
./build/tools/index_inspect/ali_inspect /tmp/ali_index

# Run the benchmark harness
./build/tools/benchmark/ali_bench /tmp/ali_index /tmp/ali.db benchmarks/queries/sample_queries.txt
```

---

## Architecture overview

See [`docs/architecture.md`](docs/architecture.md).

Key invariants enforced in code:

1. **Never load an entire segment for a normal query.** The ALI Scanner reads only required compressed blocks.
2. **Bounded working set.** All buffers have hard size limits. No unbounded allocations.
3. **Locality Memory never affects relevance.** It is a cache-admission hint only.
4. **Atomic publication.** Segment files are immutable; the manifest is updated write-to-tmp → rename.
5. **Crash-safe.** An interrupted segment build cannot expose a partial segment.
6. **Crawler yields to interactive search.** The resource controller enforces this.

---

## Development roadmap

| Phase | Deliverable                              | Status         |
|-------|------------------------------------------|----------------|
| 0     | Requirements and test corpus             | ✅ Done         |
| 1     | Correct baseline C++ inverted index      | ✅ In progress  |
| 2     | Immutable segmented binary format        | ✅ In progress  |
| 3     | Posting-list compression (VByte baseline) | ✅ Done        |
| 4     | Compact navigation (FST or sparse dict)  | 🔲 Planned     |
| 5     | ALI Scanner with block instrumentation   | ✅ Skeleton     |
| 6     | Block-Max WAND + equivalence tests       | 🔲 Planned     |
| 7     | Locality Memory and bounded cache        | ✅ Done         |
| 8     | Resource-aware polite crawler            | 🔲 Planned     |
| 9     | Android integration and low-end profiling | ✅ Skeleton    |
| 10    | 1M-document scale test, compaction, hardening | 🔲 Planned |

---

## Acceptance criteria (v0.1)

- [ ] Local corpus indexes into one or more immutable segments.
- [ ] Process closes and reopens index with validation.
- [ ] Query finds documents using a compact navigation structure.
- [ ] Normal queries access selected blocks, not full segments.
- [ ] BM25 returns deterministic top-K for the test corpus.
- [ ] Scanner exposes metrics: blocks accessed, bytes read, peak buffers.
- [ ] Query path has configurable hard limits and handles cancellation.
- [ ] Locality Memory changes cache decisions but not relevance scores.
- [ ] New segment published without corrupting the previous generation.
- [ ] Application searches with network disabled.
- [ ] Benchmark report includes device, corpus, p50/p95/p99, memory and CPU.

---

## References to investigate

- Block-Max WAND and top-K retrieval literature — verify upper-bound correctness before implementation.
- Finite State Transducers for compact static vocabularies.
- SIMD-BP128, VByte, Elias-Fano — benchmark on target ARM hardware.
- SQLite WAL mode and durability for mobile storage.
- Android WorkManager and foreground service background-execution limits.
- Existing baselines: SQLite FTS5, Tantivy, Xapian.

---

## Licence

To be determined.
