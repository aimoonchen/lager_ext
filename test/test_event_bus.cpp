// test_event_bus.cpp - Tests for EventBus (non-IPC version)
// Module 3: EventBus publish/subscribe messaging

#include <catch2/catch_all.hpp>
#include <lager_ext/event_bus.h>
#include <lager_ext/value.h>

#include <string>
#include <vector>

using namespace lager_ext;

// ============================================================
// Test Events - Static typed events
// ============================================================

LAGER_EXT_EVENT(TestEvent,
    int value;
    std::string message;
);

LAGER_EXT_EVENT(CounterEvent,
    int count;
);

LAGER_EXT_EVENT(EmptyEvent);

// ============================================================
// EventBus Instance Tests
// ============================================================

TEST_CASE("EventBus construction", "[eventbus][construction]") {
    SECTION("default construction") {
        EventBus bus;
        // Should not crash
        REQUIRE(true);
    }
    
    SECTION("move construction") {
        EventBus bus1;
        EventBus bus2 = std::move(bus1);
        REQUIRE(true);
    }
}

// ============================================================
// Static Typed Event Tests
// ============================================================

TEST_CASE("EventBus static typed events", "[eventbus][static]") {
    EventBus bus;

    SECTION("subscribe and publish") {
        int received_value = 0;
        std::string received_message;
        
        auto conn = bus.subscribe<TestEvent>([&](const TestEvent& evt) {
            received_value = evt.value;
            received_message = evt.message;
        });
        
        REQUIRE(conn.connected());
        
        bus.publish(TestEvent{.value = 42, .message = "hello"});
        
        REQUIRE(received_value == 42);
        REQUIRE(received_message == "hello");
    }
    
    SECTION("multiple subscribers") {
        int count1 = 0, count2 = 0;
        
        auto conn1 = bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
            count1 += evt.count;
        });
        
        auto conn2 = bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
            count2 += evt.count;
        });
        
        bus.publish(CounterEvent{.count = 5});
        
        REQUIRE(count1 == 5);
        REQUIRE(count2 == 5);
    }
    
    SECTION("empty event") {
        bool received = false;
        
        auto conn = bus.subscribe<EmptyEvent>([&](const EmptyEvent&) {
            received = true;
        });
        
        bus.publish(EmptyEvent{});
        
        REQUIRE(received);
    }
}

// ============================================================
// Connection Management Tests
// ============================================================

TEST_CASE("Connection management", "[eventbus][connection]") {
    EventBus bus;
    
    SECTION("disconnect stops receiving events") {
        int count = 0;
        
        auto conn = bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
            count += evt.count;
        });
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
        
        conn.disconnect();
        REQUIRE_FALSE(conn.connected());
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1); // Should not have changed
    }
    
    SECTION("Connection move semantics") {
        int count = 0;
        
        auto conn1 = bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
            count += evt.count;
        });
        
        Connection conn2 = std::move(conn1);
        
        REQUIRE_FALSE(conn1.connected());
        REQUIRE(conn2.connected());
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
        
        conn2.disconnect();
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
    }
}

// ============================================================
// ScopedConnection Tests
// ============================================================

TEST_CASE("ScopedConnection", "[eventbus][scoped]") {
    EventBus bus;
    
    SECTION("auto-disconnect on destruction") {
        int count = 0;
        
        {
            ScopedConnection scoped = bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
                count += evt.count;
            });
            
            bus.publish(CounterEvent{.count = 1});
            REQUIRE(count == 1);
        } // scoped goes out of scope, auto-disconnect
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1); // Should not have changed
    }
    
    SECTION("reset disconnects") {
        int count = 0;
        
        ScopedConnection scoped = bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
            count += evt.count;
        });
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
        
        scoped.reset();
        REQUIRE_FALSE(scoped.connected());
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
    }
    
    SECTION("release prevents auto-disconnect") {
        int count = 0;
        Connection released;
        
        {
            ScopedConnection scoped = bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
                count += evt.count;
            });
            
            released = scoped.release();
            REQUIRE_FALSE(scoped.connected());
            REQUIRE(released.connected());
        }
        
        // Should still receive because we released
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
        
        released.disconnect();
    }
}

// ============================================================
// ScopedConnectionList Tests
// ============================================================

TEST_CASE("ScopedConnectionList", "[eventbus][scoped]") {
    EventBus bus;
    
    SECTION("add multiple connections") {
        int count1 = 0, count2 = 0;
        
        {
            ScopedConnectionList list;
            
            list += bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
                count1 += evt.count;
            });
            
            list += bus.subscribe<TestEvent>([&](const TestEvent& evt) {
                count2 = evt.value;
            });
            
            REQUIRE(list.size() == 2);
            REQUIRE_FALSE(list.empty());
            
            bus.publish(CounterEvent{.count = 5});
            bus.publish(TestEvent{.value = 10, .message = ""});
            
            REQUIRE(count1 == 5);
            REQUIRE(count2 == 10);
        } // All connections auto-disconnected
        
        bus.publish(CounterEvent{.count = 100});
        bus.publish(TestEvent{.value = 100, .message = ""});
        
        REQUIRE(count1 == 5);
        REQUIRE(count2 == 10);
    }
    
    SECTION("clear disconnects all") {
        int count = 0;
        
        ScopedConnectionList list;
        list.add(bus.subscribe<CounterEvent>([&](const CounterEvent& evt) {
            count += evt.count;
        }));
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
        
        list.clear();
        REQUIRE(list.empty());
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
    }
}

// ============================================================
// Dynamic String Event Tests
// ============================================================

TEST_CASE("EventBus dynamic string events", "[eventbus][dynamic]") {
    EventBus bus;
    
    SECTION("subscribe and publish with payload") {
        ImmerValue received;
        
        auto conn = bus.subscribe("my_event", [&](const ImmerValue& payload) {
            received = payload;
        });
        
        bus.publish("my_event", ImmerValue{42});
        
        REQUIRE(received.as<int>() == 42);
    }
    
    SECTION("publish without payload") {
        bool received = false;
        
        auto conn = bus.subscribe("simple_event", [&](const ImmerValue&) {
            received = true;
        });
        
        bus.publish("simple_event");
        
        REQUIRE(received);
    }
    
    SECTION("different event names are separate") {
        int event_a_count = 0, event_b_count = 0;
        
        auto conn_a = bus.subscribe("event_a", [&](const ImmerValue&) {
            event_a_count++;
        });
        
        auto conn_b = bus.subscribe("event_b", [&](const ImmerValue&) {
            event_b_count++;
        });
        
        bus.publish("event_a");
        bus.publish("event_a");
        bus.publish("event_b");
        
        REQUIRE(event_a_count == 2);
        REQUIRE(event_b_count == 1);
    }
}

// ============================================================
// Multi-event Subscription Tests
// ============================================================

TEST_CASE("EventBus multi-event subscription", "[eventbus][multi]") {
    EventBus bus;
    
    SECTION("subscribe to multiple events") {
        std::vector<std::string> received_events;
        
        auto conn = bus.subscribe({"event_1", "event_2", "event_3"},
            [&](std::string_view name, const ImmerValue&) {
                received_events.emplace_back(name);
            });
        
        bus.publish("event_1");
        bus.publish("event_2");
        bus.publish("event_3");
        bus.publish("event_4"); // Should not be received
        
        REQUIRE(received_events.size() == 3);
        REQUIRE(received_events[0] == "event_1");
        REQUIRE(received_events[1] == "event_2");
        REQUIRE(received_events[2] == "event_3");
    }
}

// ============================================================
// Filter-based Subscription Tests
// ============================================================

TEST_CASE("EventBus filter subscription", "[eventbus][filter]") {
    EventBus bus;
    
    SECTION("filter by prefix") {
        std::vector<std::string> received_events;
        
        auto conn = bus.subscribe(
            // Filter: accept events starting with "user."
            [](std::string_view name) { return name.starts_with("user."); },
            [&](std::string_view name, const ImmerValue&) {
                received_events.emplace_back(name);
            });
        
        bus.publish("user.created");
        bus.publish("user.updated");
        bus.publish("item.created"); // Should not match
        bus.publish("user.deleted");
        
        REQUIRE(received_events.size() == 3);
        REQUIRE(received_events[0] == "user.created");
        REQUIRE(received_events[1] == "user.updated");
        REQUIRE(received_events[2] == "user.deleted");
    }
}

// ============================================================
// Guard-based Subscription Tests
// ============================================================

TEST_CASE("EventBus guard subscription", "[eventbus][guard]") {
    EventBus bus;
    
    SECTION("auto-unsubscribe when guard expires") {
        int count = 0;
        
        auto guard = std::make_shared<int>(0);
        
        auto conn = bus.subscribe<CounterEvent>(
            std::weak_ptr<int>(guard),
            [&](const CounterEvent& evt) {
                count += evt.count;
            });
        
        bus.publish(CounterEvent{.count = 1});
        REQUIRE(count == 1);
        
        // Reset the guard (weak_ptr expires)
        guard.reset();
        
        // Publish again - handler should be skipped because guard expired
        bus.publish(CounterEvent{.count = 100});
        REQUIRE(count == 1); // Should not have changed
    }
}

// ============================================================
// Default Bus Tests
// ============================================================

TEST_CASE("default_bus singleton", "[eventbus][default]") {
    SECTION("default_bus returns same instance") {
        EventBus& bus1 = default_bus();
        EventBus& bus2 = default_bus();
        
        REQUIRE(&bus1 == &bus2);
    }
    
    SECTION("can use default_bus for messaging") {
        int value = 0;
        
        ScopedConnection conn = default_bus().subscribe<CounterEvent>(
            [&](const CounterEvent& evt) {
                value = evt.count;
            });
        
        default_bus().publish(CounterEvent{.count = 42});
        
        REQUIRE(value == 42);
    }
}
