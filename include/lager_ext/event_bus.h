// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file event_bus.h
/// @brief EventBus - A lightweight publish/subscribe event system
///
/// This header provides a complete event passing system with:
/// - Static typed events (compile-time type safety, zero-copy via TLS)
/// - Dynamic string events (runtime flexibility)
/// - Multiple event bus instances (local channels)
/// - Global event bus singleton
/// - Connection lifecycle management (RAII)
/// - Guard mechanism for automatic disconnection
///
/// Performance Characteristics (Single-threaded):
/// - O(1) hash-based event lookup for single-event subscriptions
/// - Zero-copy for static typed events via thread-local storage
/// - Minimal allocations during publish (reuses internal buffer)
///
/// Usage:
/// @code
///   // Define events
///   LAGER_EXT_EVENT(DocumentSaved,
///       std::string path;
///       Value content;
///   );
///
///   // Subscribe
///   auto conn = default_bus().subscribe<DocumentSaved>([](const auto& evt) {
///       std::cout << "Saved: " << evt.path << "\n";
///   });
///
///   // Publish
///   default_bus().publish(DocumentSaved{.path = "/tmp/doc.txt", .content = {}});
/// @endcode

#pragma once

#include <lager_ext/lager_ext_config.h>
#include <lager_ext/value_fwd.h>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lager_ext {

// ============================================================================
// Event Definition Macro
// ============================================================================

/// @brief Macro to define a static typed event
#define LAGER_EXT_EVENT(Name, ...)                           \
    struct Name {                                            \
        static constexpr std::string_view event_name{#Name}; \
        __VA_ARGS__                                          \
    }

// ============================================================================
// Concepts
// ============================================================================

/// @brief Concept for static typed events
template <typename T>
concept Event = requires {
    { T::event_name } -> std::convertible_to<std::string_view>;
};

// ============================================================================
// Forward Declarations
// ============================================================================

namespace detail {

class EventBusImpl;
struct Slot;

/// @brief FNV-1a hash for compile-time string hashing
constexpr std::uint64_t fnv1a_hash(std::string_view sv) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (char c : sv) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace detail

// ============================================================================
// Connection Management
// ============================================================================

/// @brief Represents a connection to an event subscription
///
/// Lightweight handle. Does NOT auto-disconnect on destruction.
/// Use ScopedConnection for RAII semantics.
class Connection {
public:
    /// @brief Custom disconnector function type
    using Disconnector = std::function<void()>;

    Connection() noexcept = default;
    
    /// @brief Construct with custom disconnector (for external use like RemoteBus)
    explicit Connection(Disconnector disconnector) noexcept;
    
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() = default;

    /// @brief Disconnect this subscription
    void disconnect();

    /// @brief Check if connected
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return connected(); }

private:
    friend class detail::EventBusImpl;
    Connection(detail::Slot* slot, detail::EventBusImpl* bus) noexcept;

    detail::Slot* slot_ = nullptr;
    detail::EventBusImpl* bus_ = nullptr;
    Disconnector custom_disconnector_;  ///< For external connections (e.g., RemoteBus)
};

/// @brief RAII wrapper for Connection - auto-disconnects on destruction
class ScopedConnection {
public:
    ScopedConnection() noexcept = default;
    ScopedConnection(Connection conn) noexcept;
    ScopedConnection(ScopedConnection&&) noexcept = default;
    ScopedConnection& operator=(ScopedConnection&& other) noexcept;
    ScopedConnection& operator=(Connection conn) noexcept;
    ~ScopedConnection();

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    void reset();
    [[nodiscard]] Connection release() noexcept;
    [[nodiscard]] bool connected() const noexcept { return conn_.connected(); }
    [[nodiscard]] explicit operator bool() const noexcept { return connected(); }

private:
    Connection conn_;
};

/// @brief Container for multiple scoped connections
class ScopedConnectionList {
public:
    ScopedConnectionList() = default;
    ~ScopedConnectionList() = default;
    ScopedConnectionList(ScopedConnectionList&&) = default;
    ScopedConnectionList& operator=(ScopedConnectionList&&) = default;
    ScopedConnectionList(const ScopedConnectionList&) = delete;
    ScopedConnectionList& operator=(const ScopedConnectionList&) = delete;

    void add(Connection conn);
    ScopedConnectionList& operator+=(Connection conn);
    void clear();
    [[nodiscard]] std::size_t size() const noexcept { return connections_.size(); }
    [[nodiscard]] bool empty() const noexcept { return connections_.empty(); }

private:
    std::vector<ScopedConnection> connections_;
};

// ============================================================================
// Event Bus
// ============================================================================

/// @brief An event bus for publish/subscribe messaging
///
/// Thread Safety: NOT thread-safe. Optimized for single-threaded use.
class EventBus {
public:
    EventBus();
    ~EventBus();
    EventBus(EventBus&&) noexcept;
    EventBus& operator=(EventBus&&) noexcept;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // ========================================================================
    // Static Typed Event API (Zero-copy via TLS)
    // ========================================================================

    /// @brief Subscribe to a static typed event
    template <Event Evt, std::invocable<const Evt&> Handler>
    Connection subscribe(Handler&& handler);

    /// @brief Subscribe with guard (auto-disconnect when guard expires)
    template <Event Evt, typename T, std::invocable<const Evt&> Handler>
    Connection subscribe(std::weak_ptr<T> guard, Handler&& handler);

    /// @brief Publish a static typed event
    template <Event Evt>
    void publish(const Evt& evt);

    // ========================================================================
    // Dynamic String Event API
    // ========================================================================

    /// @brief Subscribe to a dynamic string event
    template <std::invocable<const Value&> Handler>
    Connection subscribe(std::string_view event_name, Handler&& handler);

    /// @brief Subscribe to multiple dynamic string events
    template <std::invocable<std::string_view, const Value&> Handler>
    Connection subscribe(std::initializer_list<std::string_view> event_names, Handler&& handler);

    /// @brief Subscribe with filter predicate
    template <std::predicate<std::string_view> Filter, std::invocable<std::string_view, const Value&> Handler>
    Connection subscribe(Filter&& filter, Handler&& handler);

    /// @brief Publish a dynamic string event
    void publish(std::string_view event_name, const Value& payload);
    void publish(std::string_view event_name);

    // ========================================================================
    // Request/Response (Placeholder for IPC)
    // ========================================================================

    std::optional<Value> request(std::string_view event_name, const Value& payload,
                                 std::chrono::milliseconds timeout = std::chrono::seconds(5));

private:
    std::unique_ptr<detail::EventBusImpl> impl_;
};

/// @brief Get the global event bus singleton
inline EventBus& default_bus() {
    static EventBus instance;
    return instance;
}

// ============================================================================
// Implementation Details
// ============================================================================

namespace detail {

/// @brief Type-erased slot for event handlers
using DynamicHandler = std::function<void(std::string_view, const Value&)>;
using FilterFunc = std::function<bool(std::string_view)>;
using GuardFunc = std::function<bool()>;

/// @brief Subscription slot
struct Slot {
    DynamicHandler handler;
    GuardFunc guard;                          // Optional: returns false when expired
    FilterFunc filter;                        // For filter-based subscriptions
    std::uint64_t hash = 0;                   // For single-event optimization
    std::unordered_set<std::uint64_t> hashes; // For multi-event subscriptions
    enum class Type : std::uint8_t { Single, Multi, Filter } type = Type::Single;
    bool active = true;
};

/// @brief Implementation class for EventBus
class EventBusImpl {
public:
    EventBusImpl() = default;
    ~EventBusImpl();

    EventBusImpl(const EventBusImpl&) = delete;
    EventBusImpl& operator=(const EventBusImpl&) = delete;

    Connection subscribe_single(std::uint64_t hash, DynamicHandler handler, GuardFunc guard = nullptr);
    Connection subscribe_multi(std::unordered_set<std::uint64_t> hashes, DynamicHandler handler,
                               GuardFunc guard = nullptr);
    Connection subscribe_filter(FilterFunc filter, DynamicHandler handler, GuardFunc guard = nullptr);

    void publish(std::uint64_t hash, std::string_view event_name, const Value& payload);
    void disconnect(Slot* slot);

private:
    Slot* create_slot();
    void maybe_compact();  // Lazy cleanup of inactive slots

    std::unordered_map<std::uint64_t, std::vector<Slot*>> single_slots_;
    std::vector<Slot*> complex_slots_;
    std::vector<std::unique_ptr<Slot>> all_slots_;
    std::vector<Slot*> dispatch_buffer_; // Reused buffer for publish
    std::size_t disconnect_count_ = 0;   // Counter for lazy cleanup
};

/// @brief Thread-local storage for zero-copy typed event passing
template <Event Evt>
inline thread_local const Evt* current_event_ptr = nullptr;

template <Event Evt>
struct EventScope {
    explicit EventScope(const Evt& evt) noexcept { current_event_ptr<Evt> = &evt; }
    ~EventScope() noexcept { current_event_ptr<Evt> = nullptr; }
    EventScope(const EventScope&) = delete;
    EventScope& operator=(const EventScope&) = delete;
};

} // namespace detail

// ============================================================================
// Inline Implementations
// ============================================================================

inline Connection::Connection(detail::Slot* slot, detail::EventBusImpl* bus) noexcept : slot_(slot), bus_(bus) {}

inline Connection::Connection(Disconnector disconnector) noexcept 
    : custom_disconnector_(std::move(disconnector)) {}

inline Connection::Connection(Connection&& other) noexcept 
    : slot_(other.slot_), bus_(other.bus_), custom_disconnector_(std::move(other.custom_disconnector_)) {
    other.slot_ = nullptr;
    other.bus_ = nullptr;
}

inline Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        slot_ = other.slot_;
        bus_ = other.bus_;
        custom_disconnector_ = std::move(other.custom_disconnector_);
        other.slot_ = nullptr;
        other.bus_ = nullptr;
    }
    return *this;
}

inline void Connection::disconnect() {
    // Custom disconnector (for RemoteBus etc.)
    if (custom_disconnector_) {
        auto d = std::move(custom_disconnector_);
        custom_disconnector_ = nullptr;
        d();
        return;
    }
    // EventBus internal disconnector
    if (bus_ && slot_) {
        bus_->disconnect(slot_);
        slot_ = nullptr;
        bus_ = nullptr;
    }
}

inline bool Connection::connected() const noexcept {
    return (slot_ != nullptr && bus_ != nullptr) || (custom_disconnector_ != nullptr);
}

inline ScopedConnection::ScopedConnection(Connection conn) noexcept : conn_(std::move(conn)) {}

inline ScopedConnection& ScopedConnection::operator=(ScopedConnection&& other) noexcept {
    if (this != &other) {
        conn_.disconnect();
        conn_ = std::move(other.conn_);
    }
    return *this;
}

inline ScopedConnection& ScopedConnection::operator=(Connection conn) noexcept {
    conn_.disconnect();
    conn_ = std::move(conn);
    return *this;
}

inline ScopedConnection::~ScopedConnection() {
    conn_.disconnect();
}

inline void ScopedConnection::reset() {
    conn_.disconnect();
}

inline Connection ScopedConnection::release() noexcept {
    return std::move(conn_);
}

inline void ScopedConnectionList::add(Connection conn) {
    connections_.emplace_back(std::move(conn));
}

inline ScopedConnectionList& ScopedConnectionList::operator+=(Connection conn) {
    add(std::move(conn));
    return *this;
}

inline void ScopedConnectionList::clear() {
    connections_.clear();
}

// ============================================================================
// Template Implementations
// ============================================================================

template <Event Evt, std::invocable<const Evt&> Handler>
Connection EventBus::subscribe(Handler&& handler) {
    constexpr std::uint64_t hash = detail::fnv1a_hash(Evt::event_name);

    auto slot_handler = [h = std::forward<Handler>(handler)](std::string_view, const Value&) {
        if (const Evt* evt_ptr = detail::current_event_ptr<Evt>) {
            h(*evt_ptr);
        }
    };

    return impl_->subscribe_single(hash, std::move(slot_handler));
}

template <Event Evt, typename T, std::invocable<const Evt&> Handler>
Connection EventBus::subscribe(std::weak_ptr<T> guard, Handler&& handler) {
    constexpr std::uint64_t hash = detail::fnv1a_hash(Evt::event_name);

    auto slot_handler = [h = std::forward<Handler>(handler)](std::string_view, const Value&) {
        if (const Evt* evt_ptr = detail::current_event_ptr<Evt>) {
            h(*evt_ptr);
        }
    };

    auto guard_func = [w = std::move(guard)]() { return !w.expired(); };

    return impl_->subscribe_single(hash, std::move(slot_handler), std::move(guard_func));
}

template <Event Evt>
void EventBus::publish(const Evt& evt) {
    constexpr std::uint64_t hash = detail::fnv1a_hash(Evt::event_name);
    detail::EventScope<Evt> scope(evt);
    impl_->publish(hash, Evt::event_name, Value{});
}

template <std::invocable<const Value&> Handler>
Connection EventBus::subscribe(std::string_view event_name, Handler&& handler) {
    const std::uint64_t hash = detail::fnv1a_hash(event_name);

    auto slot_handler = [h = std::forward<Handler>(handler)](std::string_view, const Value& v) { h(v); };

    return impl_->subscribe_single(hash, std::move(slot_handler));
}

template <std::invocable<std::string_view, const Value&> Handler>
Connection EventBus::subscribe(std::initializer_list<std::string_view> event_names, Handler&& handler) {
    std::unordered_set<std::uint64_t> hashes;
    for (auto name : event_names) {
        hashes.insert(detail::fnv1a_hash(name));
    }

    auto slot_handler = [h = std::forward<Handler>(handler)](std::string_view name, const Value& v) { h(name, v); };

    return impl_->subscribe_multi(std::move(hashes), std::move(slot_handler));
}

template <std::predicate<std::string_view> Filter, std::invocable<std::string_view, const Value&> Handler>
Connection EventBus::subscribe(Filter&& filter, Handler&& handler) {
    auto slot_handler = [h = std::forward<Handler>(handler)](std::string_view name, const Value& v) { h(name, v); };

    return impl_->subscribe_filter(std::forward<Filter>(filter), std::move(slot_handler));
}

} // namespace lager_ext