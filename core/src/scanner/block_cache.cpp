// block_cache.cpp — Bounded decoded-block cache with LRU eviction.

#include "ali/scanner/ali_scanner.h"
#include "ali/locality/locality_memory.h"
#include "ali/storage/types.h"

#include <list>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace ali {

/// A simple bounded block cache. Evicts the LRU entry when the byte budget
/// is exceeded. Locality Memory is consulted for admission but not eviction.
class BlockCache {
public:
    explicit BlockCache(uint32_t max_bytes) : max_bytes_(max_bytes), used_bytes_(0) {}

    struct Entry {
        BlockId              block_id;
        std::vector<Posting> postings;
        uint32_t             size_bytes;
    };

    const std::vector<Posting>* find(BlockId id) {
        auto it = map_.find(id);
        if (it == map_.end()) return nullptr;
        // Move to front (most recently used).
        lru_.splice(lru_.begin(), lru_, it->second);
        return &it->second->postings;
    }

    void insert(BlockId id, std::vector<Posting> postings,
                LocalityMemory& locality, float cost) {
        const uint32_t size = static_cast<uint32_t>(
            postings.size() * sizeof(Posting));

        // Respect the byte budget.
        while (used_bytes_ + size > max_bytes_ && !lru_.empty()) {
            evict_lru();
        }
        if (size > max_bytes_) return; // single entry too large; skip

        if (!locality.should_admit(id, cost)) return;

        lru_.push_front(Entry{id, std::move(postings), size});
        map_[id] = lru_.begin();
        used_bytes_ += size;
    }

    void clear() {
        lru_.clear();
        map_.clear();
        used_bytes_ = 0;
    }

    uint32_t used_bytes() const noexcept { return used_bytes_; }

private:
    void evict_lru() {
        if (lru_.empty()) return;
        auto& back = lru_.back();
        used_bytes_ -= back.size_bytes;
        map_.erase(back.block_id);
        lru_.pop_back();
    }

    uint32_t                                              max_bytes_;
    uint32_t                                              used_bytes_;
    std::list<Entry>                                      lru_;
    std::unordered_map<BlockId, std::list<Entry>::iterator> map_;
};

} // namespace ali
