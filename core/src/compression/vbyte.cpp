// vbyte.cpp — Variable-byte integer codec.
//
// Each integer is stored as a sequence of 7-bit groups, least-significant
// first. The high bit of each byte is 1 if more bytes follow, 0 for the
// last byte of an integer. This is the reference baseline codec.

#include "ali/compression/codec.h"

#include <cassert>
#include <stdexcept>

namespace ali::compression {

// ── Encode ────────────────────────────────────────────────────────────────────

static std::size_t vbyte_encode(std::span<const uint32_t> values,
                                 std::span<uint8_t>        out) {
    uint8_t* p = out.data();
    const uint8_t* end = out.data() + out.size();

    for (uint32_t v : values) {
        while (v >= 0x80u) {
            assert(p < end);
            *p++ = static_cast<uint8_t>((v & 0x7Fu) | 0x80u);
            v >>= 7u;
        }
        assert(p < end);
        *p++ = static_cast<uint8_t>(v);
    }
    return static_cast<std::size_t>(p - out.data());
}

// ── Decode ────────────────────────────────────────────────────────────────────

static std::size_t vbyte_decode(std::span<const uint8_t> in,
                                 uint32_t                 count,
                                 std::span<uint32_t>      out) {
    if (out.size() < count) return 0;

    const uint8_t* p   = in.data();
    const uint8_t* end = in.data() + in.size();

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t value = 0;
        unsigned shift = 0;
        while (p < end) {
            const uint8_t b = *p++;
            value |= static_cast<uint32_t>(b & 0x7Fu) << shift;
            shift += 7u;
            if (!(b & 0x80u)) break;
            if (shift >= 35u) return 0; // malformed: too many bytes for uint32
        }
        out[i] = value;
    }
    return static_cast<std::size_t>(p - in.data());
}

static std::size_t vbyte_max_encoded_size(std::size_t value_count) {
    // Worst case: 5 bytes per uint32 value.
    return value_count * 5;
}

const Codec kVByte = {
    .name             = "vbyte",
    .encode           = vbyte_encode,
    .decode           = vbyte_decode,
    .max_encoded_size = vbyte_max_encoded_size,
};

// ── Codec registry ────────────────────────────────────────────────────────────

const Codec& codec_for_id(CodecId id) {
    switch (id) {
        case CodecId::VByte: return kVByte;
        default:
            throw std::invalid_argument("unknown codec id");
    }
}

CodecId default_codec() noexcept {
    return CodecId::VByte;
}

} // namespace ali::compression
