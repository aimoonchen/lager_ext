// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file event_demo.cpp
/// @brief Demonstrates the usage of lager_ext::EventBus and RemoteBus
///
/// This example shows:
/// - Static typed events with LAGER_EXT_EVENT macro
/// - Dynamic string events
/// - Multiple subscriptions
/// - Guard mechanism
/// - ScopedConnection and ScopedConnectionList
/// - RemoteBus for cross-process messaging (when LAGER_EXT_ENABLE_IPC is defined)

#include <lager_ext/event_bus.h>
#include <lager_ext/serialization.h>
#include <lager_ext/value.h>

#ifdef LAGER_EXT_ENABLE_IPC
#include <lager_ext/event_bus_ipc.h>
#endif

#include <iostream>
#include <memory>
#include <string>

using namespace lager_ext;

// ============================================================================
// Define Static Typed Events (Local only - no serialization needed)
// ============================================================================

LAGER_EXT_EVENT(DocumentCreated, std::string doc_id; std::string title;);

LAGER_EXT_EVENT(DocumentSaved, std::string doc_id; std::string path;);

LAGER_EXT_EVENT(PropertyChanged, std::string property_name; int old_value; int new_value;);

// ============================================================================
// Define IPC Events (with serialization for cross-process)
// ============================================================================

#ifdef LAGER_EXT_ENABLE_IPC

// Define IPC events separately (struct first, then trait)
struct RemoteCommand {
    static constexpr std::string_view event_name{"RemoteCommand"};
    std::string command;
    int priority;
};

template <>
struct IpcEventTrait<RemoteCommand> {
    static constexpr bool is_ipc_event = true;
    static Value serialize(const RemoteCommand& evt) {
        return Value::map({{"command", evt.command}, {"priority", evt.priority}});
    }
    static RemoteCommand deserialize(const Value& v) {
        return RemoteCommand{.command = v.at("command").as<std::string>(), .priority = v.at("priority").as<int>()};
    }
};

struct StatusUpdate {
    static constexpr std::string_view event_name{"StatusUpdate"};
    std::string component;
    std::string status;
    double progress;
};

template <>
struct IpcEventTrait<StatusUpdate> {
    static constexpr bool is_ipc_event = true;
    static Value serialize(const StatusUpdate& evt) {
        return Value::map({{"component", evt.component}, {"status", evt.status}, {"progress", evt.progress}});
    }
    static StatusUpdate deserialize(const Value& v) {
        return StatusUpdate{.component = v.at("component").as<std::string>(),
                            .status = v.at("status").as<std::string>(),
                            .progress = v.at("progress").as<double>()};
    }
};

#endif // LAGER_EXT_ENABLE_IPC

// ============================================================================
// Example Component with Guard
// ============================================================================

class DocumentViewer : public std::enable_shared_from_this<DocumentViewer> {
public:
    explicit DocumentViewer(const std::string& name) : name_(name) { std::cout << "[" << name_ << "] Created\n"; }

    ~DocumentViewer() { std::cout << "[" << name_ << "] Destroyed\n"; }

    void subscribe(EventBus& bus) {
        // Subscribe with guard - auto-unsubscribe when this object is destroyed
        conn_ = bus.subscribe<DocumentSaved>(weak_from_this(), [this](const DocumentSaved& evt) {
            std::cout << "[" << name_ << "] Document saved: " << evt.doc_id << " at " << evt.path << "\n";
        });
    }

private:
    std::string name_;
    ScopedConnection conn_;
};

// ============================================================================
// Example Component with Multiple Subscriptions
// ============================================================================

class EventLogger {
public:
    void subscribe(EventBus& bus) {
        // Subscribe to multiple static events
        connections_ += bus.subscribe<DocumentCreated>([](const DocumentCreated& evt) {
            std::cout << "[Logger] Document created: " << evt.doc_id << " - " << evt.title << "\n";
        });

        connections_ += bus.subscribe<DocumentSaved>([](const DocumentSaved& evt) {
            std::cout << "[Logger] Document saved: " << evt.doc_id << " at " << evt.path << "\n";
        });

        connections_ += bus.subscribe<PropertyChanged>([](const PropertyChanged& evt) {
            std::cout << "[Logger] Property changed: " << evt.property_name << " from " << evt.old_value << " to "
                      << evt.new_value << "\n";
        });

        // Subscribe to dynamic string events
        connections_ +=
            bus.subscribe("debug.log", [](const Value& v) { std::cout << "[Logger] Debug: " << to_json(v) << "\n"; });

        // Subscribe to multiple dynamic events
        connections_ += bus.subscribe({"warning", "error"}, [](std::string_view name, const Value& v) {
            std::cout << "[Logger] " << name << ": " << to_json(v) << "\n";
        });

        // Subscribe with filter
        connections_ += bus.subscribe([](std::string_view name) { return name.starts_with("custom."); },
                                      [](std::string_view name, const Value& v) {
                                          std::cout << "[Logger] Custom event: " << name << " = " << to_json(v) << "\n";
                                      });
    }

private:
    ScopedConnectionList connections_;
};

// ============================================================================
// Demo: Local EventBus
// ============================================================================

void demo_local_events() {
    std::cout << "=== Local EventBus Demo ===\n\n";

    // Use global bus
    EventBus& bus = default_bus();

    // Create event logger
    EventLogger logger;
    logger.subscribe(bus);

    std::cout << "--- Publishing static typed events ---\n";

    bus.publish(DocumentCreated{.doc_id = "doc001", .title = "My Document"});

    bus.publish(PropertyChanged{.property_name = "zoom", .old_value = 100, .new_value = 150});

    std::cout << "\n--- Publishing dynamic string events ---\n";

    bus.publish("debug.log", Value{"Debugging information"});
    bus.publish("warning", Value{"Low memory"});
    bus.publish("error", Value{"File not found"});
    bus.publish("custom.plugin.event", Value::map({{"action", "click"}, {"x", 100}, {"y", 200}}));

    std::cout << "\n--- Testing Guard mechanism ---\n";

    {
        auto viewer = std::make_shared<DocumentViewer>("Viewer1");
        viewer->subscribe(bus);

        // This should be received by the viewer
        bus.publish(DocumentSaved{.doc_id = "doc001", .path = "/tmp/doc001.txt"});

        // viewer goes out of scope here
    }

    std::cout << "\n--- After Viewer1 destroyed ---\n";

    // This should NOT be received by the viewer (it's destroyed)
    // But the logger should still receive it
    bus.publish(DocumentSaved{.doc_id = "doc002", .path = "/tmp/doc002.txt"});

    std::cout << "\n--- Testing local EventBus instance ---\n";

    // Create a local bus (separate channel)
    EventBus local_bus;

    ScopedConnection local_conn = local_bus.subscribe<DocumentCreated>(
        [](const auto& evt) { std::cout << "[LocalBus] Document created: " << evt.doc_id << "\n"; });

    // This goes to local bus only
    local_bus.publish(DocumentCreated{.doc_id = "local001", .title = "Local Doc"});

    // This goes to global bus only
    bus.publish(DocumentCreated{.doc_id = "global001", .title = "Global Doc"});
}

// ============================================================================
// Demo: RemoteBus (Cross-Process Messaging)
// ============================================================================

#ifdef LAGER_EXT_ENABLE_IPC

void demo_remote_bus() {
    std::cout << "\n=== RemoteBus Demo ===\n\n";

    // Create a local bus for this demo
    EventBus bus;

    // Create a remote bus connected to the local bus
    // In a real application, another process would connect to this channel
    RemoteBus remote("event_demo_channel", bus, RemoteBus::Role::Peer);

    if (!remote.connected()) {
        std::cout << "[Remote] Warning: Could not create channel - " << remote.last_error() << "\n";
        std::cout << "[Remote] This is expected if running standalone (no other process connected)\n";
    } else {
        std::cout << "[Remote] Connected to channel: " << remote.channel_name() << "\n";
    }

    // Subscribe to remote events
    ScopedConnectionList connections;

    connections += remote.subscribe_remote<RemoteCommand>([](const RemoteCommand& cmd) {
        std::cout << "[Remote Received] RemoteCommand: " << cmd.command << " (priority=" << cmd.priority << ")\n";
    });

    connections += remote.subscribe_remote<StatusUpdate>([](const StatusUpdate& status) {
        std::cout << "[Remote Received] StatusUpdate: " << status.component << " - " << status.status << " ("
                  << status.progress << "%)\n";
    });

    // Subscribe to dynamic remote events
    connections += remote.subscribe_remote(
        "remote.ping", [](const Value& v) { std::cout << "[Remote Received] Ping: " << to_json(v) << "\n"; });

    // Bridge certain remote events to local bus
    connections += remote.bridge_to_local<StatusUpdate>();

    // Local subscriber will receive bridged events
    connections += bus.subscribe<StatusUpdate>([](const StatusUpdate& status) {
        std::cout << "[Local Bus] StatusUpdate bridged: " << status.component << "\n";
    });

    std::cout << "\n--- Publishing events to remote (if connected) ---\n";

    // Publish typed event to remote
    bool sent = remote.post_remote(RemoteCommand{.command = "start_render", .priority = 1});
    std::cout << "[Remote] post_remote<RemoteCommand>: " << (sent ? "queued" : "failed") << "\n";

    // Broadcast to both local and remote
    sent = remote.broadcast(StatusUpdate{.component = "Renderer", .status = "Initializing", .progress = 0.0});
    std::cout << "[Remote] broadcast<StatusUpdate>: " << (sent ? "sent" : "local only") << "\n";

    // Publish dynamic event
    sent = remote.post_remote("remote.command", Value::map({{"action", "save"}, {"target", "scene.json"}}));
    std::cout << "[Remote] post_remote(dynamic): " << (sent ? "queued" : "failed") << "\n";

    std::cout << "\n--- Polling for incoming events ---\n";

    // Poll for any incoming events (non-blocking)
    std::size_t received = remote.poll();
    std::cout << "[Remote] Polled " << received << " events\n";

    // In a real application, you would call poll() in your event loop:
    // while (running) {
    //     remote.poll();
    //     // ... other work
    // }

    std::cout << "\n--- Request/Response pattern ---\n";

    // Register a request handler (would be called when another process sends a request)
    connections += remote.on_request("query.status", [](const Value& request) -> Value {
        std::cout << "[Remote] Handling request: " << to_json(request) << "\n";
        return Value::map({{"status", "ok"}, {"uptime", 12345}});
    });

    // In another process, you would send a request like this:
    // auto response = remote.request("query.status", Value::map({{"component", "all"}}), 5s);

    std::cout << "[Remote] Request handler registered for 'query.status'\n";
}

#endif // LAGER_EXT_ENABLE_IPC

// ============================================================================
// Main
// ============================================================================

int main() {
    // Demo 1: Local event bus
    demo_local_events();

#ifdef LAGER_EXT_ENABLE_IPC
    // Demo 2: Remote bus for cross-process messaging
    demo_remote_bus();
#else
    std::cout << "\n[Note] RemoteBus demo skipped (LAGER_EXT_ENABLE_IPC not defined)\n";
#endif

    std::cout << "\n=== Demo Complete ===\n";

    return 0;
}
