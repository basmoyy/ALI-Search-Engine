#pragma once
// ali/compression/codec.h — Posting-list integer compression interface.
//
// Codecs operate on sorted delta-encoded document-ID sequences and
// term-frequency arrays. All implementations must be correct first;
// optimisation follows benchmarking on target ARM hardware.

#include <cstdint>
#include <span>
#include <vector>

namespace ali::compression {

/// Encode `values` into `out`. Returns the number of bytes written.
/// `out` must have sufficient capacity; use max_encoded_size() to pre-size.
using EncodeFunc = std::size_t (*)(std::span<const uint32_t> values,
                                   std::span<uint8_t>        out);

/// Decode `count` values from `in` into `out`. Returns the number of bytes
/// consumed from `in`, or 0 on error.
using DecodeFunc = std::size_t (*)(std::span<const uint8_t> in,
                                   uint32_t                 count,
                                   std::span<uint32_t>      out);

struct Codec {
    const char* name;
    EncodeFunc  encode;
    DecodeFunc  decode;

    /// Worst-case encoded byte size for `value_count` uint32_t values.
    std::size_t (*max_encoded_size)(std::size_t value_count);
};

// ── Available codecs ─────────────────────────────────────────────────────────

/// Variable-byte encoding. Correct reference implementation; use as baseline.
extern const Codec kVByte;

/// Gap/delta encoding wrapper: encodes sorted IDs as successive differences.
/// Compose with another codec for final byte encoding.
std::vector<uint32_t> encode_deltas(std::span<const uint32_t> sorted_ids);
std::vector<uint32_t> decode_deltas(std::span<const uint32_t> deltas,
                                     uint32_t                  first_value = 0);

// ── Codec registry ────────────────────────────────────────────────────────────

/// Codec identifier stored in segment headers for format versioning.
enum class CodecId : uint8_t {
    VByte  = 0x01,
    // Future: SIMDBp128 = 0x02, EliasFano = 0x03
};

[[nodiscard]] const Codec& codec_for_id(CodecId id);
[[nodiscard]] CodecId      default_codec() noexcept;

} // namespace ali::compression
