// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file event_bus.cpp
/// @brief Implementation of EventBus

#include <lager_ext/event_bus.h>
#include <lager_ext/value.h>

#include <algorithm>

namespace lager_ext {

// ============================================================================
// EventBus Implementation
// ============================================================================

EventBus::EventBus() : impl_(std::make_unique<detail::EventBusImpl>()) {}

EventBus::~EventBus() = default;

EventBus::EventBus(EventBus&&) noexcept = default;
EventBus& EventBus::operator=(EventBus&&) noexcept = default;

void EventBus::publish(std::string_view event_name, const Value& payload) {
    const std::uint64_t hash = detail::fnv1a_hash(event_name);
    impl_->publish(hash, event_name, payload);
}

void EventBus::publish(std::string_view event_name) {
    publish(event_name, Value{});
}

std::optional<Value> EventBus::request(std::string_view /*event_name*/, const Value& /*payload*/,
                                       std::chrono::milliseconds /*timeout*/) {
    // Placeholder for future IPC integration
    // For local-only use, this is not typically needed
    return std::nullopt;
}

// ============================================================================
// EventBusImpl Implementation
// ============================================================================

namespace detail {

EventBusImpl::~EventBusImpl() = default;

Slot* EventBusImpl::create_slot() {
    auto slot = std::make_unique<Slot>();
    auto* raw = slot.get();
    all_slots_.push_back(std::move(slot));
    return raw;
}

Connection EventBusImpl::subscribe_single(std::uint64_t hash, DynamicHandler handler, GuardFunc guard) {
    auto* slot = create_slot();
    slot->handler = std::move(handler);
    slot->guard = std::move(guard);
    slot->hash = hash;
    slot->type = Slot::Type::Single;

    single_slots_[hash].push_back(slot);
    return Connection(slot, this);
}

Connection EventBusImpl::subscribe_multi(std::unordered_set<std::uint64_t> hashes, DynamicHandler handler,
                                         GuardFunc guard) {
    auto* slot = create_slot();
    slot->handler = std::move(handler);
    slot->guard = std::move(guard);
    slot->hashes = std::move(hashes);
    slot->type = Slot::Type::Multi;

    complex_slots_.push_back(slot);
    return Connection(slot, this);
}

Connection EventBusImpl::subscribe_filter(FilterFunc filter, DynamicHandler handler, GuardFunc guard) {
    auto* slot = create_slot();
    slot->handler = std::move(handler);
    slot->guard = std::move(guard);
    slot->filter = std::move(filter);
    slot->type = Slot::Type::Filter;

    complex_slots_.push_back(slot);
    return Connection(slot, this);
}

void EventBusImpl::publish(std::uint64_t hash, std::string_view event_name, const Value& payload) {
    // Reuse dispatch buffer to avoid allocation
    dispatch_buffer_.clear();

    // Collect single-event subscriptions (O(1) lookup)
    if (auto it = single_slots_.find(hash); it != single_slots_.end()) {
        for (auto* slot : it->second) {
            if (slot->active && (!slot->guard || slot->guard())) {
                dispatch_buffer_.push_back(slot);
            }
        }
    }

    // Collect complex subscriptions (multi-event and filter-based)
    for (auto* slot : complex_slots_) {
        if (!slot->active || (slot->guard && !slot->guard())) {
            continue;
        }

        if (slot->type == Slot::Type::Multi) {
            if (slot->hashes.contains(hash)) {
                dispatch_buffer_.push_back(slot);
            }
        } else if (slot->type == Slot::Type::Filter) {
            if (slot->filter && slot->filter(event_name)) {
                dispatch_buffer_.push_back(slot);
            }
        }
    }

    // Dispatch to all matched handlers
    for (auto* slot : dispatch_buffer_) {
        if (slot->handler) {
            slot->handler(event_name, payload);
        }
    }
}

void EventBusImpl::disconnect(Slot* slot) {
    if (!slot || !slot->active) {
        return;
    }

    slot->active = false;
    slot->handler = nullptr;

    // Remove from single_slots_
    if (slot->type == Slot::Type::Single) {
        if (auto it = single_slots_.find(slot->hash); it != single_slots_.end()) {
            auto& vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), slot), vec.end());
            if (vec.empty()) {
                single_slots_.erase(it);
            }
        }
    }

    // Remove from complex_slots_
    if (slot->type == Slot::Type::Multi || slot->type == Slot::Type::Filter) {
        complex_slots_.erase(std::remove(complex_slots_.begin(), complex_slots_.end(), slot), complex_slots_.end());
    }

    // Lazy cleanup: compact the all_slots_ vector occasionally
    maybe_compact();
}

void EventBusImpl::maybe_compact() {
    // Compact every 100 disconnects to avoid memory leak from zombie slots
    constexpr std::size_t COMPACT_INTERVAL = 100;
    
    if (++disconnect_count_ % COMPACT_INTERVAL != 0) {
        return;
    }

    // Remove all inactive slots from all_slots_
    std::erase_if(all_slots_, [](const auto& slot) { 
        return !slot || !slot->active; 
    });
}

} // namespace detail

} // namespace lager_ext