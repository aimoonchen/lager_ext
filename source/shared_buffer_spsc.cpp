// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

#include <lager_ext/shared_buffer_spsc.h>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <atomic>

namespace lager_ext {
namespace ipc {

namespace bip = boost::interprocess;

//=============================================================================
// Thread-local error storage
//=============================================================================

namespace {

std::string& get_last_error() {
    static thread_local std::string error;
    return error;
}

} // anonymous namespace

//=============================================================================
// SharedBufferBase::Impl - PIMPL implementation
//=============================================================================

/// Header structure in shared memory (64-byte aligned)
/// 
/// State encoding: state = (version << 1) | read_index
/// - Bit 0: read_index (0 or 1, indicates active buffer)
/// - Bits 1-63: version (increments by 1 each write, so bit 0 flips)
struct alignas(SPSC_CACHE_LINE_SIZE) SharedMemoryHeader {
    std::atomic<uint64_t> state{0};  // Combined version + index
    uint32_t data_size{0};           // sizeof(T), for validation
    uint32_t flags{0};               // Reserved
    char padding[48];                // Pad to 64 bytes
};

static_assert(sizeof(SharedMemoryHeader) == SPSC_CACHE_LINE_SIZE);

class SharedBufferBase::Impl {
public:
    std::string name;
    bool is_producer{false};
    bool is_owner{false};  // Whether this instance should cleanup on destruction
    size_t data_size{0};
    
    bip::shared_memory_object shm;
    bip::mapped_region region;
    
    SharedMemoryHeader* header{nullptr};
    void* data_region{nullptr};  // Points to buffer area (after header)
};

//=============================================================================
// SharedBufferBase Implementation
//=============================================================================

SharedBufferBase::SharedBufferBase() : impl_(std::make_unique<Impl>()) {}

SharedBufferBase::~SharedBufferBase() {
    if (impl_ && impl_->is_owner && !impl_->name.empty()) {
        // This instance is owner, remove shared memory
        bip::shared_memory_object::remove(impl_->name.c_str());
    }
}

SharedBufferBase::SharedBufferBase(SharedBufferBase&&) noexcept = default;
SharedBufferBase& SharedBufferBase::operator=(SharedBufferBase&&) noexcept = default;

std::unique_ptr<SharedBufferBase> SharedBufferBase::create(
    std::string_view name, 
    size_t data_size,
    size_t total_size) 
{
    try {
        auto instance = std::unique_ptr<SharedBufferBase>(new SharedBufferBase());
        instance->impl_->name = std::string(name);
        instance->impl_->is_producer = true;
        instance->impl_->is_owner = true;  // Producer is owner by default
        instance->impl_->data_size = data_size;

        // Remove any existing shared memory with this name
        bip::shared_memory_object::remove(instance->impl_->name.c_str());

        // Create shared memory
        instance->impl_->shm = bip::shared_memory_object(
            bip::create_only,
            instance->impl_->name.c_str(),
            bip::read_write
        );

        // Set size
        instance->impl_->shm.truncate(static_cast<bip::offset_t>(total_size));

        // Map the region
        instance->impl_->region = bip::mapped_region(
            instance->impl_->shm,
            bip::read_write
        );

        // Initialize header
        void* addr = instance->impl_->region.get_address();
        instance->impl_->header = new (addr) SharedMemoryHeader{};
        instance->impl_->header->data_size = static_cast<uint32_t>(data_size);
        
        // Data region starts after header
        instance->impl_->data_region = static_cast<char*>(addr) + sizeof(SharedMemoryHeader);

        return instance;
    }
    catch (const bip::interprocess_exception& e) {
        get_last_error() = std::string("Failed to create shared buffer: ") + e.what();
        return nullptr;
    }
    catch (const std::exception& e) {
        get_last_error() = std::string("Failed to create shared buffer: ") + e.what();
        return nullptr;
    }
}

std::unique_ptr<SharedBufferBase> SharedBufferBase::open(
    std::string_view name,
    size_t data_size) 
{
    try {
        auto instance = std::unique_ptr<SharedBufferBase>(new SharedBufferBase());
        instance->impl_->name = std::string(name);
        instance->impl_->is_producer = false;
        instance->impl_->data_size = data_size;

        // Open existing shared memory
        instance->impl_->shm = bip::shared_memory_object(
            bip::open_only,
            instance->impl_->name.c_str(),
            bip::read_write  // Need write for atomic operations
        );

        // Map the region
        instance->impl_->region = bip::mapped_region(
            instance->impl_->shm,
            bip::read_write
        );

        // Initialize pointers
        void* addr = instance->impl_->region.get_address();
        instance->impl_->header = static_cast<SharedMemoryHeader*>(addr);
        
        // Validate data size
        if (instance->impl_->header->data_size != data_size) {
            get_last_error() = "Data type size mismatch";
            return nullptr;
        }

        instance->impl_->data_region = static_cast<char*>(addr) + sizeof(SharedMemoryHeader);

        return instance;
    }
    catch (const bip::interprocess_exception& e) {
        get_last_error() = std::string("Failed to open shared buffer: ") + e.what();
        return nullptr;
    }
    catch (const std::exception& e) {
        get_last_error() = std::string("Failed to open shared buffer: ") + e.what();
        return nullptr;
    }
}

const std::string& SharedBufferBase::last_error() {
    return get_last_error();
}

const std::string& SharedBufferBase::name() const {
    return impl_->name;
}

bool SharedBufferBase::is_producer() const {
    return impl_->is_producer;
}

void* SharedBufferBase::data_region() {
    return impl_->data_region;
}

const void* SharedBufferBase::data_region() const {
    return impl_->data_region;
}

void* SharedBufferBase::state_ptr() {
    return &impl_->header->state;
}

const void* SharedBufferBase::state_ptr() const {
    return &impl_->header->state;
}

bool SharedBufferBase::is_owner() const {
    return impl_->is_owner;
}

void SharedBufferBase::take_ownership() {
    impl_->is_owner = true;
}

void SharedBufferBase::release_ownership() {
    impl_->is_owner = false;
}

} // namespace ipc
} // namespace lager_ext
