// test_event_bus_ipc.cpp - Tests for IPC EventBus system
// Module 5: EventBus (IPC) related interfaces

#include <catch2/catch_all.hpp>
#include <lager_ext/event_bus.h>
#include <lager_ext/ipc/ipc_event_bus.h>
#include <lager_ext/value.h>

#include <thread>
#include <chrono>
#include <atomic>
#include <string>

using namespace lager_ext;
using namespace std::chrono_literals;

// ============================================================
// IPC Event Definitions
// ============================================================

// Define test events for IPC
LAGER_EXT_EVENT(IpcTestEvent, 
    int id;
    std::string payload;
);

LAGER_EXT_EVENT(IpcCounterEvent, 
    int counter;
);

// ============================================================
// IPCEventBus Construction Tests
// ============================================================

TEST_CASE("IPCEventBus construction", "[eventbus][ipc]") {
    SECTION("create with unique name") {
        std::string unique_name = "test_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        IPCEventBus bus{unique_name};
        REQUIRE(bus.is_valid());
        REQUIRE(bus.name() == unique_name);
    }
    
    SECTION("create as host") {
        std::string unique_name = "host_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        IPCEventBus bus{unique_name, IPCEventBus::Role::Host};
        REQUIRE(bus.is_valid());
        REQUIRE(bus.is_host());
    }
    
    SECTION("create as client") {
        std::string unique_name = "client_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        // First create host
        IPCEventBus host{unique_name, IPCEventBus::Role::Host};
        
        // Then connect as client
        IPCEventBus client{unique_name, IPCEventBus::Role::Client};
        REQUIRE(client.is_valid());
        REQUIRE_FALSE(client.is_host());
    }
}

// ============================================================
// IPCEventBus Configuration Tests
// ============================================================

TEST_CASE("IPCEventBus configuration", "[eventbus][ipc][config]") {
    std::string unique_name = "config_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    SECTION("default buffer size") {
        IPCEventBus bus{unique_name};
        REQUIRE(bus.buffer_size() > 0);
    }
    
    SECTION("custom buffer size") {
        IPCEventBusConfig config;
        config.buffer_size = 1024 * 1024;  // 1MB
        
        IPCEventBus bus{unique_name, config};
        REQUIRE(bus.buffer_size() == config.buffer_size);
    }
    
    SECTION("config with timeout") {
        IPCEventBusConfig config;
        config.connection_timeout_ms = 5000;
        config.send_timeout_ms = 1000;
        
        IPCEventBus bus{unique_name, config};
        REQUIRE(bus.is_valid());
    }
}

// ============================================================
// Single Process IPC Tests (Mock IPC within same process)
// ============================================================

TEST_CASE("IPCEventBus single-process messaging", "[eventbus][ipc][messaging]") {
    std::string unique_name = "msg_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    IPCEventBus bus{unique_name, IPCEventBus::Role::Host};
    
    std::atomic<int> received_count{0};
    int received_id = 0;
    std::string received_payload;
    
    SECTION("subscribe and publish") {
        auto conn = bus.subscribe<IpcTestEvent>([&](const IpcTestEvent& evt) {
            received_id = evt.id;
            received_payload = evt.payload;
            received_count++;
        });
        
        bus.publish(IpcTestEvent{.id = 42, .payload = "hello"});
        
        // Process events
        bus.poll();
        
        REQUIRE(received_count == 1);
        REQUIRE(received_id == 42);
        REQUIRE(received_payload == "hello");
    }
    
    SECTION("multiple subscribers") {
        std::atomic<int> count1{0};
        std::atomic<int> count2{0};
        
        auto conn1 = bus.subscribe<IpcCounterEvent>([&](const IpcCounterEvent&) {
            count1++;
        });
        
        auto conn2 = bus.subscribe<IpcCounterEvent>([&](const IpcCounterEvent&) {
            count2++;
        });
        
        bus.publish(IpcCounterEvent{.counter = 1});
        bus.poll();
        
        REQUIRE(count1 == 1);
        REQUIRE(count2 == 1);
    }
}

// ============================================================
// Connection Management Tests
// ============================================================

TEST_CASE("IPCEventBus connection management", "[eventbus][ipc][connection]") {
    std::string unique_name = "conn_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    IPCEventBus bus{unique_name};
    
    SECTION("scoped connection auto-disconnects") {
        std::atomic<int> received{0};
        
        {
            auto conn = bus.subscribe<IpcTestEvent>([&](const IpcTestEvent&) {
                received++;
            });
            
            bus.publish(IpcTestEvent{.id = 1, .payload = "test"});
            bus.poll();
            REQUIRE(received == 1);
        }
        
        // Connection out of scope
        bus.publish(IpcTestEvent{.id = 2, .payload = "test2"});
        bus.poll();
        
        REQUIRE(received == 1);  // Still 1, handler disconnected
    }
    
    SECTION("manual disconnect") {
        std::atomic<int> received{0};
        
        auto conn = bus.subscribe<IpcTestEvent>([&](const IpcTestEvent&) {
            received++;
        });
        
        bus.publish(IpcTestEvent{.id = 1, .payload = "test"});
        bus.poll();
        REQUIRE(received == 1);
        
        conn.disconnect();
        
        bus.publish(IpcTestEvent{.id = 2, .payload = "test2"});
        bus.poll();
        
        REQUIRE(received == 1);  // Still 1
    }
}

// ============================================================
// Serialization Tests
// ============================================================

TEST_CASE("IPCEventBus serialization", "[eventbus][ipc][serialize]") {
    std::string unique_name = "serial_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    IPCEventBus bus{unique_name};
    
    SECTION("complex payload") {
        std::string long_payload(1000, 'x');  // 1000 character string
        std::string received_payload;
        
        auto conn = bus.subscribe<IpcTestEvent>([&](const IpcTestEvent& evt) {
            received_payload = evt.payload;
        });
        
        bus.publish(IpcTestEvent{.id = 1, .payload = long_payload});
        bus.poll();
        
        REQUIRE(received_payload == long_payload);
    }
    
    SECTION("special characters in payload") {
        std::string special = "Hello\nWorld\t\"quoted\"\\backslash";
        std::string received_payload;
        
        auto conn = bus.subscribe<IpcTestEvent>([&](const IpcTestEvent& evt) {
            received_payload = evt.payload;
        });
        
        bus.publish(IpcTestEvent{.id = 1, .payload = special});
        bus.poll();
        
        REQUIRE(received_payload == special);
    }
}

// ============================================================
// Generic Value Event Tests
// ============================================================

TEST_CASE("IPCEventBus generic value events", "[eventbus][ipc][value]") {
    std::string unique_name = "value_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    IPCEventBus bus{unique_name};
    
    SECTION("publish and receive ImmerValue") {
        ImmerValue received_value;
        
        auto conn = bus.subscribe_value("test_channel", [&](const ImmerValue& value) {
            received_value = value;
        });
        
        auto test_value = ImmerValue::map({
            {"key", ImmerValue{"value"}},
            {"number", ImmerValue{42}}
        });
        
        bus.publish_value("test_channel", test_value);
        bus.poll();
        
        REQUIRE(received_value.is_map());
        REQUIRE(received_value.at("key").as<std::string>() == "value");
        REQUIRE(received_value.at("number").as<int>() == 42);
    }
    
    SECTION("channel filtering") {
        std::atomic<int> channel1_count{0};
        std::atomic<int> channel2_count{0};
        
        auto conn1 = bus.subscribe_value("channel1", [&](const ImmerValue&) {
            channel1_count++;
        });
        
        auto conn2 = bus.subscribe_value("channel2", [&](const ImmerValue&) {
            channel2_count++;
        });
        
        bus.publish_value("channel1", ImmerValue{1});
        bus.poll();
        
        REQUIRE(channel1_count == 1);
        REQUIRE(channel2_count == 0);
    }
}

// ============================================================
// Queue and Buffer Tests
// ============================================================

TEST_CASE("IPCEventBus queue behavior", "[eventbus][ipc][queue]") {
    std::string unique_name = "queue_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    IPCEventBus bus{unique_name};
    
    SECTION("multiple messages queued") {
        std::vector<int> received_ids;
        
        auto conn = bus.subscribe<IpcTestEvent>([&](const IpcTestEvent& evt) {
            received_ids.push_back(evt.id);
        });
        
        // Publish multiple before polling
        bus.publish(IpcTestEvent{.id = 1, .payload = "a"});
        bus.publish(IpcTestEvent{.id = 2, .payload = "b"});
        bus.publish(IpcTestEvent{.id = 3, .payload = "c"});
        
        // Single poll should process all
        bus.poll();
        
        REQUIRE(received_ids.size() == 3);
        REQUIRE(received_ids[0] == 1);
        REQUIRE(received_ids[1] == 2);
        REQUIRE(received_ids[2] == 3);
    }
    
    SECTION("queue statistics") {
        auto stats = bus.get_stats();
        REQUIRE(stats.total_sent == 0);
        REQUIRE(stats.total_received == 0);
        
        auto conn = bus.subscribe<IpcTestEvent>([](const IpcTestEvent&) {});
        
        bus.publish(IpcTestEvent{.id = 1, .payload = "test"});
        bus.poll();
        
        stats = bus.get_stats();
        REQUIRE(stats.total_sent >= 1);
        REQUIRE(stats.total_received >= 1);
    }
}

// ============================================================
// Error Handling Tests
// ============================================================

TEST_CASE("IPCEventBus error handling", "[eventbus][ipc][error]") {
    SECTION("invalid bus name") {
        // Empty name should fail or create default
        IPCEventBus bus{""};
        // Implementation dependent - either fails or uses default name
    }
    
    SECTION("exception in handler doesn't crash") {
        std::string unique_name = "error_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        IPCEventBus bus{unique_name};
        std::atomic<int> after_exception_count{0};
        
        auto conn1 = bus.subscribe<IpcTestEvent>([](const IpcTestEvent&) {
            throw std::runtime_error("handler error");
        });
        
        auto conn2 = bus.subscribe<IpcTestEvent>([&](const IpcTestEvent&) {
            after_exception_count++;
        });
        
        REQUIRE_NOTHROW([&]() {
            bus.publish(IpcTestEvent{.id = 1, .payload = "test"});
            bus.poll();
        }());
        
        // Second handler should still have been called
        REQUIRE(after_exception_count == 1);
    }
}

// ============================================================
// Threading Tests (if supported)
// ============================================================

TEST_CASE("IPCEventBus thread safety", "[eventbus][ipc][thread]") {
    std::string unique_name = "thread_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    IPCEventBus bus{unique_name};
    
    SECTION("concurrent publish") {
        std::atomic<int> received_count{0};
        
        auto conn = bus.subscribe<IpcCounterEvent>([&](const IpcCounterEvent&) {
            received_count++;
        });
        
        const int num_threads = 4;
        const int msgs_per_thread = 25;
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&bus, msgs_per_thread]() {
                for (int i = 0; i < msgs_per_thread; ++i) {
                    bus.publish(IpcCounterEvent{.counter = i});
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Poll until all received
        for (int i = 0; i < 10; ++i) {
            bus.poll();
            std::this_thread::sleep_for(10ms);
        }
        
        REQUIRE(received_count == num_threads * msgs_per_thread);
    }
}

// ============================================================
// Lifecycle Tests
// ============================================================

TEST_CASE("IPCEventBus lifecycle", "[eventbus][ipc][lifecycle]") {
    SECTION("shutdown cleans up") {
        std::string unique_name = "lifecycle_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        auto bus = std::make_unique<IPCEventBus>(unique_name);
        REQUIRE(bus->is_valid());
        
        auto conn = bus->subscribe<IpcTestEvent>([](const IpcTestEvent&) {});
        
        bus->shutdown();
        REQUIRE_FALSE(bus->is_valid());
        
        // Subsequent operations should be safe (no-op)
        REQUIRE_NOTHROW([&]() {
            bus->publish(IpcTestEvent{.id = 1, .payload = "test"});
            bus->poll();
        }());
    }
    
    SECTION("destructor cleans up properly") {
        std::string unique_name = "dtor_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        {
            IPCEventBus bus{unique_name};
            auto conn = bus.subscribe<IpcTestEvent>([](const IpcTestEvent&) {});
            bus.publish(IpcTestEvent{.id = 1, .payload = "test"});
        }
        // Bus destroyed, should not leak resources
        
        // Creating a new bus with same name should work
        IPCEventBus new_bus{unique_name};
        REQUIRE(new_bus.is_valid());
    }
}

// ============================================================
// State Synchronization Tests
// ============================================================

TEST_CASE("IPCEventBus state synchronization", "[eventbus][ipc][state]") {
    std::string unique_name = "state_bus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    IPCEventBus bus{unique_name};
    
    SECTION("request-reply pattern") {
        // Simulating request-reply within same process
        ImmerValue request_received;
        
        auto request_handler = bus.subscribe_value("request", [&](const ImmerValue& req) {
            request_received = req;
            // Send reply
            bus.publish_value("reply", ImmerValue::map({
                {"status", ImmerValue{"ok"}},
                {"data", req}
            }));
        });
        
        ImmerValue reply_received;
        auto reply_handler = bus.subscribe_value("reply", [&](const ImmerValue& rep) {
            reply_received = rep;
        });
        
        // Send request
        bus.publish_value("request", ImmerValue::map({
            {"action", ImmerValue{"get_data"}}
        }));
        
        // Process
        bus.poll();  // Process request
        bus.poll();  // Process reply
        
        REQUIRE(request_received.is_map());
        REQUIRE(reply_received.is_map());
        REQUIRE(reply_received.at("status").as<std::string>() == "ok");
    }
}
