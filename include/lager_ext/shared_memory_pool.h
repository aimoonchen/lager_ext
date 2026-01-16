// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file shared_memory_pool.h
/// @brief Shared memory pool for large IPC data transfers
///
/// This pool solves the 240-byte inline limit of the IPC Channel by providing
/// a separate shared memory region for large payloads.
///
/// Architecture:
/// @code
///     +-------------------------------------------------------------+
///     |                   SharedMemoryPool                          |
///     +-------------------------------------------------------------+
///     | Header (512 bytes)                                          |
///     |   +-- magic, version                                        |
///     |   +-- pool_size                                             |
///     |   +-- free_head (atomic)                                    |
///     |   +-- stats (allocations, deallocations)                    |
///     |   +-- SPSC LIFO Cache (shared between Producer/Consumer)    |
///     +-------------------------------------------------------------+
///     | Block 0: [BlockHeader][User Data...][Padding]               |
///     | Block 1: [BlockHeader][User Data...][Padding]               |
///     | ...                                                         |
///     | Block N: [BlockHeader][User Data...][Padding]               |
///     +-------------------------------------------------------------+
/// @endcode
///
/// SPSC Optimization:
/// - Producer calls allocate(), Consumer calls deallocate()
/// - Shared LIFO cache in shared memory allows O(1) block reuse
/// - Consumer pushes freed blocks to cache head
/// - Producer pops from cache tail, achieving near-100% cache hit rate
///
/// Usage:
/// @code
///     // Process A (Producer/Creator)
///     auto pool = SharedMemoryPool<>::create("MyPool", 1024 * 1024);
///     auto block = pool->allocate(large_data.size());
///     if (block) {
///         memcpy(block->data(), large_data.data(), large_data.size());
///         // Send block.offset() via Channel message
///     }
///
///     // Process B (Consumer/Opener)
///     auto pool = SharedMemoryPool<>::open("MyPool");
///     auto span = pool->get(offset, size);
///     process_data(span);
///     pool->deallocate(offset);  // Returns to shared LIFO cache
/// @endcode

#pragma once

#include <lager_ext/lager_ext_config.h>
#include <lager_ext/api.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace lager_ext {
namespace ipc {

//=============================================================================
// Constants
//=============================================================================

/// Default pool size (1 MB)
inline constexpr size_t DEFAULT_POOL_SIZE = 1024 * 1024;

/// Minimum block size (64 bytes, cache line aligned)
inline constexpr size_t MIN_BLOCK_SIZE = 64;

/// Maximum number of blocks in the pool
inline constexpr size_t MAX_POOL_BLOCKS = 1024;

/// SPSC LIFO cache size (in shared memory, for cross-process reuse)
inline constexpr size_t SPSC_LIFO_CACHE_SIZE = 8;

//=============================================================================
// SharedMemoryPool
//=============================================================================

/// @brief Shared memory pool optimized for SPSC IPC pattern
///
/// This class provides a high-performance allocator for shared memory blocks.
/// It's specifically optimized for the SPSC (Single-Producer Single-Consumer) pattern:
/// - Producer (creator) allocates blocks and writes data
/// - Consumer (opener) reads data and deallocates blocks
///
/// Performance Features:
/// - Shared LIFO cache: Blocks freed by Consumer are immediately available to Producer
/// - O(1) allocation: Near 100% cache hit rate for typical IPC patterns
/// - Cache-line aligned: All structures aligned to 64 bytes to prevent false sharing
/// - Lock-free: Uses atomic operations for SPSC safety without mutexes
///
/// Memory Management:
/// - Uses first-fit free list as fallback when LIFO cache is empty
/// - Blocks are 64-byte aligned for optimal cache performance
class SharedMemoryPool {
public:
    //=========================================================================
    // Block Handle
    //=========================================================================

    /// @brief Handle to an allocated block
    ///
    /// This is a lightweight handle that can be copied/moved.
    /// The block remains valid until deallocate() is called.
    class Block {
    public:
        /// Get the offset of this block (for IPC transfer)
        [[nodiscard]] uint32_t offset() const noexcept { return offset_; }

        /// Get the usable size of this block
        [[nodiscard]] uint32_t size() const noexcept { return size_; }

        /// Get writable pointer to data
        [[nodiscard]] uint8_t* data() noexcept { return data_; }

        /// Get read-only pointer to data
        [[nodiscard]] const uint8_t* data() const noexcept { return data_; }

        /// Get span view of the data
        [[nodiscard]] std::span<uint8_t> span() noexcept { return {data_, size_}; }
        [[nodiscard]] std::span<const uint8_t> span() const noexcept { return {data_, size_}; }

        /// Check if block is valid
        [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    private:
        friend class SharedMemoryPool;
        Block(uint32_t offset, uint32_t size, uint8_t* data) noexcept
            : offset_(offset), size_(size), data_(data) {}
        Block() noexcept : offset_(0), size_(0), data_(nullptr) {}

        uint32_t offset_;
        uint32_t size_;
        uint8_t* data_;
    };

    //=========================================================================
    // Factory Methods
    //=========================================================================

    /// Create a new shared memory pool (Producer side)
    /// @param name Unique pool name
    /// @param pool_size Total size of the pool in bytes
    /// @return Pool instance, nullptr on failure
    static std::unique_ptr<SharedMemoryPool> create(
        std::string_view name,
        size_t pool_size = DEFAULT_POOL_SIZE);

    /// Open an existing shared memory pool (Consumer side)
    /// @param name Pool name (must match creator)
    /// @return Pool instance, nullptr on failure
    static std::unique_ptr<SharedMemoryPool> open(std::string_view name);

    /// Get the last error message
    static const std::string& last_error();

    //=========================================================================
    // Allocation (Producer side)
    //=========================================================================

    /// Allocate a block from the pool
    /// @param size Required size in bytes
    /// @return Block handle, or empty Block if allocation failed
    /// @note First checks SPSC LIFO cache for O(1) reuse of freed blocks
    /// @note Falls back to free list if cache is empty
    [[nodiscard]] Block allocate(size_t size);

    //=========================================================================
    // Deallocation (Consumer side)  
    //=========================================================================

    /// Deallocate a block by offset
    /// @param offset Block offset (from Block::offset())
    /// @note Block is pushed to shared SPSC LIFO cache for fast reuse by Producer
    void deallocate(uint32_t offset);

    //=========================================================================
    // Data Access
    //=========================================================================

    /// Get a view of an allocated block by offset
    /// @param offset Block offset
    /// @param size Expected size (for validation)
    /// @return Span of the block data, empty if invalid
    [[nodiscard]] std::span<uint8_t> get(uint32_t offset, uint32_t size);
    [[nodiscard]] std::span<const uint8_t> get(uint32_t offset, uint32_t size) const;

    //=========================================================================
    // Properties
    //=========================================================================

    /// Get the pool name
    [[nodiscard]] const std::string& name() const;

    /// Check if this is the creator (Producer) side
    [[nodiscard]] bool is_creator() const;

    /// Get total pool size
    [[nodiscard]] size_t pool_size() const;

    /// Get approximate free space (may not be accurate due to fragmentation)
    [[nodiscard]] size_t free_space() const;

    /// Get number of allocated blocks
    [[nodiscard]] size_t allocated_count() const;

    /// Get SPSC LIFO cache hit statistics (for performance tuning)
    [[nodiscard]] size_t cache_hits() const;

    /// Get SPSC LIFO cache miss statistics (for performance tuning)
    [[nodiscard]] size_t cache_misses() const;

    //=========================================================================
    // Lifecycle
    //=========================================================================

    ~SharedMemoryPool();

    SharedMemoryPool(SharedMemoryPool&&) noexcept;
    SharedMemoryPool& operator=(SharedMemoryPool&&) noexcept;

    SharedMemoryPool(const SharedMemoryPool&) = delete;
    SharedMemoryPool& operator=(const SharedMemoryPool&) = delete;

private:
    SharedMemoryPool();
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ipc
} // namespace lager_ext