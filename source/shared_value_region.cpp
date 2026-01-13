// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file shared_value_region.cpp
/// @brief Implementation of SharedMemoryRegion using Boost.Interprocess
///
/// This file contains the PIMPL implementation that hides Boost.Interprocess
/// from the public header, allowing users to link lager_ext without needing
/// Boost headers in their include path.

#include <lager_ext/shared_value.h>

// Boost.Interprocess headers - private to this translation unit
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/windows_shared_memory.hpp>

namespace bip = boost::interprocess;

namespace shared_memory {

//==============================================================================
// SharedMemoryRegion::Impl - PIMPL implementation
//==============================================================================

struct SharedMemoryRegion::Impl {
    std::unique_ptr<bip::windows_shared_memory> shm;
    std::unique_ptr<bip::mapped_region> region;
    size_t size = 0;
    bool is_owner = false;
    std::string name;
    size_t local_heap_cursor = 0;

    Impl() = default;

    ~Impl() { close(); }

    void close() {
        region.reset();
        shm.reset();
        size = 0;
        is_owner = false;
    }

    void swap(Impl& other) noexcept {
        std::swap(shm, other.shm);
        std::swap(region, other.region);
        std::swap(size, other.size);
        std::swap(is_owner, other.is_owner);
        std::swap(name, other.name);
        std::swap(local_heap_cursor, other.local_heap_cursor);
    }
};

//==============================================================================
// SharedMemoryRegion - Public interface implementation
//==============================================================================

SharedMemoryRegion::SharedMemoryRegion() : impl_(std::make_unique<Impl>()) {}

SharedMemoryRegion::~SharedMemoryRegion() {
    if (impl_) {
        impl_->close();
    }
}

SharedMemoryRegion::SharedMemoryRegion(SharedMemoryRegion&& other) noexcept : impl_(std::move(other.impl_)) {
    // Ensure other has a valid (empty) impl after move
    other.impl_ = std::make_unique<Impl>();
}

SharedMemoryRegion& SharedMemoryRegion::operator=(SharedMemoryRegion&& other) noexcept {
    if (this != &other) {
        impl_->close();
        impl_.swap(other.impl_);
    }
    return *this;
}

bool SharedMemoryRegion::create(const char* name, size_t size, void* base_address) {
    impl_->close();

    impl_->name = name;
    impl_->size = size;
    impl_->is_owner = true;

    try {
        impl_->shm = std::make_unique<bip::windows_shared_memory>(bip::create_only, name, bip::read_write, size);

        // Try to map to fixed address
        impl_->region = std::make_unique<bip::mapped_region>(*impl_->shm, bip::read_write, 0, size, base_address);

        void* base = impl_->region->get_address();
        if (!base) {
            impl_->close();
            return false;
        }

        // Initialize header
        auto* header = reinterpret_cast<SharedMemoryHeader*>(base);
        header->magic = SharedMemoryHeader::MAGIC;
        header->version = SharedMemoryHeader::CURRENT_VERSION;
        header->fixed_base_address = base;
        header->total_size = size;
        header->heap_offset = sizeof(SharedMemoryHeader);
        header->heap_size = size - sizeof(SharedMemoryHeader);
        header->heap_used = 0;
        header->value_offset = 0;

        return true;
    } catch (const bip::interprocess_exception&) {
        impl_->close();
        return false;
    }
}

bool SharedMemoryRegion::open(const char* name) {
    impl_->close();

    impl_->name = name;
    impl_->is_owner = false;

    try {
        impl_->shm = std::make_unique<bip::windows_shared_memory>(bip::open_only, name, bip::read_write);

        // First map header to get fixed base address and size
        bip::mapped_region temp_region(*impl_->shm, bip::read_only, 0, sizeof(SharedMemoryHeader));

        auto* temp_header = reinterpret_cast<SharedMemoryHeader*>(temp_region.get_address());
        if (temp_header->magic != SharedMemoryHeader::MAGIC) {
            impl_->close();
            return false;
        }

        void* fixed_base = temp_header->fixed_base_address;
        impl_->size = temp_header->total_size;

        // Try to map to the same fixed address
        impl_->region = std::make_unique<bip::mapped_region>(*impl_->shm, bip::read_write, 0, impl_->size, fixed_base);

        void* base = impl_->region->get_address();

        // Verify mapping address
        if (base != fixed_base) {
            impl_->close();
            return false;
        }

        return true;
    } catch (const bip::interprocess_exception&) {
        impl_->close();
        return false;
    }
}

void SharedMemoryRegion::close() {
    impl_->close();
}

bool SharedMemoryRegion::is_valid() const {
    return impl_->region && impl_->region->get_address() != nullptr;
}

void* SharedMemoryRegion::base() const {
    return impl_->region ? impl_->region->get_address() : nullptr;
}

size_t SharedMemoryRegion::size() const {
    return impl_->size;
}

bool SharedMemoryRegion::is_owner() const {
    return impl_->is_owner;
}

SharedMemoryHeader* SharedMemoryRegion::header() const {
    return reinterpret_cast<SharedMemoryHeader*>(base());
}

void* SharedMemoryRegion::heap_base() const {
    void* b = base();
    if (!b)
        return nullptr;
    return reinterpret_cast<char*>(b) + header()->heap_offset;
}

void* SharedMemoryRegion::allocate(size_t size, size_t alignment) {
    if (!base())
        return nullptr;

    auto* h = header();
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

    // Initialize local cursor from shared state if needed
    if (impl_->local_heap_cursor == 0) {
        impl_->local_heap_cursor = h->heap_used;
    }

    size_t current = impl_->local_heap_cursor;
    size_t next = current + aligned_size;

    if (next > h->heap_size) {
        return nullptr; // Out of memory
    }

    impl_->local_heap_cursor = next;
    return reinterpret_cast<char*>(heap_base()) + current;
}

void SharedMemoryRegion::sync_allocation_cursor() {
    if (impl_->local_heap_cursor > 0 && base()) {
        header()->heap_used = impl_->local_heap_cursor;
    }
}

void SharedMemoryRegion::reset_local_cursor() {
    impl_->local_heap_cursor = 0;
}

size_t SharedMemoryRegion::local_cursor() const {
    return impl_->local_heap_cursor;
}

} // namespace shared_memory
