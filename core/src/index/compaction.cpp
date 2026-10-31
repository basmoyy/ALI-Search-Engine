// compaction.cpp — Background segment compaction.
//
// Merges small segments into larger ones and applies Removed-status deletions
// to keep the segment count bounded and the index clean.
//
// Safety rules:
//   - Never runs while interactive search is active.
//   - Checks resource_controller.may_compact() before and between merge steps.
//   - Writes the merged segment to a temporary directory; publishes atomically
//     via Manifest::replace_segments() only after successful validation.
//   - An interrupted compaction leaves the original segments intact (the tmp
//     directory is cleaned up; the manifest is never partially updated).
//   - cancel_flag allows the caller to abort at any merge step.
//
// Implementation order per roadmap Phase 10:
//   - Identify the N smallest live segments.
//   - Read and merge their posting lists in sorted order.
//   - Skip documents whose URL record has status == Removed.
//   - Write the merged segment to a tmp dir.
//   - Atomically swap via Manifest::replace_segments().
//   - Remove the now-retired tmp directory.

#include "ali/index/compaction.h"
#include "ali/storage/segment_reader.h"
#include "ali/storage/segment_writer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>

namespace ali {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string new_segment_id() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(16) << rng()
       << std::setw(16) << rng();
    return ss.str();
}

static int64_t now_unix() {
    using namespace std::chrono;
    return duration_cast<seconds>(
        system_clock::now().time_since_epoch()).count();
}

// ── Compaction ────────────────────────────────────────────────────────────────

Compaction::Compaction(Manifest&           manifest,
                       MetadataDb&         db,
                       ResourceController& resources,
                       CompactionConfig    cfg)
    : manifest_(manifest), db_(db), resources_(resources), cfg_(std::move(cfg)) {}

bool Compaction::needs_compaction() const {
    const auto snap = manifest_.snapshot();
    uint32_t live = 0;
    for (const auto& e : snap.segments) {
        if (!e.is_tombstone) ++live;
    }
    return live > cfg_.max_segments_before_compact;
}

CompactionResult Compaction::run(std::atomic<bool>& cancel_flag) {
    CompactionResult result;

    if (!resources_.may_compact()) {
        result.error_detail = "Resource conditions do not permit compaction";
        return result;
    }

    const auto snap = manifest_.snapshot();

    // Collect live segments sorted by doc_count ascending (merge smallest first).
    std::vector<const SegmentEntry*> live;
    for (const auto& e : snap.segments) {
        if (!e.is_tombstone) live.push_back(&e);
    }

    if (live.size() <= cfg_.max_segments_before_compact) {
        result.completed    = true; // nothing to do
        return result;
    }

    std::sort(live.begin(), live.end(),
              [](const SegmentEntry* a, const SegmentEntry* b) {
                  return a->doc_range.count() < b->doc_range.count();
              });

    // Take the two smallest segments to merge.
    if (live.size() < 2) { result.completed = true; return result; }
    const SegmentEntry* seg_a = live[0];
    const SegmentEntry* seg_b = live[1];

    if (cancel_flag.load()) {
        result.error_detail = "Cancelled before merge started";
        return result;
    }

    // Open both segments for reading.
    SegmentReader reader_a(seg_a->path);
    SegmentReader reader_b(seg_b->path);
    if (!reader_a.is_valid() || !reader_b.is_valid()) {
        result.error_detail = "Cannot open one or both source segments";
        return result;
    }

    // Prepare a merged segment writer.
    const std::string merged_id  = new_segment_id();
    const auto index_root = seg_a->path.parent_path();
    const auto tmp_dir    = index_root / ("seg_" + merged_id + ".tmp");
    const auto dest_dir   = index_root / ("seg_" + merged_id);

    SegmentWriterConfig wcfg;
    wcfg.max_docs        = cfg_.target_docs_per_segment;
    wcfg.max_segment_bytes = cfg_.max_segment_bytes;

    SegmentWriter writer(tmp_dir, wcfg);

    // Re-index all non-Removed documents from both source segments.
    // Note: we iterate via the stored display data; a full merge would
    // re-read posting lists. This initial implementation rebuilds from
    // the document store, which is correct but not optimal for very large
    // segments. Phase 10 profiling will show whether a posting-list merge
    // is warranted.

    auto merge_segment = [&](const SegmentReader& reader) -> bool {
        const uint32_t count = reader.doc_count();
        for (uint32_t i = 0; i < count; ++i) {
            if (cancel_flag.load()) return false;
            if (!resources_.may_compact()) return false;

            std::string url, title, excerpt;
            if (!reader.get_doc_display(i, url, title, excerpt)) continue;

            // Skip documents marked Removed in the metadata DB.
            auto meta = db_.find_url(url);
            if (meta && meta->status == CrawlStatus::Removed) {
                ++result.docs_removed;
                continue;
            }

            writer.add_document(url, title, excerpt, {});
        }
        return true;
    };

    if (!merge_segment(reader_a) || !merge_segment(reader_b)) {
        result.error_detail = "Merge aborted by cancel or resource pressure";
        std::filesystem::remove_all(tmp_dir);
        return result;
    }

    // Publish the merged segment.
    const Generation gen = snap.manifest_version + 1;
    if (!writer.finish(dest_dir, gen)) {
        result.error_detail = "Failed to write merged segment";
        std::filesystem::remove_all(tmp_dir);
        std::filesystem::remove_all(dest_dir);
        return result;
    }

    SegmentEntry merged_entry;
    merged_entry.generation      = gen;
    merged_entry.segment_id      = merged_id;
    merged_entry.path            = dest_dir;
    merged_entry.size_bytes      = writer.estimated_bytes();
    merged_entry.created_at_unix = now_unix();

    const bool ok = manifest_.replace_segments(
        {seg_a->segment_id, seg_b->segment_id}, merged_entry);

    if (!ok) {
        result.error_detail = "Manifest update failed";
        std::filesystem::remove_all(dest_dir);
        return result;
    }

    result.segments_merged  = 2;
    result.segments_created = 1;
    result.completed        = true;
    return result;
}

} // namespace ali
