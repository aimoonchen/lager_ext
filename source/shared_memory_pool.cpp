// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file shared_memory_pool.cpp
/// @brief Implementation of shared memory pool optimized for SPSC IPC pattern

#include <lager_ext/shared_memory_pool.h>

#include <atomic>
#include <cstring>
#include <array>

// Boost.Interprocess (hidden from headers)
#include <boost/interprocess/mapped_region.hpp>
// Use Windows native shared memory to avoid Boost intermodule singleton issues
#ifdef _WIN32
#include <boost/interprocess/windows_shared_memory.hpp>
#else
#include <boost/interprocess/shared_memory_object.hpp>
#endif

namespace bip = boost::interprocess;

namespace lager_ext {
namespace ipc {

//=============================================================================
// Error storage (SPSC mode: single thread per process)
//=============================================================================

static std::string s_lastError;

//=============================================================================
// SPSC LIFO Cache Entry (in shared memory)
//=============================================================================

/// Entry in the SPSC LIFO cache - stored in shared memory
struct alignas(8) SPSCCacheEntry {
    uint32_t offset;     // Block offset in pool (UINT32_MAX = invalid)
    uint32_t blockSize;  // Block size (for quick matching)
};

static_assert(sizeof(SPSCCacheEntry) == 8, "SPSCCacheEntry should be 8 bytes");

//=============================================================================
// SPSC LIFO Cache (in shared memory)
//=============================================================================

/// SPSC LIFO cache structure - lives in shared memory header
/// 
/// Design:
/// - Consumer (deallocate) pushes to head
/// - Producer (allocate) pops from head  
/// - LIFO order ensures most recently freed blocks are reused first
/// - Single atomic counter for SPSC safety
///
/// Memory Layout:
/// - count: number of valid entries (atomic, both sides update)
/// - entries[]: array of cache entries (LIFO stack)
///
/// Size calculation:
/// - atomic<uint32_t>: 4 bytes
/// - reserved[3]: 12 bytes  
/// - entries[8]: 8 * 8 = 64 bytes
/// - Total: 80 bytes, padded to 128 bytes (2 cache lines)
struct alignas(64) SPSCLifoCache {
    // Atomic count of valid entries in cache
    // Consumer increments on push, Producer decrements on pop
    std::atomic<uint32_t> count;
    
    // Padding to separate atomic from data
    uint32_t reserved[3];
    
    // Cache entries - LIFO stack
    // Index 0 is the "top" of the LIFO stack
    SPSCCacheEntry entries[SPSC_LIFO_CACHE_SIZE];  // 8 entries * 8 bytes = 64 bytes
    
    // Padding to fill to 128 bytes (2 cache lines)
    // 4 + 12 + 64 = 80 bytes used, need 48 bytes padding
    uint8_t padding[48];
    
    void init() noexcept {
        count.store(0, std::memory_order_relaxed);
        for (auto& entry : entries) {
            entry.offset = UINT32_MAX;
            entry.blockSize = 0;
        }
    }
    
    /// Push a freed block to the cache (Consumer side)
    /// @return true if pushed, false if cache is full
    bool push(uint32_t offset, uint32_t blockSize) noexcept {
        uint32_t currentCount = count.load(std::memory_order_acquire);
        if (currentCount >= SPSC_LIFO_CACHE_SIZE) {
            return false;  // Cache full
        }
        
        // Shift entries down to make room at index 0 (LIFO top)
        for (size_t i = SPSC_LIFO_CACHE_SIZE - 1; i > 0; --i) {
            entries[i] = entries[i - 1];
        }
        
        // Insert at top
        entries[0] = {offset, blockSize};
        
        // Increment count with release semantics (makes entries visible to Producer)
        count.store(currentCount + 1, std::memory_order_release);
        return true;
    }
    
    /// Pop a suitable block from the cache (Producer side)
    /// @param minSize Minimum block size required
    /// @return Offset of suitable block, or UINT32_MAX if none found
    uint32_t pop(uint32_t minSize) noexcept {
        uint32_t currentCount = count.load(std::memory_order_acquire);
        if (currentCount == 0) {
            return UINT32_MAX;  // Cache empty
        }
        
        // Search for a suitable block (starting from top)
        for (size_t i = 0; i < currentCount && i < SPSC_LIFO_CACHE_SIZE; ++i) {
            if (entries[i].offset != UINT32_MAX && entries[i].blockSize >= minSize) {
                // Found suitable block
                uint32_t offset = entries[i].offset;
                
                // Remove this entry by shifting remaining entries up
                for (size_t j = i; j < SPSC_LIFO_CACHE_SIZE - 1; ++j) {
                    entries[j] = entries[j + 1];
                }
                entries[SPSC_LIFO_CACHE_SIZE - 1] = {UINT32_MAX, 0};
                
                // Decrement count with release semantics
                count.store(currentCount - 1, std::memory_order_release);
                return offset;
            }
        }
        
        return UINT32_MAX;  // No suitable block found
    }
    
    /// Peek at cache state (for debugging/stats)
    uint32_t size() const noexcept {
        return count.load(std::memory_order_relaxed);
    }
};

// Verify cache fits in a cache line multiple
static_assert(sizeof(SPSCLifoCache) == 128, "SPSCLifoCache should be 128 bytes");

//=============================================================================
// Pool Header (in shared memory)
//=============================================================================

/// Pool header structure - located at the beginning of shared memory
/// 
/// Layout (total 448 bytes):
/// - Cache Line 0 (64B): magic, version, poolSize, dataOffset, padding
/// - Cache Line 1 (64B): freeHead atomic, padding
/// - Cache Line 2 (64B): statistics (allocatedCount, totalAlloc, totalDealloc), padding
/// - Cache Lines 3-4 (128B): SPSC LIFO Cache
/// - Cache Lines 5-6 (128B): Bitmap for block tracking
struct PoolHeader {
    static constexpr uint32_t MAGIC = 0x4C475058;  // "LGPX"
    static constexpr uint16_t VERSION = 3;  // Version 3: SPSC LIFO cache in shared memory

    // === Cache Line 0: Basic info (64 bytes) ===
    uint32_t magic;              // 4 bytes
    uint16_t version;            // 2 bytes
    uint16_t reserved1;          // 2 bytes
    uint32_t poolSize;           // 4 bytes - Total pool size in bytes
    uint32_t dataOffset;         // 4 bytes - Offset where data region starts
    uint8_t headerPadding[48];   // 48 bytes padding (64 - 16 = 48)
    
    // === Cache Line 1: Free list head (64 bytes) ===
    alignas(64) std::atomic<uint32_t> freeHead;  // 4 bytes
    uint8_t freeHeadPadding[60];                 // 60 bytes padding
    
    // === Cache Line 2: Statistics (64 bytes) ===
    alignas(64) std::atomic<uint32_t> allocatedCount;      // 4 bytes
    std::atomic<uint32_t> totalAllocations;                // 4 bytes
    std::atomic<uint32_t> totalDeallocations;              // 4 bytes
    uint8_t statsPadding[52];                              // 52 bytes padding (64 - 12 = 52)
    
    // === Cache Lines 3-4: SPSC LIFO Cache (128 bytes) ===
    alignas(64) SPSCLifoCache spscCache;
    
    // === Cache Lines 5-6: Bitmap for allocation tracking (128 bytes) ===
    alignas(64) uint8_t bitmap[128];

    bool is_valid() const { return magic == MAGIC && version == VERSION; }
};

// Layout verification:
// Cache Line 0: 64 bytes (16 used + 48 padding)
// Cache Line 1: 64 bytes (4 used + 60 padding)
// Cache Line 2: 64 bytes (12 used + 52 padding)
// Cache Lines 3-4: 128 bytes (SPSCLifoCache)
// Cache Lines 5-6: 128 bytes (bitmap)
// Total: 64 + 64 + 64 + 128 + 128 = 448 bytes
static_assert(sizeof(PoolHeader) == 448, "PoolHeader size mismatch");

/// Block header - prepended to each allocated block
struct BlockHeader {
    static constexpr uint32_t MAGIC = 0x424C4B48;  // "BLKH"

    uint32_t magic;
    uint32_t size;          // User-requested size
    uint32_t blockSize;     // Actual block size (including header, aligned)
    uint32_t nextFree;      // Offset of next free block (when in free list)

    bool is_valid() const { return magic == MAGIC; }
};

static_assert(sizeof(BlockHeader) == 16, "BlockHeader should be 16 bytes");

//=============================================================================
// SharedMemoryPool::Impl
//=============================================================================

class SharedMemoryPool::Impl {
public:
    Impl() = default;

    ~Impl() {
#ifndef _WIN32
        // POSIX: cleanup shared memory if we created it
        // Windows: kernel handles cleanup automatically via reference counting
        if (isCreator_ && !name_.empty()) {
            try {
                bip::shared_memory_object::remove(name_.c_str());
            } catch (...) {}
        }
#endif
    }

    bool create(std::string_view name, size_t poolSize) {
        name_ = std::string(name);
        isCreator_ = true;
        poolSize_ = poolSize;

        try {
            // Total size = header + pool
            size_t totalSize = sizeof(PoolHeader) + poolSize;

#ifdef _WIN32
            // Windows: use native shared memory
            shm_ = std::make_unique<bip::windows_shared_memory>(
                bip::create_only, name_.c_str(), bip::read_write, totalSize);

            // Map region
            region_ = bip::mapped_region(*shm_, bip::read_write);
#else
            // POSIX: Remove any existing
            bip::shared_memory_object::remove(name_.c_str());

            // Create shared memory
            shm_ = bip::shared_memory_object(bip::create_only, name_.c_str(), bip::read_write);
            shm_.truncate(static_cast<bip::offset_t>(totalSize));

            // Map region
            region_ = bip::mapped_region(shm_, bip::read_write);
#endif

            // Initialize header
            header_ = new (region_.get_address()) PoolHeader{};
            header_->magic = PoolHeader::MAGIC;
            header_->version = PoolHeader::VERSION;
            header_->reserved1 = 0;
            header_->poolSize = static_cast<uint32_t>(poolSize);
            header_->dataOffset = sizeof(PoolHeader);
            header_->freeHead.store(0, std::memory_order_relaxed);
            header_->allocatedCount.store(0, std::memory_order_relaxed);
            header_->totalAllocations.store(0, std::memory_order_relaxed);
            header_->totalDeallocations.store(0, std::memory_order_relaxed);
            std::memset(header_->bitmap, 0, sizeof(header_->bitmap));
            
            // Initialize SPSC LIFO cache
            header_->spscCache.init();

            // Initialize entire pool as one big free block
            auto* firstBlock = reinterpret_cast<BlockHeader*>(dataRegion());
            firstBlock->magic = BlockHeader::MAGIC;
            firstBlock->size = 0;
            firstBlock->blockSize = static_cast<uint32_t>(poolSize);
            firstBlock->nextFree = UINT32_MAX;  // End of list

            return true;
        } catch (const std::exception& e) {
            s_lastError = std::string("Failed to create pool: ") + e.what();
            return false;
        }
    }

    bool open(std::string_view name) {
        name_ = std::string(name);
        isCreator_ = false;

        try {
#ifdef _WIN32
            // Windows: open existing native shared memory
            shm_ = std::make_unique<bip::windows_shared_memory>(
                bip::open_only, name_.c_str(), bip::read_write);

            // Map region
            region_ = bip::mapped_region(*shm_, bip::read_write);
#else
            // POSIX: Open existing
            shm_ = bip::shared_memory_object(bip::open_only, name_.c_str(), bip::read_write);

            // Map region
            region_ = bip::mapped_region(shm_, bip::read_write);
#endif

            // Get header
            header_ = static_cast<PoolHeader*>(region_.get_address());

            if (!header_->is_valid()) {
                s_lastError = "Invalid pool header (version mismatch or corruption)";
                return false;
            }

            poolSize_ = header_->poolSize;
            return true;
        } catch (const std::exception& e) {
            s_lastError = std::string("Failed to open pool: ") + e.what();
            return false;
        }
    }

    SharedMemoryPool::Block allocate(size_t requestedSize) {
        if (!header_) {
            s_lastError = "Pool not initialized";
            return {};
        }

        // Calculate actual block size needed (header + data, aligned to MIN_BLOCK_SIZE)
        size_t totalNeeded = sizeof(BlockHeader) + requestedSize;
        size_t blockSize = ((totalNeeded + MIN_BLOCK_SIZE - 1) / MIN_BLOCK_SIZE) * MIN_BLOCK_SIZE;

        if (blockSize > poolSize_) {
            s_lastError = "Requested size exceeds pool size";
            return {};
        }

        // =====================================================================
        // SPSC LIFO Cache Fast Path - O(1) for recently freed blocks
        // =====================================================================
        uint32_t cachedOffset = header_->spscCache.pop(static_cast<uint32_t>(blockSize));
        if (cachedOffset != UINT32_MAX) {
            auto* block = blockAt(cachedOffset);
            if (block->is_valid()) {
                block->size = static_cast<uint32_t>(requestedSize);
                
                // Mark as allocated in bitmap
                size_t blockIndex = cachedOffset / MIN_BLOCK_SIZE;
                if (blockIndex < sizeof(header_->bitmap) * 8) {
                    header_->bitmap[blockIndex / 8] |= (1 << (blockIndex % 8));
                }

                header_->allocatedCount.fetch_add(1, std::memory_order_relaxed);
                header_->totalAllocations.fetch_add(1, std::memory_order_relaxed);
                ++cacheHits_;

                return SharedMemoryPool::Block(
                    cachedOffset,
                    static_cast<uint32_t>(requestedSize),
                    reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader)
                );
            }
        }
        
        ++cacheMisses_;

        // =====================================================================
        // Slow Path - First-fit from free list
        // =====================================================================
        uint32_t prevOffset = UINT32_MAX;
        uint32_t currentOffset = header_->freeHead.load(std::memory_order_acquire);

        while (currentOffset != UINT32_MAX && currentOffset < poolSize_) {
            auto* block = blockAt(currentOffset);
            if (!block->is_valid()) {
                s_lastError = "Corrupted free list";
                return {};
            }

            if (block->blockSize >= blockSize) {
                // Found a suitable block
                uint32_t remaining = block->blockSize - static_cast<uint32_t>(blockSize);

                if (remaining >= MIN_BLOCK_SIZE + sizeof(BlockHeader)) {
                    // Split the block
                    uint32_t newBlockOffset = currentOffset + static_cast<uint32_t>(blockSize);
                    auto* newBlock = blockAt(newBlockOffset);
                    newBlock->magic = BlockHeader::MAGIC;
                    newBlock->size = 0;
                    newBlock->blockSize = remaining;
                    newBlock->nextFree = block->nextFree;

                    block->blockSize = static_cast<uint32_t>(blockSize);
                    block->nextFree = UINT32_MAX;

                    // Update free list
                    if (prevOffset == UINT32_MAX) {
                        header_->freeHead.store(newBlockOffset, std::memory_order_release);
                    } else {
                        blockAt(prevOffset)->nextFree = newBlockOffset;
                    }
                } else {
                    // Use the entire block
                    if (prevOffset == UINT32_MAX) {
                        header_->freeHead.store(block->nextFree, std::memory_order_release);
                    } else {
                        blockAt(prevOffset)->nextFree = block->nextFree;
                    }
                    block->nextFree = UINT32_MAX;
                }

                block->size = static_cast<uint32_t>(requestedSize);
                
                // Mark as allocated in bitmap
                size_t blockIndex = currentOffset / MIN_BLOCK_SIZE;
                if (blockIndex < sizeof(header_->bitmap) * 8) {
                    header_->bitmap[blockIndex / 8] |= (1 << (blockIndex % 8));
                }

                header_->allocatedCount.fetch_add(1, std::memory_order_relaxed);
                header_->totalAllocations.fetch_add(1, std::memory_order_relaxed);

                return SharedMemoryPool::Block(
                    currentOffset,
                    static_cast<uint32_t>(requestedSize),
                    reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader)
                );
            }

            prevOffset = currentOffset;
            currentOffset = block->nextFree;
        }

        s_lastError = "Pool exhausted (no suitable free block)";
        return {};
    }

    void deallocate(uint32_t offset) {
        if (!header_ || offset >= poolSize_) {
            return;
        }

        auto* block = blockAt(offset);
        if (!block->is_valid()) {
            return;
        }

        // Clear bitmap
        size_t blockIndex = offset / MIN_BLOCK_SIZE;
        if (blockIndex < sizeof(header_->bitmap) * 8) {
            header_->bitmap[blockIndex / 8] &= ~(1 << (blockIndex % 8));
        }

        // =====================================================================
        // SPSC LIFO Cache - Push to shared cache for fast reuse by Producer
        // =====================================================================
        bool pushedToCache = header_->spscCache.push(offset, block->blockSize);
        
        if (!pushedToCache) {
            // Cache full - add to free list as fallback
            block->size = 0;
            block->nextFree = header_->freeHead.load(std::memory_order_acquire);
            header_->freeHead.store(offset, std::memory_order_release);
        }

        header_->allocatedCount.fetch_sub(1, std::memory_order_relaxed);
        header_->totalDeallocations.fetch_add(1, std::memory_order_relaxed);
    }

    std::span<uint8_t> get(uint32_t offset, uint32_t size) {
        if (!header_ || offset >= poolSize_) {
            return {};
        }

        auto* block = blockAt(offset);
        if (!block->is_valid()) {
            return {};
        }

        // Verify allocated in bitmap
        size_t blockIndex = offset / MIN_BLOCK_SIZE;
        if (blockIndex < sizeof(header_->bitmap) * 8) {
            if (!(header_->bitmap[blockIndex / 8] & (1 << (blockIndex % 8)))) {
                return {};  // Not allocated
            }
        }

        uint8_t* data = reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader);
        size_t available = block->blockSize - sizeof(BlockHeader);
        size_t actual = (size <= available) ? size : available;

        return {data, actual};
    }

    std::span<const uint8_t> get(uint32_t offset, uint32_t size) const {
        return const_cast<Impl*>(this)->get(offset, size);
    }

    const std::string& name() const { return name_; }
    bool isCreator() const { return isCreator_; }
    size_t poolSize() const { return poolSize_; }

    size_t freeSpace() const {
        if (!header_) return 0;
        
        size_t total = 0;
        uint32_t offset = header_->freeHead.load(std::memory_order_acquire);
        while (offset != UINT32_MAX && offset < poolSize_) {
            auto* block = blockAt(offset);
            if (!block->is_valid()) break;
            total += block->blockSize;
            offset = block->nextFree;
        }
        return total;
    }

    size_t allocatedCount() const {
        if (!header_) return 0;
        return header_->allocatedCount.load(std::memory_order_relaxed);
    }
    
    size_t cacheHits() const { return cacheHits_; }
    size_t cacheMisses() const { return cacheMisses_; }

private:
    uint8_t* dataRegion() {
        return reinterpret_cast<uint8_t*>(header_) + sizeof(PoolHeader);
    }

    BlockHeader* blockAt(uint32_t offset) {
        return reinterpret_cast<BlockHeader*>(dataRegion() + offset);
    }

    const BlockHeader* blockAt(uint32_t offset) const {
        return reinterpret_cast<const BlockHeader*>(
            reinterpret_cast<const uint8_t*>(header_) + sizeof(PoolHeader) + offset
        );
    }

    std::string name_;
    bool isCreator_ = false;
    size_t poolSize_ = 0;

#ifdef _WIN32
    std::unique_ptr<bip::windows_shared_memory> shm_;
#else
    bip::shared_memory_object shm_;
#endif
    bip::mapped_region region_;
    PoolHeader* header_ = nullptr;
    
    // Local statistics (per-process)
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;
};

//=============================================================================
// SharedMemoryPool Public API
//=============================================================================

SharedMemoryPool::SharedMemoryPool() : impl_(std::make_unique<Impl>()) {}

SharedMemoryPool::~SharedMemoryPool() = default;

SharedMemoryPool::SharedMemoryPool(SharedMemoryPool&&) noexcept = default;

SharedMemoryPool& SharedMemoryPool::operator=(SharedMemoryPool&&) noexcept = default;

std::unique_ptr<SharedMemoryPool> SharedMemoryPool::create(std::string_view name, size_t poolSize) {
    auto pool = std::unique_ptr<SharedMemoryPool>(new SharedMemoryPool());
    if (!pool->impl_->create(name, poolSize)) {
        return nullptr;
    }
    return pool;
}

std::unique_ptr<SharedMemoryPool> SharedMemoryPool::open(std::string_view name) {
    auto pool = std::unique_ptr<SharedMemoryPool>(new SharedMemoryPool());
    if (!pool->impl_->open(name)) {
        return nullptr;
    }
    return pool;
}

const std::string& SharedMemoryPool::last_error() {
    return s_lastError;
}

SharedMemoryPool::Block SharedMemoryPool::allocate(size_t size) {
    return impl_->allocate(size);
}

void SharedMemoryPool::deallocate(uint32_t offset) {
    impl_->deallocate(offset);
}

std::span<uint8_t> SharedMemoryPool::get(uint32_t offset, uint32_t size) {
    return impl_->get(offset, size);
}

std::span<const uint8_t> SharedMemoryPool::get(uint32_t offset, uint32_t size) const {
    return impl_->get(offset, size);
}

const std::string& SharedMemoryPool::name() const {
    return impl_->name();
}

bool SharedMemoryPool::is_creator() const {
    return impl_->isCreator();
}

size_t SharedMemoryPool::pool_size() const {
    return impl_->poolSize();
}

size_t SharedMemoryPool::free_space() const {
    return impl_->freeSpace();
}

size_t SharedMemoryPool::allocated_count() const {
    return impl_->allocatedCount();
}

size_t SharedMemoryPool::cache_hits() const {
    return impl_->cacheHits();
}

size_t SharedMemoryPool::cache_misses() const {
    return impl_->cacheMisses();
}

} // namespace ipc
} // namespace lager_ext