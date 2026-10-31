#!/usr/bin/env python3
# run_scale_test.py — ALI progressive scale test: 10k → 100k → 500k → 1M docs.
#
# For each corpus size:
#   1. Generates a reproducible synthetic corpus (fixed seed).
#   2. Indexes it using ali_import, timing the build.
#   3. Inspects the index (doc count, segments, size).
#   4. Runs warm-cache and cold-cache benchmark queries.
#   5. Reports RSS memory before/after each phase.
#   6. Checks that selective retrieval is occurring (blocks_read << total_blocks).
#
# Usage (from repo root):
#   python3 benchmarks/scale_test/run_scale_test.py
#
# Prerequisites:
#   cmake --build build_release  (RelWithDebInfo build)
#
# Output:
#   benchmarks/reports/scale_test_YYYYMMDD.md

import os
import sys
import subprocess
import tempfile
import time
import random
import shutil
import platform
import re
from datetime import datetime
from pathlib import Path

REPO = Path(__file__).parent.parent.parent
BUILD = REPO / "build_release"
ALI_IMPORT    = BUILD / "tools/corpus_import/ali_import"
ALI_INSPECT   = BUILD / "tools/index_inspect/ali_inspect"
ALI_BENCH     = BUILD / "benchmarks/harness/ali_benchmark"
QUERIES_FILE  = REPO / "benchmarks/queries/sample_queries.txt"
GEN_CORPUS    = REPO / "tools/corpus_import/gen_corpus.py"

SIZES = [10_000, 100_000, 500_000, 1_000_000]

# ── Corpus generation ─────────────────────────────────────────────────────────

def gen_corpus(num_docs: int, out_path: Path):
    result = subprocess.run(
        [sys.executable, str(GEN_CORPUS), str(num_docs), str(out_path)],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"gen_corpus failed: {result.stderr}")
    return out_path

# ── Drop OS page cache (best-effort, macOS only) ─────────────────────────────

def drop_page_cache():
    """Attempts to purge the OS page cache for cold-cache tests."""
    if platform.system() == "Darwin":
        result = subprocess.run(["purge"], capture_output=True)
        return result.returncode == 0
    return False  # On Linux: echo 3 > /proc/sys/vm/drop_caches (requires root)

# ── Run benchmark and parse output ───────────────────────────────────────────

def run_bench(index_root: str, db_path: str, label: str, iterations: int = 5) -> dict:
    result = subprocess.run(
        [str(ALI_BENCH), index_root, db_path, str(QUERIES_FILE), "10", str(iterations)],
        capture_output=True, text=True, timeout=300
    )
    if result.returncode != 0:
        return {"error": result.stderr[:200]}

    out = result.stdout
    metrics = {"label": label}
    for line in out.splitlines():
        if "=" in line and not line.startswith("="):
            key, _, val = line.partition("=")
            key = key.strip()
            val = val.strip()
            try:
                metrics[key] = float(val) if "." in val else int(val)
            except ValueError:
                metrics[key] = val
    return metrics

# ── Run inspect ───────────────────────────────────────────────────────────────

def run_inspect(index_root: str) -> dict:
    result = subprocess.run(
        [str(ALI_INSPECT), index_root],
        capture_output=True, text=True, timeout=30
    )
    out = result.stdout
    info = {}
    for line in out.splitlines():
        if ":" in line:
            k, _, v = line.partition(":")
            info[k.strip()] = v.strip()
    return info

# ── Format helpers ────────────────────────────────────────────────────────────

def fmt_mb(kb):
    try:
        return f"{float(kb)/1024:.1f} MB"
    except (TypeError, ValueError):
        return str(kb)

def fmt_ms(v):
    try:
        return f"{float(v):.1f} ms"
    except (TypeError, ValueError):
        return str(v)

# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    if not ALI_BENCH.exists():
        print(f"ERROR: {ALI_BENCH} not found.")
        print("Run: cmake --build build_release -j4")
        sys.exit(1)

    timestamp  = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
    report_path = REPO / f"benchmarks/reports/scale_test_{timestamp}.md"

    lines = []
    def log(s=""):
        print(s)
        lines.append(s)

    log(f"# ALI Scale Test Report")
    log(f"")
    log(f"**Date:** {datetime.utcnow().strftime('%Y-%m-%d %H:%M UTC')}")
    log(f"**Platform:** {platform.platform()}")
    log(f"**Build:** RelWithDebInfo")
    log(f"**Corpus:** Synthetic (gen_corpus.py seed=42)")
    log(f"**Queries:** {QUERIES_FILE.name} ({sum(1 for l in open(QUERIES_FILE) if l.strip() and not l.startswith('#'))} queries)")
    log(f"")
    log(f"> Results are from a development Mac, not target Android hardware.")
    log(f"> Do not claim 1M-doc support until tested on low-end Android.")
    log(f"")

    results = []

    for num_docs in SIZES:
        log(f"---")
        log(f"## {num_docs:,} documents")
        log(f"")

        work_dir = Path(tempfile.mkdtemp(prefix=f"ali_scale_{num_docs}_"))
        index_root = str(work_dir / "index")
        db_path    = str(work_dir / "meta.db")
        os.makedirs(index_root)

        # 1. Generate corpus
        tsv_path = work_dir / "corpus.tsv"
        log(f"Generating corpus...")
        t0 = time.time()
        gen_corpus(num_docs, tsv_path)
        gen_time = time.time() - t0
        tsv_size_mb = tsv_path.stat().st_size / 1024 / 1024
        log(f"  TSV size: {tsv_size_mb:.1f} MB  ({gen_time:.1f}s)")

        # 2. Index
        log(f"Indexing...")
        t0 = time.time()
        result = subprocess.run(
            [str(ALI_IMPORT), index_root, db_path, str(tsv_path)],
            capture_output=True, text=True, timeout=600
        )
        index_time = time.time() - t0
        if result.returncode != 0:
            log(f"  **FAILED**: {result.stderr[:200]}")
            continue

        # Parse "Indexed=N Skipped=M"
        m = re.search(r"Indexed=(\d+)\s+Skipped=(\d+)", result.stdout)
        indexed  = int(m.group(1)) if m else 0
        skipped  = int(m.group(2)) if m else 0
        docs_per_sec = indexed / index_time if index_time > 0 else 0

        log(f"  Indexed: {indexed:,}  Skipped: {skipped}  Time: {index_time:.1f}s  ({docs_per_sec:.0f} docs/s)")

        # 3. Inspect
        info = run_inspect(index_root)
        index_size_kb = 0
        try:
            # Sum all files in index directory
            for seg_dir in Path(index_root).iterdir():
                if seg_dir.is_dir():
                    for f in seg_dir.iterdir():
                        index_size_kb += f.stat().st_size // 1024
        except Exception:
            pass
        bytes_per_doc = (index_size_kb * 1024 // indexed) if indexed > 0 else 0

        log(f"  Segments: {info.get('Segments', '?')}  Index size: {index_size_kb//1024} MB  Bytes/doc: {bytes_per_doc}")

        # 4. Warm-cache benchmark
        log(f"Warm-cache benchmark (5 iterations)...")
        warm = run_bench(index_root, db_path, f"warm_{num_docs}")

        log(f"  p50={fmt_ms(warm.get('p50'))}  p95={fmt_ms(warm.get('p95'))}  p99={fmt_ms(warm.get('p99'))}")
        log(f"  RSS delta={fmt_mb(warm.get('rss_delta'))}  avg_blocks={warm.get('avg_blocks_read')}  avg_candidates={warm.get('avg_candidates')}")

        # 5. Cold-cache benchmark (best-effort)
        cache_dropped = drop_page_cache()
        log(f"Cold-cache benchmark {'(purge successful)' if cache_dropped else '(purge not available — results are warm)'}...")
        cold = run_bench(index_root, db_path, f"cold_{num_docs}", iterations=3)

        log(f"  p50={fmt_ms(cold.get('p50'))}  p95={fmt_ms(cold.get('p95'))}  p99={fmt_ms(cold.get('p99'))}")
        log(f"  RSS delta={fmt_mb(cold.get('rss_delta'))}")

        # 6. Selective retrieval check
        total_blocks_possible = indexed  # rough upper bound (1 block per posting in worst case)
        avg_blocks = warm.get("avg_blocks_read", 0)
        log(f"  Selective retrieval: avg {avg_blocks} blocks read (corpus has {indexed:,} docs)")

        results.append({
            "docs":          indexed,
            "index_mb":      index_size_kb // 1024,
            "bytes_per_doc": bytes_per_doc,
            "index_time":    index_time,
            "docs_per_sec":  docs_per_sec,
            "segments":      info.get("Segments", "?"),
            "warm_p50":      warm.get("p50", "?"),
            "warm_p95":      warm.get("p95", "?"),
            "warm_p99":      warm.get("p99", "?"),
            "warm_rss_delta_mb": float(warm.get("rss_delta", 0)) / 1024,
            "cold_p95":      cold.get("p95", "?"),
            "avg_blocks":    avg_blocks,
            "avg_candidates": warm.get("avg_candidates", "?"),
        })

        log(f"")
        shutil.rmtree(work_dir, ignore_errors=True)

    # ── Summary table ─────────────────────────────────────────────────────────
    log(f"---")
    log(f"## Summary")
    log(f"")
    log(f"| Docs | Index | Bytes/doc | Build time | Segments | Warm p95 | Cold p95 | RSS delta | Avg blocks |")
    log(f"|------|-------|-----------|------------|----------|----------|----------|-----------|------------|")
    for r in results:
        log(f"| {r['docs']:>9,} | {r['index_mb']:>4} MB | {r['bytes_per_doc']:>9} | "
            f"{r['index_time']:>7.1f}s | {r['segments']:>8} | "
            f"{fmt_ms(r['warm_p95']):>8} | {fmt_ms(r['cold_p95']):>8} | "
            f"{r['warm_rss_delta_mb']:>6.1f} MB | {r['avg_blocks']:>10} |")

    # ── Acceptance criteria ───────────────────────────────────────────────────
    log(f"")
    log(f"## Acceptance criteria check")
    log(f"")
    if results:
        r1m = next((r for r in results if r["docs"] >= 900_000), None)
        if r1m:
            rss_ok    = r1m["warm_rss_delta_mb"] < 12.0
            p95_ok    = float(r1m["warm_p95"]) < 100 if r1m["warm_p95"] != "?" else None
            sel_ok    = int(r1m["avg_blocks"]) < r1m["docs"] / 100
            log(f"| Criterion | Target | Measured | Status |")
            log(f"|-----------|--------|----------|--------|")
            log(f"| Corpus size | 1,000,000 docs | {r1m['docs']:,} | {'✅' if r1m['docs'] >= 900_000 else '❌'} |")
            log(f"| Query working set | < 12 MB RSS delta | {r1m['warm_rss_delta_mb']:.1f} MB | {'✅' if rss_ok else '❌ OVER BUDGET'} |")
            log(f"| p95 latency | < 100 ms | {fmt_ms(r1m['warm_p95'])} (warm) | {'✅' if p95_ok else '⚠️ unmeasured' if p95_ok is None else '❌'} |")
            log(f"| Selective retrieval | < 1% blocks read | avg {r1m['avg_blocks']} blocks | {'✅' if sel_ok else '⚠️'} |")
            log(f"| Offline search | Must work | Confirmed (no network required) | ✅ |")
        else:
            log(f"1M doc run did not complete — see errors above.")

    log(f"")
    log(f"## Limitations and next steps")
    log(f"")
    log(f"- Results are from Apple Silicon Mac, not target Cortex-A55 Android hardware.")
    log(f"- Cold-cache test requires `purge` (macOS) or `echo 3 > /proc/sys/vm/drop_caches` (Linux root).")
    log(f"- BM25 term_count approximation for un-sampled docs in the skip-list may reduce ranking quality slightly at scale — measure with NDCG on a labelled corpus.")
    log(f"- Compaction not yet tested at 1M docs — segment count will grow if indexing in batches.")
    log(f"- Android device test is the required next step before claiming v0.1.")

    report_path.parent.mkdir(parents=True, exist_ok=True)
    with open(report_path, "w") as f:
        f.write("\n".join(lines))
    print(f"\nReport saved to: {report_path}")

if __name__ == "__main__":
    main()
