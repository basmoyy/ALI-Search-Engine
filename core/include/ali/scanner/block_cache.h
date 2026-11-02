#pragma once
// ali/scanner/block_cache.h — Bounded LRU compressed-block cache.

#include "ali/storage/types.h"
#include "ali/storage/segment_reader.h"
#include "ali/locality/locality_memory.h"

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ali {

class BlockCache {
public:
    explicit BlockCache(uint32_t max_bytes, LocalityMemory& locality);
    ~BlockCache();

    BlockCache(const BlockCache&)            = delete;
    BlockCache& operator=(const BlockCache&) = delete;

    [[nodiscard]] const CompressedBlock* find(BlockId id) const;
    void insert(BlockId id, const CompressedBlock& block);
    void clear();

    [[nodiscard]] uint32_t used_bytes()  const noexcept;
    [[nodiscard]] uint32_t entry_count() const noexcept;

private:
    struct Entry {
        BlockId        block_id;
        CompressedBlock block;
        uint32_t       size_bytes;
    };
    using List     = std::list<Entry>;
    using Iterator = List::iterator;

    void evict_lru();

    uint32_t        max_bytes_;
    uint32_t        used_bytes_ = 0;
    LocalityMemory& locality_;

    mutable List                                        lru_;
    std::unordered_map<BlockId, Iterator>               map_;
};

} // namespace ali
