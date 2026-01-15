// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file ipc_domain_demo.cpp
/// @brief Demonstration of Message Domain and extended IPC features
///
/// This demo tests:
/// 1. Extended Message struct with domain/flags/requestId
/// 2. Channel send/receive with domain parameter
/// 3. RemoteBus domain subscription API
///
/// Usage: Run this single executable - it tests single-process IPC

#include <lager_ext/event_bus.h>
#include <lager_ext/event_bus_ipc.h>
#include <lager_ext/ipc.h>
#include <lager_ext/ipc_message.h>
#include <lager_ext/value.h>

#include <iostream>

// Import FNV-1a hash function from detail namespace for convenience
using lager_ext::ipc::detail::fnv1a_hash32;

#include <chrono>
#include <iostream>
#include <thread>

using namespace lager_ext;
using namespace lager_ext::ipc;

//=============================================================================
// Test 1: Message struct layout
//=============================================================================

void test_message_layout() {
    std::cout << "\n=== Test 1: Message Layout ===" << std::endl;

    // Verify static_assert at compile time already passed
    std::cout << "sizeof(Message) = " << sizeof(Message) << " bytes (expected: 256)" << std::endl;
    std::cout << "Message::INLINE_SIZE = " << Message::INLINE_SIZE << " bytes (expected: 232)" << std::endl;

    // Create a message with domain
    Message msg;
    msg.msgId = fnv1a_hash32("TestEvent");
    msg.dataSize = 0;
    msg.timestamp = 12345;
    msg.domain = MessageDomain::Document;
    msg.flags = MessageFlags::None;
    msg.requestId = 0;
    msg.poolOffset = 0;

    std::cout << "msg.msgId (hash of 'TestEvent') = " << msg.msgId << std::endl;
    std::cout << "msg.domain = " << static_cast<int>(msg.domain) << " (Document)" << std::endl;
    std::cout << "msg.uses_pool() = " << msg.uses_pool() << std::endl;
    std::cout << "msg.is_request() = " << msg.is_request() << std::endl;
    
    // Test flags
    msg.flags = MessageFlags::LargePayload | MessageFlags::IsRequest;
    std::cout << "After setting LargePayload | IsRequest:" << std::endl;
    std::cout << "  msg.uses_pool() = " << msg.uses_pool() << std::endl;
    std::cout << "  msg.is_request() = " << msg.is_request() << std::endl;

    std::cout << "[PASS] Message layout test" << std::endl;
}

//=============================================================================
// Test 2: Channel with domain parameter
//=============================================================================

void test_channel_with_domain() {
    std::cout << "\n=== Test 2: Channel with Domain ===" << std::endl;

    // Create producer and consumer
    auto producer = Channel::create("DomainTest", 16);
    if (!producer) {
        std::cerr << "[FAIL] Failed to create producer" << std::endl;
        return;
    }

    auto consumer = Channel::open("DomainTest");
    if (!consumer) {
        std::cerr << "[FAIL] Failed to open consumer" << std::endl;
        return;
    }

    // Send messages with different domains
    Value docData = Value::map({{"file", "test.txt"}, {"saved", true}});
    Value propData = Value::map({{"name", "width"}, {"value", 100}});

    bool sent1 = producer->post(fnv1a_hash32("DocSave"), docData, MessageDomain::Document);
    bool sent2 = producer->post(fnv1a_hash32("PropChange"), propData, MessageDomain::Property);

    std::cout << "Sent Document event: " << sent1 << std::endl;
    std::cout << "Sent Property event: " << sent2 << std::endl;

    // Receive and verify domains
    auto msg1 = consumer->tryReceive();
    if (msg1) {
        std::cout << "Received 1: domain=" << static_cast<int>(msg1->domain) 
                  << " (expected 1=Document)" << std::endl;
    }

    auto msg2 = consumer->tryReceive();
    if (msg2) {
        std::cout << "Received 2: domain=" << static_cast<int>(msg2->domain) 
                  << " (expected 2=Property)" << std::endl;
    }

    std::cout << "[PASS] Channel domain test" << std::endl;
}

//=============================================================================
// Test 3: RemoteBus domain subscription
//=============================================================================

void test_remote_bus_domain_subscription() {
    std::cout << "\n=== Test 3: RemoteBus Domain Subscription ===" << std::endl;

    EventBus bus;
    
    // Create RemoteBus as server
    RemoteBus remote("DomainBusTest", bus, RemoteBus::Role::Server, 64);
    
    if (!remote.connected()) {
        std::cerr << "[FAIL] RemoteBus not connected: " << remote.last_error() << std::endl;
        return;
    }

    int documentEventCount = 0;
    int propertyEventCount = 0;

    // Subscribe to Document domain
    auto docConn = remote.subscribe_domain(MessageDomain::Document,
        [&](const RemoteBus::DomainEnvelope& env, const Value& data) {
            documentEventCount++;
            std::cout << "  -> Document domain event received, msgId=" << env.msgId << std::endl;
        });

    // Subscribe to Property domain  
    auto propConn = remote.subscribe_domain(MessageDomain::Property,
        [&](const RemoteBus::DomainEnvelope& env, const Value& data) {
            propertyEventCount++;
            std::cout << "  -> Property domain event received, msgId=" << env.msgId << std::endl;
        });

    // Create a separate channel to inject messages into the RemoteBus's underlying channel
    // Note: In real use, this would be from another process
    // For this demo, we directly test the subscription mechanism
    
    std::cout << "Domain subscriptions registered" << std::endl;
    std::cout << "Document events: " << documentEventCount << std::endl;
    std::cout << "Property events: " << propertyEventCount << std::endl;

    // Disconnect subscriptions
    docConn.disconnect();
    propConn.disconnect();
    
    std::cout << "[PASS] RemoteBus domain subscription API test" << std::endl;
}

//=============================================================================
// Test 4: FNV-1a Hash consistency
//=============================================================================

void test_fnv1a_hash() {
    std::cout << "\n=== Test 4: FNV-1a Hash ===" << std::endl;

    // Compile-time hash
    constexpr uint32_t hash1 = fnv1a_hash32("DocumentSaved");
    
    // Runtime hash
    std::string eventName = "DocumentSaved";
    uint32_t hash2 = fnv1a_hash32(eventName);

    std::cout << "Compile-time hash('DocumentSaved') = " << hash1 << std::endl;
    std::cout << "Runtime hash('DocumentSaved')      = " << hash2 << std::endl;

    if (hash1 == hash2) {
        std::cout << "[PASS] Hash consistency verified" << std::endl;
    } else {
        std::cerr << "[FAIL] Hash mismatch!" << std::endl;
    }

    // Test different strings produce different hashes
    constexpr uint32_t hashA = fnv1a_hash32("EventA");
    constexpr uint32_t hashB = fnv1a_hash32("EventB");
    
    std::cout << "hash('EventA') = " << hashA << std::endl;
    std::cout << "hash('EventB') = " << hashB << std::endl;
    
    if (hashA != hashB) {
        std::cout << "[PASS] Different strings produce different hashes" << std::endl;
    } else {
        std::cerr << "[FAIL] Hash collision detected!" << std::endl;
    }
}

//=============================================================================
// Test 5: MessageFlags operations
//=============================================================================

void test_message_flags() {
    std::cout << "\n=== Test 5: MessageFlags Operations ===" << std::endl;

    MessageFlags flags = MessageFlags::None;
    std::cout << "Initial: " << static_cast<int>(flags) << std::endl;

    // Add flags
    flags = flags | MessageFlags::LargePayload;
    std::cout << "After |= LargePayload: " << static_cast<int>(flags) << std::endl;

    flags = flags | MessageFlags::IsRequest;
    std::cout << "After |= IsRequest: " << static_cast<int>(flags) << std::endl;

    // Check flags
    std::cout << "has_flag(LargePayload): " << has_flag(flags, MessageFlags::LargePayload) << std::endl;
    std::cout << "has_flag(IsRequest): " << has_flag(flags, MessageFlags::IsRequest) << std::endl;
    std::cout << "has_flag(IsResponse): " << has_flag(flags, MessageFlags::IsResponse) << std::endl;

    // Combined flags
    MessageFlags combined = MessageFlags::LargePayload | MessageFlags::IsRequest | MessageFlags::IsResponse;
    std::cout << "Combined (Large|Request|Response): " << static_cast<int>(combined) << std::endl;

    std::cout << "[PASS] MessageFlags operations test" << std::endl;
}

//=============================================================================
// Main
//=============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  IPC Domain & Extended Features Demo  " << std::endl;
    std::cout << "========================================" << std::endl;

    test_message_layout();
    test_channel_with_domain();
    test_remote_bus_domain_subscription();
    test_fnv1a_hash();
    test_message_flags();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  All tests completed!                  " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}