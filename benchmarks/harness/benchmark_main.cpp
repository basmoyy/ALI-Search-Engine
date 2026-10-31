// benchmark_main.cpp — ALI reproducible query latency benchmark.
//
// Measures p50/p95/p99 query latency, peak RSS, blocks read, bytes read,
// and candidates evaluated. Outputs a report suitable for inclusion in
// benchmarks/reports/.
//
// Usage:
//   ali_benchmark <index_root> <db_path> <queries_file> [top_k=10] [iterations=5]
//
// Output format:
//   Section headers followed by key=value lines, one per metric.
//   Machine-readable so Python post-processing can aggregate across runs.
//
// Memory measurement:
//   RSS is read from /proc/self/status (Linux) or getrusage() (macOS/BSD).
//   Report peak RSS before and after the query suite so the delta is visible.
//
// Reproducibility requirements (Architecture Brief §16):
//   - Report device model, OS, compiler, build flags, corpus checksum.
//   - Measure cold-cache and warm-cache separately (user must drop OS caches
//     between cold runs; this tool measures warm by default).
//   - Record p50, p95, p99 latency, CPU time, bytes read, blocks decoded,
//     candidate count and result quality (non-zero result count).

#include "ali.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef __APPLE__
#  include <mach/mach.h>
#elif defined(__linux__)
#  include <sys/resource.h>
#endif

// ── Memory helpers ────────────────────────────────────────────────────────────

/// Returns current process RSS in kilobytes.
static uint64_t rss_kb() {
#ifdef __APPLE__
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<uint64_t>(info.resident_size) / 1024;
    }
    return 0;
#elif defined(__linux__)
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<uint64_t>(usage.ru_maxrss);  // already KB on Linux
#else
    return 0;
#endif
}

// ── Latency percentile ────────────────────────────────────────────────────────

static double percentile(std::vector<uint32_t>& v, double pct) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t idx =
        static_cast<std::size_t>(pct / 100.0 * static_cast<double>(v.size() - 1));
    return static_cast<double>(v[std::min(idx, v.size() - 1)]);
}

// ── System info ───────────────────────────────────────────────────────────────

static std::string get_os_version() {
#ifdef __APPLE__
    std::array<char, 128> buf{};
    FILE* f = popen("sw_vers -productVersion 2>/dev/null", "r");
    if (f) { fgets(buf.data(), buf.size(), f); pclose(f); }
    std::string s(buf.data());
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return "macOS " + s;
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::fprintf(stderr,
            "Usage: ali_benchmark <index_root> <db_path> <queries_file>"
            " [top_k=10] [iterations=5]\n");
        return EXIT_FAILURE;
    }

    const char*  index_root   = argv[1];
    const char*  db_path      = argv[2];
    const char*  queries_file = argv[3];
    const int    top_k        = (argc >= 5) ? std::atoi(argv[4]) : 10;
    const int    iterations   = (argc >= 6) ? std::atoi(argv[5]) : 5;

    // ── Load queries ──────────────────────────────────────────────────────────
    std::vector<std::string> queries;
    {
        std::ifstream f(queries_file);
        if (!f) {
            std::fprintf(stderr, "Cannot open queries file: %s\n", queries_file);
            return EXIT_FAILURE;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line[0] != '#') queries.push_back(line);
        }
    }
    if (queries.empty()) {
        std::fprintf(stderr, "No queries found in %s\n", queries_file);
        return EXIT_FAILURE;
    }

    // ── Open engine ───────────────────────────────────────────────────────────
    AliEngine* engine = nullptr;
    if (ali_engine_open(index_root, db_path, &engine) != ALI_OK || !engine) {
        std::fprintf(stderr, "Failed to open engine\n");
        return EXIT_FAILURE;
    }

    uint64_t doc_count = 0, index_bytes = 0;
    uint32_t seg_count = 0;
    ali_stats_doc_count(engine,    &doc_count);
    ali_stats_segment_count(engine, &seg_count);
    ali_stats_index_bytes(engine,  &index_bytes);

    // ── Print report header ───────────────────────────────────────────────────
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);

    std::cout << "\n=== ALI BENCHMARK REPORT ===\n";
    std::cout << "timestamp="      << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ") << "\n";
    std::cout << "os="             << get_os_version()             << "\n";
    std::cout << "index_root="     << index_root                   << "\n";
    std::cout << "doc_count="      << doc_count                    << "\n";
    std::cout << "segment_count="  << seg_count                    << "\n";
    std::cout << "index_bytes="    << index_bytes                  << "\n";
    std::cout << "queries="        << queries.size()               << "\n";
    std::cout << "top_k="          << top_k                        << "\n";
    std::cout << "iterations="     << iterations                   << "\n";

    // ── Run benchmark ─────────────────────────────────────────────────────────
    const uint64_t rss_before = rss_kb();

    std::vector<uint32_t> latencies_ms;
    latencies_ms.reserve(queries.size() * static_cast<std::size_t>(iterations));

    uint64_t total_blocks     = 0;
    uint64_t total_bytes      = 0;
    uint64_t total_candidates = 0;
    uint64_t total_results    = 0;
    uint64_t zero_result_queries = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        for (const auto& q : queries) {
            AliResults* results = nullptr;

            const auto t0 = std::chrono::steady_clock::now();
            ali_search(engine, q.c_str(),
                       static_cast<uint32_t>(top_k), 2000, &results);
            const auto t1 = std::chrono::steady_clock::now();

            const uint32_t elapsed = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
            latencies_ms.push_back(elapsed);

            if (results) {
                uint32_t blk = 0, cand = 0;
                uint64_t bytes = 0;
                ali_results_metrics(results, &cand, &blk, &bytes, nullptr, nullptr);
                total_blocks     += blk;
                total_bytes      += bytes;
                total_candidates += cand;

                const uint32_t rc = ali_results_count(results);
                total_results += rc;
                if (rc == 0) ++zero_result_queries;

                ali_results_free(results);
            }
        }
    }

    const uint64_t rss_after  = rss_kb();
    const std::size_t n = latencies_ms.size();

    // ── Print results ─────────────────────────────────────────────────────────
    std::cout << "\n--- LATENCY (ms) ---\n";
    std::cout << "p50="  << std::fixed << std::setprecision(1) << percentile(latencies_ms, 50)  << "\n";
    std::cout << "p95="  << percentile(latencies_ms, 95) << "\n";
    std::cout << "p99="  << percentile(latencies_ms, 99) << "\n";
    std::cout << "min="  << latencies_ms.front() << "\n";
    std::cout << "max="  << latencies_ms.back()  << "\n";
    std::cout << "mean=" << std::accumulate(latencies_ms.begin(),
                                            latencies_ms.end(), 0ULL) / n << "\n";

    std::cout << "\n--- MEMORY (KB) ---\n";
    std::cout << "rss_before_queries=" << rss_before  << "\n";
    std::cout << "rss_after_queries="  << rss_after   << "\n";
    std::cout << "rss_delta="          << (rss_after > rss_before ? rss_after - rss_before : 0) << "\n";

    std::cout << "\n--- PER-QUERY AVERAGES ---\n";
    std::cout << "avg_blocks_read="      << total_blocks     / n << "\n";
    std::cout << "avg_bytes_read="       << total_bytes      / n << "\n";
    std::cout << "avg_candidates="       << total_candidates / n << "\n";
    std::cout << "avg_results="          << total_results    / n << "\n";
    std::cout << "zero_result_queries="  << zero_result_queries  << "\n";

    std::cout << "\n--- INDEX ---\n";
    std::cout << "bytes_per_doc=" << (doc_count > 0 ? index_bytes / doc_count : 0) << "\n";

    ali_engine_close(engine);
    std::cout << "\n=== END REPORT ===\n";
    return EXIT_SUCCESS;
}
