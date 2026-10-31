// ali_import — Batch corpus importer.
//
// Reads a TSV file (url TAB title TAB body_text) and indexes each line.
//
// Usage: ali_import <index_root> <db_path> <corpus.tsv>

#include "ali.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: ali_import <index_root> <db_path> <corpus.tsv>\n";
        return EXIT_FAILURE;
    }

    AliEngine* engine = nullptr;
    AliStatus s = ali_engine_open(argv[1], argv[2], &engine);
    if (s != ALI_OK) {
        std::cerr << "Failed to open engine (status=" << s << ")\n";
        return EXIT_FAILURE;
    }

    std::ifstream file(argv[3]);
    if (!file) {
        std::cerr << "Cannot open corpus file: " << argv[3] << '\n';
        ali_engine_close(engine);
        return EXIT_FAILURE;
    }

    uint64_t indexed = 0;
    uint64_t skipped = 0;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string url, title, body;
        if (!std::getline(ss, url,   '\t') ||
            !std::getline(ss, title, '\t') ||
            !std::getline(ss, body))
        {
            ++skipped;
            continue;
        }

        s = ali_index_document(engine, url.c_str(), title.c_str(), body.c_str());
        if (s == ALI_OK) {
            ++indexed;
        } else {
            ++skipped;
        }

        if (indexed % 1000 == 0) {
            std::cout << "\rIndexed: " << indexed << "  Skipped: " << skipped << std::flush;
        }
    }

    ali_index_flush(engine);
    ali_engine_close(engine);

    std::cout << "\nDone. Indexed=" << indexed << " Skipped=" << skipped << '\n';
    return EXIT_SUCCESS;
}
