# ALI Benchmark Methodology

> Every optimisation claim must be backed by a measurement on a defined device,
> corpus and query set. Do not report estimates as results.

## Device reporting requirements

Each benchmark report must include:

- Device model and Android/OS version
- CPU architecture (e.g. ARM Cortex-A55 × 4 + A78 × 4)
- RAM and available storage at time of test
- Compiler (e.g. clang 17), build flags, optimisation level
- Corpus checksum (SHA-256 of the TSV input)
- Query file checksum

## Corpus requirements

- Minimum 10,000 documents for unit benchmarks.
- Representative mix of: short pages, long pages, duplicate content,
  multiple languages, varied term distributions.
- Document all excluded content (e.g. non-HTML, oversized responses).

## Query suite (stored in `benchmarks/queries/`)

| Category         | Count | Notes                                  |
|------------------|-------|----------------------------------------|
| Common terms     | 20    | Top-1000 English words                 |
| Rare terms       | 20    | Low-frequency terms                    |
| Multi-term AND   | 20    | 2–4 terms, expected results exist      |
| Broad queries    | 10    | High-DF terms; triggers WAND stress    |
| No-result queries | 10   | Terms not in corpus                    |

## Metrics to record

| Metric              | Unit    | Notes                                    |
|---------------------|---------|------------------------------------------|
| p50 / p95 / p99     | ms      | Wall time, cold and warm cache separate  |
| CPU time            | ms      | User + sys                               |
| Bytes read          | bytes   | From disk or page cache                  |
| Blocks decoded      | count   | Raw decode operations                    |
| Peak RSS/PSS        | KB      | Native allocations; report separately    |
| Candidates scored   | count   | Size of accumulator before top-K         |
| Index size          | bytes   | Per component: postings, dict, meta, docs |

## Cold vs warm cache

- **Cold:** Clear the OS page cache between each query batch.
  On Linux: `echo 3 > /proc/sys/vm/drop_caches` (requires root).
  On Android: restart the test process between batches.
- **Warm:** Run the query suite twice and record the second pass.

## Block-Max WAND equivalence test

Compare top-K results with WAND enabled vs exhaustive scoring on the
same query and index. Results must be identical for the same ranking
function and index generation. Report any divergence as a bug.

## Reporting format

Results go in `benchmarks/reports/<date>_<device>_<corpus>.md`.
Include raw numbers, not only summaries.
Do not cherry-pick queries or corpora.
