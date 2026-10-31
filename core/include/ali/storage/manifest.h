#pragma once
// ali/storage/manifest.h — Atomic segment manifest and catalogue.
//
// The manifest is the single source of truth for which segments are live.
// It is updated atomically (write-to-temp + fsync + rename) so a crash
// during an update never exposes a partially-written catalogue.

#include "ali/storage/types.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ali {

/// Descriptor for one live segment.
struct SegmentEntry {
    Generation  generation      = 0;
    std::string segment_id;          ///< unique stable identifier (e.g. UUID)
    std::filesystem::path path;
    SegmentRange doc_range;
    uint64_t    size_bytes      = 0;
    int64_t     created_at_unix = 0; ///< seconds since epoch
    bool        is_tombstone    = false;
};

/// Immutable snapshot of the manifest at one point in time.
struct ManifestSnapshot {
    uint64_t                 manifest_version = 0;
    std::vector<SegmentEntry> segments;

    [[nodiscard]] uint32_t total_doc_count() const noexcept;
    [[nodiscard]] uint64_t total_size_bytes() const noexcept;
};

/// Reads and writes the segment manifest.
class Manifest {
public:
    explicit Manifest(std::filesystem::path index_root);
    ~Manifest();

    Manifest(const Manifest&)            = delete;
    Manifest& operator=(const Manifest&) = delete;

    /// Load or create the manifest. Returns false on corruption.
    [[nodiscard]] bool open();

    /// Atomically add a new segment to the manifest.
    [[nodiscard]] bool publish_segment(const SegmentEntry& entry);

    /// Atomically remove a segment (e.g. after successful compaction).
    [[nodiscard]] bool retire_segment(const std::string& segment_id);

    /// Replace multiple segments with a merged segment atomically.
    [[nodiscard]] bool replace_segments(const std::vector<std::string>& old_ids,
                                        const SegmentEntry&             new_entry);

    /// Return a consistent snapshot of the current manifest.
    [[nodiscard]] ManifestSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ali
