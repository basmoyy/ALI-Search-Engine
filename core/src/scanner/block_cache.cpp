#include "ali/scanner/block_cache.h"

namespace ali {

BlockCache::BlockCache(uint32_t max_bytes, LocalityMemory& locality)
    : max_bytes_(max_bytes), locality_(locality) {}
BlockCache::~BlockCache() = default;

const CompressedBlock* BlockCache::find(BlockId id) const {
    auto it = map_.find(id);
    if (it == map_.end()) return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second);
    return &it->second->block;
}

void BlockCache::insert(BlockId id, const CompressedBlock& block) {
    if (max_bytes_ == 0) return;
    if (map_.count(id)) return;
    const uint32_t size = static_cast<uint32_t>(block.data.size()) + sizeof(BlockMeta);
    if (size > max_bytes_) return;
    while (used_bytes_ + size > max_bytes_ && !lru_.empty()) evict_lru();
    // Cold-start: always admit. Locality gates eviction only under pressure.
    if (used_bytes_ + size > max_bytes_) {
        if (!locality_.should_admit(id, static_cast<float>(size))) return;
    }
    lru_.push_front(Entry{id, block, size});
    map_[id] = lru_.begin();
    used_bytes_ += size;
}

void BlockCache::clear() { lru_.clear(); map_.clear(); used_bytes_ = 0; }
uint32_t BlockCache::used_bytes()  const noexcept { return used_bytes_; }
uint32_t BlockCache::entry_count() const noexcept { return static_cast<uint32_t>(lru_.size()); }

void BlockCache::evict_lru() {
    if (lru_.empty()) return;
    auto& back = lru_.back();
    used_bytes_ -= back.size_bytes;
    map_.erase(back.block_id);
    lru_.pop_back();
}

} // namespace ali
