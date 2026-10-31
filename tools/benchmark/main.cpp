// ali_bench — Query latency benchmark harness.
//
// Reads a queries file (one query per line), runs each query against the
// index and reports p50/p95/p99 latency, blocks read, bytes read and
// candidates evaluated.
//
// Usage: ali_bench <index_root> <db_path> <queries.txt> [iterations=3]

#include "ali.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

static uint32_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static float percentile(std::vector<uint32_t>& v, double pct) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    const std::size_t idx = static_cast<std::size_t>(pct / 100.0 * (v.size() - 1));
    return static_cast<float>(v[idx]);
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: ali_bench <index_root> <db_path> <queries.txt> [iterations]\n";
        return EXIT_FAILURE;
    }

    const int iterations = (argc >= 5) ? std::atoi(argv[4]) : 3;

    AliEngine* engine = nullptr;
    if (ali_engine_open(argv[1], argv[2], &engine) != ALI_OK) {
        std::cerr << "Failed to open engine\n";
        return EXIT_FAILURE;
    }

    std::ifstream file(argv[3]);
    if (!file) {
        std::cerr << "Cannot open queries file: " << argv[3] << '\n';
        ali_engine_close(engine);
        return EXIT_FAILURE;
    }

    std::vector<std::string> queries;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') queries.push_back(line);
    }

    if (queries.empty()) {
        std::cerr << "No queries found.\n";
        ali_engine_close(engine);
        return EXIT_FAILURE;
    }

    std::vector<uint32_t> latencies;
    latencies.reserve(queries.size() * static_cast<std::size_t>(iterations));
    uint64_t total_blocks = 0, total_bytes = 0, total_candidates = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        for (const auto& q : queries) {
            AliResults* results = nullptr;
            const uint32_t t0 = now_ms();
            ali_search(engine, q.c_str(), 10, 500, &results);
            const uint32_t elapsed = now_ms() - t0;
            latencies.push_back(elapsed);

            if (results) {
                uint32_t blocks = 0, candidates = 0;
                uint64_t bytes  = 0;
                ali_results_metrics(results, &candidates, &blocks, &bytes, nullptr, nullptr);
                total_blocks     += blocks;
                total_bytes      += bytes;
                total_candidates += candidates;
                ali_results_free(results);
            }
        }
    }

    ali_engine_close(engine);

    const std::size_t n = latencies.size();
    std::cout << "\n=== ALI Query Benchmark ===\n"
              << "Queries:        " << queries.size() << '\n'
              << "Iterations:     " << iterations << '\n'
              << "Total runs:     " << n << "\n\n"
              << "Latency (ms)\n"
              << "  p50: "  << std::fixed << std::setprecision(1) << percentile(latencies, 50) << '\n'
              << "  p95: "  << percentile(latencies, 95) << '\n'
              << "  p99: "  << percentile(latencies, 99) << '\n'
              << "\nAvg blocks/query:     " << total_blocks     / n << '\n'
              << "Avg bytes_read/query: " << total_bytes      / n << '\n'
              << "Avg candidates/query: " << total_candidates / n << '\n';

    return EXIT_SUCCESS;
}
