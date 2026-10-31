// delta.cpp — Gap/delta encoding for sorted document-ID sequences.
//
// Converts a sorted list of document IDs into successive differences
// (gaps). The first element is the first ID itself (gap from 0).
// Delta encoding is composable with any integer codec.

#include "ali/compression/codec.h"

#include <stdexcept>
#include <vector>

namespace ali::compression {

std::vector<uint32_t> encode_deltas(std::span<const uint32_t> sorted_ids) {
    std::vector<uint32_t> deltas;
    deltas.reserve(sorted_ids.size());

    uint32_t prev = 0;
    for (uint32_t id : sorted_ids) {
        if (id < prev) {
            throw std::invalid_argument(
                "encode_deltas: input must be strictly sorted ascending");
        }
        deltas.push_back(id - prev);
        prev = id;
    }
    return deltas;
}

std::vector<uint32_t> decode_deltas(std::span<const uint32_t> deltas,
                                     uint32_t                  first_value) {
    std::vector<uint32_t> ids;
    ids.reserve(deltas.size());

    uint32_t current = first_value;
    for (uint32_t delta : deltas) {
        current += delta;
        ids.push_back(current);
    }
    return ids;
}

} // namespace ali::compression
