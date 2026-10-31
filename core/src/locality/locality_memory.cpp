// locality_memory.cpp — Compact block-usage signal store.

#include "ali/locality/locality_memory.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ali {

struct LocalityMemory::Impl {
    LocalityConfig cfg;
    mutable std::mutex mtx;
    std::unordered_map<BlockId, LocalityEntry> entries;

    int64_t now_seconds() const noexcept {
        using namespace std::chrono;
        return duration_cast<seconds>(
            system_clock::now().time_since_epoch()).count();
    }

    float compute_priority(const LocalityEntry& e, int64_t now) const noexcept {
        const float age = static_cast<float>(
            std::max<int64_t>(0, now - e.last_access_time));
        return (1.0f
            + cfg.alpha * static_cast<float>(e.access_count)
            + cfg.beta  * static_cast<float>(e.useful_access_count))
            * std::exp(-cfg.lambda * age);
    }
};

LocalityMemory::LocalityMemory(LocalityConfig cfg) : impl_(new Impl{cfg}) {}
LocalityMemory::~LocalityMemory() = default;

void LocalityMemory::record_access(BlockId block_id, bool was_useful) {
    const int64_t now = impl_->now_seconds();
    std::lock_guard lock(impl_->mtx);

    auto& e = impl_->entries[block_id];
    e.block_id = block_id;
    if (e.access_count < UINT32_MAX) ++e.access_count;
    if (was_useful && e.useful_access_count < UINT32_MAX) ++e.useful_access_count;
    e.last_access_time = now;
    e.priority = impl_->compute_priority(e, now);

    // Enforce entry cap with simple eviction of the lowest-priority entry.
    if (impl_->entries.size() > impl_->cfg.max_entries) {
        auto min_it = impl_->entries.begin();
        for (auto it = impl_->entries.begin(); it != impl_->entries.end(); ++it) {
            if (it->second.priority < min_it->second.priority) min_it = it;
        }
        impl_->entries.erase(min_it);
    }
}

void LocalityMemory::record_batch_access(const BlockId* block_ids,
                                          const bool*    useful,
                                          std::size_t    count) {
    for (std::size_t i = 0; i < count; ++i) {
        record_access(block_ids[i], useful[i]);
    }
}

float LocalityMemory::priority(BlockId block_id) const noexcept {
    std::lock_guard lock(impl_->mtx);
    auto it = impl_->entries.find(block_id);
    if (it == impl_->entries.end()) return 0.0f;
    return impl_->compute_priority(it->second, impl_->now_seconds());
}

bool LocalityMemory::should_admit(BlockId block_id,
                                   float   retrieval_cost_estimate) const noexcept {
    // Admit if the cached priority exceeds the retrieval cost estimate.
    return priority(block_id) >= retrieval_cost_estimate;
}

bool LocalityMemory::load(const std::filesystem::path& path) {
    // Binary format: count (uint32) followed by count LocalityEntry records.
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t count = 0;
    if (!f.read(reinterpret_cast<char*>(&count), sizeof(count))) return false;

    std::lock_guard lock(impl_->mtx);
    impl_->entries.clear();
    impl_->entries.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        LocalityEntry e;
        if (!f.read(reinterpret_cast<char*>(&e), sizeof(e))) return false;
        impl_->entries[e.block_id] = e;
    }
    return true;
}

bool LocalityMemory::save(const std::filesystem::path& path) const {
    std::lock_guard lock(impl_->mtx);

    // Write to a temporary file first; rename on success.
    auto tmp = path;
    tmp += ".tmp";

    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const uint32_t count = static_cast<uint32_t>(impl_->entries.size());
    f.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [id, e] : impl_->entries) {
        f.write(reinterpret_cast<const char*>(&e), sizeof(e));
    }
    f.flush();
    if (!f) return false;
    f.close();

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

void LocalityMemory::compact() {
    std::lock_guard lock(impl_->mtx);
    const int64_t now = impl_->now_seconds();

    // Recompute priorities and remove entries with near-zero priority.
    constexpr float kMinPriority = 0.001f;
    for (auto it = impl_->entries.begin(); it != impl_->entries.end(); ) {
        it->second.priority = impl_->compute_priority(it->second, now);
        if (it->second.priority < kMinPriority) {
            it = impl_->entries.erase(it);
        } else {
            ++it;
        }
    }
}

uint32_t LocalityMemory::entry_count() const noexcept {
    std::lock_guard lock(impl_->mtx);
    return static_cast<uint32_t>(impl_->entries.size());
}

} // namespace ali
