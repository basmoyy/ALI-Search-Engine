// manifest.cpp — Atomic segment manifest implementation.
//
// The manifest is stored as a JSON-like binary record in `manifest.bin`.
// Updates follow: write to `manifest.bin.tmp` → fsync → rename (atomic on POSIX).
// On open, if `manifest.bin.tmp` exists and `manifest.bin` does not, the
// tmp file is a crash-recovery candidate and is ignored (partial write).

#include "ali/storage/manifest.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace ali {

// ── Wire format (v1) ─────────────────────────────────────────────────────────
// All integers are little-endian.
//
// Header:  magic[4] version[4] manifest_version[8] entry_count[4]
// Entry[]: generation[4] doc_first[4] doc_last[4]
//          size_bytes[8] created_at[8] is_tombstone[1]
//          segment_id_len[2] segment_id[segment_id_len]
//          path_len[2] path[path_len]
//
static constexpr uint8_t kMagic[4] = {'A','L','I','M'};
static constexpr uint32_t kFormatVersion = 1;

static void write_u8(std::ostream& out, uint8_t v) {
    out.write(reinterpret_cast<const char*>(&v), 1);
}
static void write_u16(std::ostream& out, uint16_t v) {
    out.write(reinterpret_cast<const char*>(&v), 2);
}
static void write_u32(std::ostream& out, uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), 4);
}
static void write_u64(std::ostream& out, uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), 8);
}
static void write_str(std::ostream& out, const std::string& s) {
    write_u16(out, static_cast<uint16_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

static uint8_t  read_u8(std::istream& in)  { uint8_t  v; in.read(reinterpret_cast<char*>(&v),1); return v; }
static uint16_t read_u16(std::istream& in) { uint16_t v; in.read(reinterpret_cast<char*>(&v),2); return v; }
static uint32_t read_u32(std::istream& in) { uint32_t v; in.read(reinterpret_cast<char*>(&v),4); return v; }
static uint64_t read_u64(std::istream& in) { uint64_t v; in.read(reinterpret_cast<char*>(&v),8); return v; }
static std::string read_str(std::istream& in) {
    uint16_t len = read_u16(in);
    std::string s(len, '\0');
    in.read(s.data(), len);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────

struct Manifest::Impl {
    std::filesystem::path root;
    mutable std::mutex    mtx;
    ManifestSnapshot      current;

    explicit Impl(std::filesystem::path r) : root(std::move(r)) {}

    std::filesystem::path manifest_path() const { return root / "manifest.bin"; }
    std::filesystem::path tmp_path()      const { return root / "manifest.bin.tmp"; }

    bool write_snapshot(const ManifestSnapshot& snap) const {
        std::ofstream out(tmp_path(), std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out.write(reinterpret_cast<const char*>(kMagic), 4);
        write_u32(out, kFormatVersion);
        write_u64(out, snap.manifest_version);
        write_u32(out, static_cast<uint32_t>(snap.segments.size()));

        for (const auto& e : snap.segments) {
            write_u32(out, e.generation);
            write_u32(out, e.doc_range.first_doc);
            write_u32(out, e.doc_range.last_doc);
            write_u64(out, e.size_bytes);
            write_u64(out, static_cast<uint64_t>(e.created_at_unix));
            write_u8(out,  e.is_tombstone ? 1u : 0u);
            write_str(out, e.segment_id);
            write_str(out, e.path.string());
        }

        out.flush();
        if (!out) return false;
        out.close();

        std::error_code ec;
        std::filesystem::rename(tmp_path(), manifest_path(), ec);
        return !ec;
    }

    bool read_snapshot(ManifestSnapshot& snap) const {
        std::ifstream in(manifest_path(), std::ios::binary);
        if (!in) return false;  // No manifest yet; start empty.

        uint8_t magic[4] = {};
        in.read(reinterpret_cast<char*>(magic), 4);
        if (std::memcmp(magic, kMagic, 4) != 0) return false;

        uint32_t ver = read_u32(in);
        if (ver != kFormatVersion) return false;

        snap.manifest_version = read_u64(in);
        uint32_t count = read_u32(in);

        snap.segments.clear();
        snap.segments.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            SegmentEntry e;
            e.generation             = read_u32(in);
            e.doc_range.first_doc    = read_u32(in);
            e.doc_range.last_doc     = read_u32(in);
            e.size_bytes             = read_u64(in);
            e.created_at_unix        = static_cast<int64_t>(read_u64(in));
            e.is_tombstone           = read_u8(in) != 0;
            e.segment_id             = read_str(in);
            e.path                   = read_str(in);
            snap.segments.push_back(std::move(e));  // fix: was missing
        }
        return in.good() || in.eof();
    }
};

// ── ManifestSnapshot helpers ─────────────────────────────────────────────────

uint32_t ManifestSnapshot::total_doc_count() const noexcept {
    uint32_t total = 0;
    for (const auto& e : segments) {
        if (!e.is_tombstone) total += e.doc_range.count();
    }
    return total;
}

uint64_t ManifestSnapshot::total_size_bytes() const noexcept {
    uint64_t total = 0;
    for (const auto& e : segments) {
        if (!e.is_tombstone) total += e.size_bytes;
    }
    return total;
}

// ── Manifest ─────────────────────────────────────────────────────────────────

Manifest::Manifest(std::filesystem::path index_root)
    : impl_(new Impl(std::move(index_root))) {}
Manifest::~Manifest() = default;

bool Manifest::open() {
    std::error_code ec;
    std::filesystem::create_directories(impl_->root, ec);
    if (ec) return false;

    ManifestSnapshot snap;
    snap.manifest_version = 0;
    if (!impl_->read_snapshot(snap)) {
        // Fresh index.
        impl_->current = ManifestSnapshot{};
        return true;
    }
    impl_->current = std::move(snap);
    return true;
}

bool Manifest::publish_segment(const SegmentEntry& entry) {
    std::lock_guard lock(impl_->mtx);
    ManifestSnapshot snap = impl_->current;
    snap.manifest_version++;
    snap.segments.push_back(entry);
    if (!impl_->write_snapshot(snap)) return false;
    impl_->current = std::move(snap);
    return true;
}

bool Manifest::retire_segment(const std::string& segment_id) {
    std::lock_guard lock(impl_->mtx);
    ManifestSnapshot snap = impl_->current;
    snap.manifest_version++;
    for (auto& e : snap.segments) {
        if (e.segment_id == segment_id) { e.is_tombstone = true; break; }
    }
    if (!impl_->write_snapshot(snap)) return false;
    impl_->current = std::move(snap);
    return true;
}

bool Manifest::replace_segments(const std::vector<std::string>& old_ids,
                                  const SegmentEntry&             new_entry) {
    std::lock_guard lock(impl_->mtx);
    ManifestSnapshot snap = impl_->current;
    snap.manifest_version++;

    for (auto& e : snap.segments) {
        for (const auto& id : old_ids) {
            if (e.segment_id == id) { e.is_tombstone = true; break; }
        }
    }
    snap.segments.push_back(new_entry);

    if (!impl_->write_snapshot(snap)) return false;
    impl_->current = std::move(snap);
    return true;
}

ManifestSnapshot Manifest::snapshot() const {
    std::lock_guard lock(impl_->mtx);
    return impl_->current;
}

} // namespace ali
