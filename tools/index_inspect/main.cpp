// ali_inspect — Command-line index inspector.
//
// Usage: ali_inspect <index_root>
//   Prints segment list, document count, term count and storage breakdown.

#include "ali/storage/manifest.h"
#include "ali/storage/segment_reader.h"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace fs = std::filesystem;
using namespace ali;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ali_inspect <index_root>\n";
        return EXIT_FAILURE;
    }

    const fs::path root(argv[1]);
    Manifest manifest(root);
    if (!manifest.open()) {
        std::cerr << "Failed to open manifest at: " << root << '\n';
        return EXIT_FAILURE;
    }

    const auto snap = manifest.snapshot();
    std::cout << "ALI Index Inspector\n"
              << "===================\n"
              << "Root:             " << root << '\n'
              << "Manifest version: " << snap.manifest_version << '\n'
              << "Segments:         " << snap.segments.size() << '\n'
              << "Total docs:       " << snap.total_doc_count() << '\n'
              << "Total size:       " << snap.total_size_bytes() / 1024 << " KB\n\n";

    for (const auto& entry : snap.segments) {
        std::cout << "  [" << entry.segment_id << "]\n"
                  << "    gen=" << entry.generation
                  << "  docs=" << entry.doc_range.count()
                  << "  size=" << entry.size_bytes / 1024 << " KB"
                  << (entry.is_tombstone ? "  [TOMBSTONE]" : "")
                  << '\n';

        if (!entry.is_tombstone) {
            SegmentReader reader(entry.path);
            if (reader.is_valid()) {
                std::cout << "    avg_doc_len=" << std::fixed << std::setprecision(1)
                          << reader.avg_doc_length() << '\n';
            } else {
                std::cout << "    [segment unreadable or corrupt]\n";
            }
        }
    }

    return EXIT_SUCCESS;
}
