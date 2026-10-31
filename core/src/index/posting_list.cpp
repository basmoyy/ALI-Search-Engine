// posting_list.cpp — Posting-list decode helpers.

#include "ali/storage/types.h"
#include "ali/compression/codec.h"

#include <vector>

namespace ali {

/// Decode a compressed block into a vector of Posting structs.
std::vector<Posting> decode_block(const std::vector<uint8_t>& data,
                                   uint32_t                    doc_count) {
    if (data.size() < 8) return {};

    auto& codec = compression::codec_for_id(compression::default_codec());

    // Wire layout: doc_count[4] freq_count[4] doc_bytes freq_bytes
    uint32_t stored_doc_count  = 0;
    uint32_t stored_freq_count = 0;
    std::memcpy(&stored_doc_count,  data.data(),     4);
    std::memcpy(&stored_freq_count, data.data() + 4, 4);

    const uint32_t count = std::min(stored_doc_count, doc_count);
    std::vector<uint32_t> doc_deltas(count), freqs(count);

    const std::span<const uint8_t> rest(data.data() + 8, data.size() - 8);

    const std::size_t doc_consumed =
        codec.decode(rest, count, doc_deltas);
    if (doc_consumed == 0) return {};

    const std::span<const uint8_t> freq_span(rest.data() + doc_consumed,
                                              rest.size() - doc_consumed);
    const std::size_t freq_consumed =
        codec.decode(freq_span, count, freqs);
    if (freq_consumed == 0) return {};

    auto doc_ids = compression::decode_deltas(doc_deltas);
    std::vector<Posting> postings;
    postings.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        postings.push_back({doc_ids[i], freqs[i]});
    }
    return postings;
}

} // namespace ali
