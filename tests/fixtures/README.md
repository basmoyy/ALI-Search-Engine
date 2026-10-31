# Test Fixtures

Deterministic test data used by unit and integration tests.

## corpus_small.tsv

A minimal TSV corpus for unit tests.
Format: `url<TAB>title<TAB>body_text`

Add documents here that cover:
- Short pages (< 20 terms)
- Long pages (> 500 terms)
- Duplicate URLs (for dedup tests)
- Non-ASCII content (for tokeniser tests)
- Empty body (edge case)
- Very long terms (truncation tests)

## queries_small.txt

Queries matched to `corpus_small.tsv` with known expected result counts.
Used by correctness tests to assert deterministic ranking.
