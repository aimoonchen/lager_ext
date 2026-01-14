// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file shared_buffer_spsc.h
/// @brief High-performance SPSC (Single Producer Single Consumer) shared memory buffer
///
/// This is an optimized double-buffer implementation for cross-process data synchronization:
/// - Lock-free: Only atomic operations, no mutexes or locks
/// - Zero-copy read: Reader gets direct reference to shared memory
/// - Deterministic latency: Reader never waits or retries
/// - Cache-optimized: 64-byte alignment to avoid false sharing
///
/// Usage:
/// @code
///     // Process A (Producer) - creates the buffer
///     auto buffer = SharedBufferSPSC<CameraState>::create("CameraSync");
///     buffer->write(camera_state);
///     // Or use RAII guard for in-place modification:
///     {
///         auto guard = buffer->write_guard();
///         guard->position = new_pos;
///         guard->rotation = new_rot;
///     }  // Auto-commit on destruction
///
///     // Process B (Consumer) - opens existing buffer
///     auto buffer = SharedBufferSPSC<CameraState>::open("CameraSync");
///     const auto& state = buffer->read();  // Zero-copy read
///     // Or check for updates:
///     if (buffer->has_update()) {
///         CameraState state;
///         buffer->try_read(state);
///     }
/// @endcode

#pragma once

#include "lager_ext_config.h"
#include "api.h"

#include <atomic>
#include <bit>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lager_ext {
namespace ipc {

//=============================================================================
// Constants
//=============================================================================

/// Cache line size for padding (avoid false sharing)
inline constexpr size_t SPSC_CACHE_LINE_SIZE = 64;

//=============================================================================
// SharedBufferBase - Non-template implementation (hides Boost dependency)
//=============================================================================

/// @brief Non-template base class for shared buffer implementation
/// @note This class handles all Boost.Interprocess operations internally
class LAGER_EXT_API SharedBufferBase {
public:
    /// Create shared buffer (producer)
    /// @param name Unique buffer name
    /// @param data_size Size of the data type
    /// @param total_size Total shared memory size (header + 2 buffers)
    /// @return Buffer instance, nullptr on failure
    static std::unique_ptr<SharedBufferBase> create(
        std::string_view name, 
        size_t data_size,
        size_t total_size);
    
    /// Open existing shared buffer (consumer)
    /// @param name Buffer name
    /// @param data_size Expected size of the data type
    /// @return Buffer instance, nullptr on failure
    static std::unique_ptr<SharedBufferBase> open(
        std::string_view name,
        size_t data_size);
    
    /// Get the last error message
    static const std::string& last_error();
    
    /// Get buffer name
    const std::string& name() const;
    
    /// Check if this is the producer side
    bool is_producer() const;
    
    /// Check if this instance owns the shared memory (will cleanup on destruction)
    bool is_owner() const;
    
    /// Take ownership (this instance will cleanup shared memory on destruction)
    /// Useful for one-shot transfers where consumer should cleanup after receiving
    void take_ownership();
    
    /// Release ownership (this instance will not cleanup on destruction)
    void release_ownership();
    
    /// Get pointer to the raw data region (after header)
    void* data_region();
    const void* data_region() const;
    
    /// Get the internal state for atomic operations
    /// @return Pointer to the atomic state variable in shared memory
    void* state_ptr();
    const void* state_ptr() const;
    
    ~SharedBufferBase();
    SharedBufferBase(SharedBufferBase&&) noexcept;
    SharedBufferBase& operator=(SharedBufferBase&&) noexcept;
    
    SharedBufferBase(const SharedBufferBase&) = delete;
    SharedBufferBase& operator=(const SharedBufferBase&) = delete;

private:
    SharedBufferBase();
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// SharedBufferSPSC - Type-safe template wrapper
//=============================================================================

/// @brief High-performance SPSC shared memory buffer with double-buffering
///
/// This class provides lock-free data synchronization between two processes:
/// - ONE producer process writes data
/// - ONE consumer process reads data
///
/// Double-buffer mechanism:
/// - Two buffers exist in shared memory (Buffer A and Buffer B)
/// - Producer writes to the inactive buffer, then atomically swaps
/// - Consumer always reads from the active buffer (consistent data)
///
/// Performance characteristics:
/// - write(): ~30-50 ns + memcpy(sizeof(T))
/// - write_guard(): ~30-50 ns (zero-copy, commit on destruction)
/// - read(): ~20-30 ns (returns reference, zero-copy)
/// - has_update(): ~5-10 ns (relaxed atomic, no memory barrier)
///
/// @tparam T Data type (must be trivially copyable for shared memory safety)
template<typename T>
    requires std::is_trivially_copyable_v<T>
class SharedBufferSPSC {
public:
    //=========================================================================
    // Factory Methods
    //=========================================================================

    /// Create the shared buffer as producer (creates shared memory)
    /// @param name Unique buffer name for shared memory identification
    /// @return Buffer instance, nullptr on failure
    static std::unique_ptr<SharedBufferSPSC> create(std::string_view name) {
        auto base = SharedBufferBase::create(name, sizeof(T), shared_memory_size());
        if (!base) return nullptr;
        
        auto instance = std::unique_ptr<SharedBufferSPSC>(new SharedBufferSPSC());
        instance->base_ = std::move(base);
        instance->init_pointers();
        
        // Initialize both buffers to zero
        std::memset(instance->buffers_, 0, 2 * sizeof(BufferSlot));
        
        return instance;
    }

    /// Open an existing shared buffer as consumer
    /// @param name Buffer name (must match producer)
    /// @return Buffer instance, nullptr on failure
    static std::unique_ptr<SharedBufferSPSC> open(std::string_view name) {
        auto base = SharedBufferBase::open(name, sizeof(T));
        if (!base) return nullptr;
        
        auto instance = std::unique_ptr<SharedBufferSPSC>(new SharedBufferSPSC());
        instance->base_ = std::move(base);
        instance->init_pointers();
        
        return instance;
    }

    //=========================================================================
    // Writer API (Producer only)
    //=========================================================================

    /// Write data to the buffer (copies data)
    /// @param data Data to write
    /// @note This performs a memcpy. For zero-copy writes, use write_guard()
    void write(const T& data) {
        uint64_t old_state = state_->load(std::memory_order_relaxed);
        uint32_t write_idx = 1 - static_cast<uint32_t>(old_state & 1);
        std::memcpy(&buffers_[write_idx].data, &data, sizeof(T));
        state_->store(old_state + 1, std::memory_order_release);
    }

    /// RAII write guard for zero-copy in-place modification
    /// @note Data is automatically committed when the guard is destroyed
    class WriteGuard {
    public:
        /// Access the buffer for modification
        T* operator->() noexcept { return buffer_; }
        T& operator*() noexcept { return *buffer_; }
        
        /// Destructor commits the write (atomic swap)
        ~WriteGuard() {
            if (owner_) {
                owner_->state_->store(old_state_ + 1, std::memory_order_release);
            }
        }

        // Non-copyable
        WriteGuard(const WriteGuard&) = delete;
        WriteGuard& operator=(const WriteGuard&) = delete;
        
        // Movable (safe in SPSC single-producer context)
        WriteGuard(WriteGuard&& other) noexcept
            : owner_(std::exchange(other.owner_, nullptr))
            , buffer_(other.buffer_)
            , old_state_(other.old_state_) {}
        
        WriteGuard& operator=(WriteGuard&& other) noexcept {
            if (this != &other) {
                // Commit current write if active
                if (owner_) {
                    owner_->state_->store(old_state_ + 1, std::memory_order_release);
                }
                owner_ = std::exchange(other.owner_, nullptr);
                buffer_ = other.buffer_;
                old_state_ = other.old_state_;
            }
            return *this;
        }

    private:
        friend class SharedBufferSPSC;
        WriteGuard(SharedBufferSPSC* owner, T* buffer, uint64_t old_state) noexcept
            : owner_(owner), buffer_(buffer), old_state_(old_state) {}
        
        SharedBufferSPSC* owner_;
        T* buffer_;
        uint64_t old_state_;
    };

    /// Get a write guard for zero-copy modification
    /// @return RAII guard that commits on destruction
    /// @note Only call from the producer process!
    [[nodiscard]] WriteGuard write_guard() {
        uint64_t old_state = state_->load(std::memory_order_relaxed);
        uint32_t write_idx = 1 - static_cast<uint32_t>(old_state & 1);
        return WriteGuard(this, &buffers_[write_idx].data, old_state);
    }

    //=========================================================================
    // Reader API (Consumer only)
    //=========================================================================

    /// Read the current data (zero-copy, returns reference)
    /// @return Const reference to the active buffer
    /// @note The reference is valid until the next write
    const T& read() const {
        uint64_t state = state_->load(std::memory_order_acquire);
        uint32_t read_idx = static_cast<uint32_t>(state & 1);
        return buffers_[read_idx].data;
    }

    /// Try to read new data (only if updated since last read)
    /// @param out Output parameter to receive the data
    /// @return true if new data was read, false if no update
    bool try_read(T& out) const {
        uint64_t state = state_->load(std::memory_order_acquire);
        if (state == last_read_state_) {
            return false;
        }
        uint32_t read_idx = static_cast<uint32_t>(state & 1);
        out = buffers_[read_idx].data;
        last_read_state_ = state;
        return true;
    }

    /// Get the current version number
    /// @return Monotonically increasing version (increments on each write)
    uint64_t version() const {
        uint64_t state = state_->load(std::memory_order_acquire);
        return state >> 1;
    }

    /// Check if there's new data since the last try_read()
    /// @return true if version has changed
    /// @note Uses relaxed memory order for minimal overhead
    bool has_update() const {
        uint64_t state = state_->load(std::memory_order_relaxed);
        return state != last_read_state_;
    }

    /// Reset the update tracking (next has_update() will return true if any data exists)
    void reset_update_tracking() const {
        last_read_state_ = 0;
    }

    //=========================================================================
    // Properties
    //=========================================================================

    /// Get the buffer name
    const std::string& name() const { return base_->name(); }

    /// Check if this is the producer side
    bool is_producer() const { return base_->is_producer(); }

    /// Get the last error message (if create/open failed)
    static const std::string& last_error() { return SharedBufferBase::last_error(); }
    
    //=========================================================================
    // Ownership Control
    //=========================================================================
    
    /// Check if this instance owns the shared memory (will cleanup on destruction)
    /// @return true if this instance is the owner
    bool is_owner() const { return base_->is_owner(); }
    
    /// Take ownership of the shared memory
    /// After this call, this instance will cleanup shared memory on destruction.
    /// Useful for one-shot transfers where consumer should cleanup after receiving.
    /// 
    /// Example (one-shot transfer pattern):
    /// @code
    ///     // Producer side
    ///     auto buffer = SharedBufferSPSC<T>::create("OneShot");
    ///     buffer->release_ownership();  // Don't cleanup on destruction
    ///     buffer->write(data);
    ///     // Producer can now exit safely
    /// 
    ///     // Consumer side
    ///     auto buffer = SharedBufferSPSC<T>::open("OneShot");
    ///     buffer->take_ownership();  // Consumer will cleanup
    ///     auto data = buffer->read();
    ///     // Shared memory cleaned up when consumer's buffer is destroyed
    /// @endcode
    void take_ownership() { base_->take_ownership(); }
    
    /// Release ownership (this instance will not cleanup on destruction)
    /// Call this on the producer if you want the consumer to handle cleanup
    void release_ownership() { base_->release_ownership(); }

    //=========================================================================
    // Lifecycle
    //=========================================================================

    ~SharedBufferSPSC() = default;

    SharedBufferSPSC(SharedBufferSPSC&&) noexcept = default;
    SharedBufferSPSC& operator=(SharedBufferSPSC&&) noexcept = default;

    // Non-copyable
    SharedBufferSPSC(const SharedBufferSPSC&) = delete;
    SharedBufferSPSC& operator=(const SharedBufferSPSC&) = delete;

private:
    //=========================================================================
    // Internal Types
    //=========================================================================

    /// Buffer slot with cache line alignment
    struct alignas(SPSC_CACHE_LINE_SIZE) BufferSlot {
        T data;
    };

    /// Align size up to cache line boundary (C++20 bit operations)
    static constexpr size_t align_to_cache_line(size_t size) noexcept {
        // Round up to next multiple of 64 using bit manipulation
        return (size + SPSC_CACHE_LINE_SIZE - 1) & ~(SPSC_CACHE_LINE_SIZE - 1);
    }

    /// Total shared memory layout size
    static constexpr size_t shared_memory_size() noexcept {
        // Header (64 bytes) + 2 buffer slots (each aligned to 64 bytes)
        // Use std::bit_ceil for power-of-2 alignment when sizeof(T) < 64
        constexpr size_t slot_size = sizeof(T) <= SPSC_CACHE_LINE_SIZE 
            ? SPSC_CACHE_LINE_SIZE 
            : align_to_cache_line(sizeof(T));
        return SPSC_CACHE_LINE_SIZE + 2 * slot_size;
    }

    //=========================================================================
    // Internal Methods
    //=========================================================================

    SharedBufferSPSC() = default;

    void init_pointers() {
        // State is at the beginning of shared memory
        state_ = static_cast<std::atomic<uint64_t>*>(base_->state_ptr());
        
        // Buffers start after the header (64 bytes)
        void* data_region = base_->data_region();
        buffers_ = static_cast<BufferSlot*>(data_region);
    }

    //=========================================================================
    // Member Variables
    //=========================================================================

    std::unique_ptr<SharedBufferBase> base_;
    
    // Direct pointers into shared memory (cached for performance)
    std::atomic<uint64_t>* state_ = nullptr;
    BufferSlot* buffers_ = nullptr;

    // Reader-side cached state for has_update() / try_read()
    mutable uint64_t last_read_state_ = 0;
};

} // namespace ipc
} // namespace lager_ext
