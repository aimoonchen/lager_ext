// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file event_bus_ipc.h
/// @brief RemoteBus - Cross-process event passing via IPC
///
/// This is an OPTIONAL extension that enables cross-process event passing.
/// Only available when LAGER_EXT_ENABLE_IPC is defined.
///
/// Architecture:
/// @code
///   Process A                              Process B
///   ┌─────────────────┐                    ┌─────────────────┐
///   │  EventBus       │                    │  EventBus       │
///   │  (local events) │                    │  (local events) │
///   └────────┬────────┘                    └────────▲────────┘
///            │                                      │
///            ▼                                      │
///   ┌─────────────────┐                    ┌────────┴────────┐
///   │  RemoteBus      │ ================== │  RemoteBus      │
///   │  (serialize)    │   Shared Memory    │  (deserialize)  │
///   └─────────────────┘                    └─────────────────┘
/// @endcode
///
/// Usage:
/// @code
///   LAGER_EXT_IPC_EVENT(DocumentSaved,
///       std::string doc_id;
///       std::string path;
///   ,
///       return Value::map({{"doc_id", evt.doc_id}, {"path", evt.path}});
///   ,
///       return DocumentSaved{
///           .doc_id = v.at("doc_id").as<std::string>(),
///           .path = v.at("path").as<std::string>()
///       };
///   );
///
///   RemoteBus remote("my_channel", default_bus());
///   remote.subscribe_remote<DocumentSaved>([](const auto& evt) { ... });
///   remote.publish_remote(DocumentSaved{...});
///   remote.poll();  // Call in event loop
/// @endcode
///
/// Design Notes:
/// - Single-process mode has ZERO overhead (this file is never included)
/// - Events must be serializable to work across processes
/// - The remote bus polls the IPC channel; it does not run a separate thread

#pragma once

#ifndef LAGER_EXT_ENABLE_IPC
#error "IPC module not enabled. Build with -DLAGER_EXT_ENABLE_IPC=ON"
#endif

#include <lager_ext/event_bus.h>
#include <lager_ext/ipc.h>
#include <lager_ext/serialization.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lager_ext {

// ============================================================================
// IPC Event Trait
// ============================================================================

/// @brief Trait for IPC-serializable events
template <typename T>
struct IpcEventTrait {
    static constexpr bool is_ipc_event = false;
};

/// @brief Concept for IPC-enabled events
template <typename T>
concept IpcEvent = Event<T> && IpcEventTrait<T>::is_ipc_event;

// ============================================================================
// IPC Event Definition Macro
// ============================================================================

/// @brief Define an IPC-enabled event with serialization
#define LAGER_EXT_IPC_EVENT(Name, Fields, SerializeBody, DeserializeBody) \
    struct Name {                                                         \
        static constexpr std::string_view event_name{#Name};              \
        Fields                                                            \
    };                                                                    \
    template <>                                                           \
    struct lager_ext::IpcEventTrait<Name> {                               \
        static constexpr bool is_ipc_event = true;                        \
        static lager_ext::Value serialize(const Name& evt) {              \
            SerializeBody                                                 \
        }                                                                 \
        static Name deserialize(const lager_ext::Value& v) {              \
            DeserializeBody                                               \
        }                                                                 \
    }

// ============================================================================
// IPC Protocol Constants
// ============================================================================

namespace detail {

constexpr uint32_t IPC_EVT_BASE = 0xFFFF0000;
constexpr uint32_t IPC_EVT_EVENT = IPC_EVT_BASE + 1;
constexpr uint32_t IPC_EVT_REQUEST = IPC_EVT_BASE + 2;
constexpr uint32_t IPC_EVT_RESPONSE = IPC_EVT_BASE + 3;

} // namespace detail

// ============================================================================
// Remote Bus
// ============================================================================

/// @brief Cross-process event bus via shared memory IPC
///
/// Thread Safety: NOT thread-safe. Call poll() from the same thread as publish.
class LAGER_EXT_API RemoteBus {
public:
    /// @brief Role in the IPC connection
    enum class Role {
        Server, ///< Creates the channel
        Client, ///< Connects to existing channel
        Peer    ///< Bidirectional (creates ChannelPair)
    };

    RemoteBus(std::string_view channel_name, EventBus& bus, Role role = Role::Peer,
              std::size_t capacity = ipc::DEFAULT_CAPACITY);
    ~RemoteBus();

    RemoteBus(const RemoteBus&) = delete;
    RemoteBus& operator=(const RemoteBus&) = delete;
    RemoteBus(RemoteBus&&) noexcept;
    RemoteBus& operator=(RemoteBus&&) noexcept;

    // ========================================================================
    // Publishing
    // ========================================================================

    /// @brief Publish event to remote only
    template <IpcEvent Evt>
    bool publish_remote(const Evt& evt) {
        return publish_remote(Evt::event_name, IpcEventTrait<Evt>::serialize(evt));
    }

    /// @brief Publish event to both local and remote
    template <IpcEvent Evt>
    bool broadcast(const Evt& evt) {
        bus_ref().publish(evt);
        return publish_remote(evt);
    }

    bool publish_remote(std::string_view event_name, const Value& payload);
    bool broadcast(std::string_view event_name, const Value& payload);

    // ========================================================================
    // Subscribing
    // ========================================================================

    /// @brief Subscribe to remote typed event
    template <IpcEvent Evt, std::invocable<const Evt&> Handler>
    Connection subscribe_remote(Handler&& handler) {
        return subscribe_remote_impl(Evt::event_name, [h = std::forward<Handler>(handler)](const Value& v) {
            h(IpcEventTrait<Evt>::deserialize(v));
        });
    }

    /// @brief Subscribe to remote dynamic event
    template <std::invocable<const Value&> Handler>
    Connection subscribe_remote(std::string_view event_name, Handler&& handler) {
        return subscribe_remote_impl(event_name, std::forward<Handler>(handler));
    }

    /// @brief Bridge remote events to local bus
    template <IpcEvent Evt>
    Connection bridge_to_local() {
        return subscribe_remote<Evt>([this](const Evt& evt) { bus_ref().publish(evt); });
    }

    Connection bridge_to_local(std::string_view event_name);

    // ========================================================================
    // Polling
    // ========================================================================

    /// @brief Poll for incoming events (non-blocking)
    std::size_t poll();

    /// @brief Poll with timeout
    std::size_t poll(std::chrono::milliseconds timeout);

    // ========================================================================
    // Request/Response
    // ========================================================================

    std::optional<Value> request(std::string_view event_name, const Value& payload,
                                 std::chrono::milliseconds timeout = std::chrono::seconds(5));

    template <std::invocable<const Value&> Handler>
        requires std::convertible_to<std::invoke_result_t<Handler, const Value&>, Value>
    Connection on_request(std::string_view event_name, Handler&& handler) {
        return on_request_impl(event_name, std::forward<Handler>(handler));
    }

    // ========================================================================
    // Properties
    // ========================================================================

    [[nodiscard]] bool connected() const;
    [[nodiscard]] const std::string& channel_name() const;
    [[nodiscard]] const std::string& last_error() const;

private:
    Connection subscribe_remote_impl(std::string_view event_name, std::function<void(const Value&)> handler);
    Connection on_request_impl(std::string_view event_name, std::function<Value(const Value&)> handler);
    EventBus& bus_ref();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lager_ext