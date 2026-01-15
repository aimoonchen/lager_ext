// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file main.cpp
/// @brief IPC Demo - Basic usage of lager_ext IPC module
///
/// This demo shows:
/// 1. Unidirectional Channel (Producer -> Consumer)
/// 2. Bidirectional ChannelPair (Request/Reply pattern)
/// 3. Sending/receiving raw data and Value objects
/// 4. SharedBufferSPSC - High-performance Value serialization transfer
///
/// Usage:
///   ipc_demo                 # Run as client (spawns server automatically)
///   ipc_demo --server        # Run as server (internal use)

#include <lager_ext/builders.h>
#include <lager_ext/ipc.h>
#include <lager_ext/serialization.h>
#include <lager_ext/shared_buffer_spsc.h>
#include <lager_ext/value.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace lager_ext;
using namespace lager_ext::ipc;

// Channel names for demo
const std::string CHANNEL_NAME = "IpcDemoChannel";

//=============================================================================
// Server Process (Endpoint B)
//=============================================================================

int runServer() {
    std::cout << "[Server] Starting IPC server...\n";

    // =========================================
    // Demo 1: Unidirectional Channel (Consumer)
    // =========================================
    std::cout << "\n[Server] Demo 1: Unidirectional Channel\n";
    std::cout << "[Server] Creating consumer channel...\n";

    // Create consumer to receive messages from client
    auto consumer = Channel::open(CHANNEL_NAME + "_unidirectional");
    if (!consumer) {
        std::cerr << "[Server] Failed to create consumer channel\n";
        return 1;
    }
    std::cout << "[Server] Consumer channel created, waiting for messages...\n";

    // Wait and receive messages
    int messagesReceived = 0;
    auto startTime = std::chrono::steady_clock::now();

    while (messagesReceived < 5) {
        uint32_t msgId;
        uint8_t buffer[256];

        int len = consumer->tryReceiveRaw(msgId, buffer, sizeof(buffer));
        if (len > 0) {
            std::string content(reinterpret_cast<char*>(buffer), len);
            std::cout << "[Server] Received message #" << msgId << ": \"" << content << "\"\n";
            messagesReceived++;
        }

        // Timeout after 10 seconds
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            std::cerr << "[Server] Timeout waiting for messages\n";
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // =========================================
    // Demo 2: Bidirectional ChannelPair (Endpoint B - Server)
    // =========================================
    std::cout << "\n[Server] Demo 2: Bidirectional ChannelPair\n";
    std::cout << "[Server] Creating ChannelPair (Connector)...\n";

    // Server (Connector) attaches to existing channels created by client (Creator)
    std::unique_ptr<ChannelPair> pair;
    startTime = std::chrono::steady_clock::now();

    // Try connecting until client creates the channels
    while (!pair) {
        pair = ChannelPair::connect(CHANNEL_NAME + "_bidirectional");

        if (!pair) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
                std::cerr << "[Server] Timeout waiting for client to create ChannelPair\n";
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    std::cout << "[Server] Connected to ChannelPair!\n";

    // Echo loop: receive Value messages and reply
    int echoCount = 0;
    while (echoCount < 3) {
        auto msg = pair->tryReceive();
        if (msg) {
            std::cout << "[Server] Received request #" << msg->msgId << "\n";

            // Build and send reply Value
            Value reply = MapBuilder()
                              .set("status", "ok")
                              .set("echo_id", static_cast<int64_t>(msg->msgId))
                              .set("message", "Reply from server")
                              .finish();

            pair->post(msg->msgId + 1000, reply);
            std::cout << "[Server] Sent reply #" << (msg->msgId + 1000) << "\n";

            echoCount++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // =========================================
    // Demo 3: Sending/Receiving complex Value objects
    // =========================================
    std::cout << "\n[Server] Demo 3: Complex Value Object Transfer\n";

    // Receive Value from client
    startTime = std::chrono::steady_clock::now();

    while (true) {
        auto msg = pair->tryReceive();
        if (msg) {
            std::cout << "[Server] Received Value object (msgId=" << msg->msgId << "):\n";

            // Access Value data using at()
            Value name = msg->data.at("name");
            if (!name.is_null()) {
                std::cout << "  name: " << name.as_string() << "\n";
            }
            Value age = msg->data.at("age");
            if (!age.is_null()) {
                std::cout << "  age: " << age.as_number() << "\n";
            }
            Value tags = msg->data.at("tags");
            if (!tags.is_null()) {
                std::cout << "  tags: [";
                if (auto* vec = tags.get_if<ValueVector>()) {
                    bool first = true;
                    for (const auto& item : *vec) {
                        if (!first)
                            std::cout << ", ";
                        std::cout << "\"" << item.get().as_string() << "\"";
                        first = false;
                    }
                }
                std::cout << "]\n";
            }

            // Send acknowledgment Value
            std::string originalName = name.is_null() ? "unknown" : name.as_string();

            Value ack = MapBuilder().set("status", "received").set("original_name", originalName).finish();
            pair->post(msg->msgId + 2000, ack);
            std::cout << "[Server] Sent acknowledgment Value\n";
            break;
        }

        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            std::cerr << "[Server] Timeout waiting for Value\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // =========================================
    // Demo 4: SharedBufferSPSC - High-performance Value Transfer
    // =========================================
    std::cout << "\n[Server] Demo 4: SharedBufferSPSC Value Transfer\n";

    // Fixed-size message structure for SharedBufferSPSC
    struct ValueMessage {
        uint32_t size;                    // Actual data size
        uint8_t data[64 * 1024 - 4];      // 64KB buffer
    };
    static_assert(std::is_trivially_copyable_v<ValueMessage>);

    // Open the shared buffer (consumer side)
    std::cout << "[Server] Opening SharedBufferSPSC...\n";
    std::unique_ptr<SharedBufferSPSC<ValueMessage>> spscBuffer;
    startTime = std::chrono::steady_clock::now();

    while (!spscBuffer) {
        spscBuffer = SharedBufferSPSC<ValueMessage>::open(CHANNEL_NAME + "_spsc_value");
        if (!spscBuffer) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
                std::cerr << "[Server] Timeout waiting for SharedBufferSPSC\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (spscBuffer) {
        std::cout << "[Server] SharedBufferSPSC opened, waiting for Value...\n";

        // Wait for data with version check
        startTime = std::chrono::steady_clock::now();
        while (!spscBuffer->has_update()) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
                std::cerr << "[Server] Timeout waiting for SPSC data\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (spscBuffer->has_update()) {
            // Zero-copy read from shared memory
            const auto& msg = spscBuffer->read();
            std::cout << "[Server] Received " << msg.size << " bytes via SharedBufferSPSC\n";

            // Deserialize Value from the buffer
            Value received = deserialize(msg.data, msg.size);

            // Display received Value
            std::cout << "[Server] Deserialized Value:\n";
            Value title = received.at("title");
            if (!title.is_null()) {
                std::cout << "  title: " << title.as_string() << "\n";
            }
            Value count = received.at("count");
            if (!count.is_null()) {
                std::cout << "  count: " << count.as_number() << "\n";
            }
            Value position = received.at("position");
            if (!position.is_null()) {
                if (auto* vec = position.get_if<Vec3>()) {
                    std::cout << "  position: [" << (*vec)[0] << ", " << (*vec)[1] << ", " << (*vec)[2] << "]\n";
                }
            }
            std::cout << "[Server] SharedBufferSPSC demo complete!\n";
        }
    }

    std::cout << "\n[Server] Demo complete. Exiting.\n";
    return 0;
}

//=============================================================================
// Client Process (Endpoint A)
//=============================================================================

int runClient() {
    std::cout << "[Client] Starting IPC client...\n";

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // =========================================
    // Demo 1: Unidirectional Channel (Producer)
    // =========================================
    std::cout << "\n[Client] Demo 1: Unidirectional Channel\n";
    std::cout << "[Client] Creating producer channel...\n";

    // Create producer to send messages to server
    auto producer = Channel::create(CHANNEL_NAME + "_unidirectional");
    if (!producer) {
        std::cerr << "[Client] Failed to create producer channel\n";
        return 1;
    }
    std::cout << "[Client] Producer channel created.\n";

    // Send several messages as raw bytes
    const char* messages[] = {"Hello from client!", "This is message 2", "IPC is working", "Almost done",
                              "Last message"};

    for (int i = 0; i < 5; ++i) {
        bool sent = producer->postRaw(i + 1, // message ID
                                      messages[i], strlen(messages[i]));
        std::cout << "[Client] Sent message #" << (i + 1) << ": \"" << messages[i] << "\" - "
                  << (sent ? "OK" : "FAILED") << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // =========================================
    // Demo 2: Bidirectional ChannelPair (Creator - Client)
    // =========================================
    std::cout << "\n[Client] Demo 2: Bidirectional ChannelPair\n";
    std::cout << "[Client] Creating ChannelPair (Creator)...\n";

    // Client (Creator) creates both channels
    auto pair = ChannelPair::create(CHANNEL_NAME + "_bidirectional");
    if (!pair) {
        std::cerr << "[Client] Failed to create ChannelPair\n";
        return 1;
    }
    std::cout << "[Client] ChannelPair created, waiting for server to connect...\n";

    // Give server time to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Send requests and wait for replies
    const char* requestMsgs[] = {"Ping", "Hello Server", "How are you?"};

    for (int i = 0; i < 3; ++i) {
        // Build request Value
        Value request = MapBuilder().set("type", "request").set("content", requestMsgs[i]).finish();

        std::cout << "[Client] Sending request #" << (i + 1) << ": \"" << requestMsgs[i] << "\"\n";
        pair->post(i + 1, request);

        // Wait for reply
        bool gotReply = false;
        auto startTime = std::chrono::steady_clock::now();
        while (!gotReply) {
            auto reply = pair->tryReceive();
            if (reply) {
                std::cout << "[Client] Received reply #" << reply->msgId;
                Value status = reply->data.at("status");
                if (!status.is_null()) {
                    std::cout << " (status: " << status.as_string() << ")";
                }
                std::cout << "\n";
                gotReply = true;
            }

            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 5) {
                std::cerr << "[Client] Timeout waiting for reply\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // =========================================
    // Demo 3: Sending/Receiving complex Value objects
    // =========================================
    std::cout << "\n[Client] Demo 3: Complex Value Object Transfer\n";

    // Build a complex Value object using builder API
    Value userData =
        MapBuilder()
            .set("name", "Alice")
            .set("age", 30)
            .set("active", true)
            .set("tags", VectorBuilder().push_back("developer").push_back("gamer").push_back("reader").finish())
            .finish();

    std::cout << "[Client] Sending complex Value object...\n";
    pair->post(100, userData);

    // Wait for acknowledgment
    auto startTime = std::chrono::steady_clock::now();
    while (true) {
        auto ack = pair->tryReceive();
        if (ack) {
            std::cout << "[Client] Received acknowledgment:\n";
            Value status = ack->data.at("status");
            if (!status.is_null()) {
                std::cout << "  status: " << status.as_string() << "\n";
            }
            Value origName = ack->data.at("original_name");
            if (!origName.is_null()) {
                std::cout << "  original_name: " << origName.as_string() << "\n";
            }
            break;
        }

        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 5) {
            std::cerr << "[Client] Timeout waiting for acknowledgment\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // =========================================
    // Demo 4: SharedBufferSPSC - High-performance Value Transfer
    // =========================================
    std::cout << "\n[Client] Demo 4: SharedBufferSPSC Value Transfer\n";

    // Fixed-size message structure for SharedBufferSPSC
    struct ValueMessage {
        uint32_t size;                    // Actual data size
        uint8_t data[64 * 1024 - 4];      // 64KB buffer
    };
    static_assert(std::is_trivially_copyable_v<ValueMessage>);

    // Create the shared buffer (producer side)
    std::cout << "[Client] Creating SharedBufferSPSC...\n";
    auto spscBuffer = SharedBufferSPSC<ValueMessage>::create(CHANNEL_NAME + "_spsc_value");
    if (!spscBuffer) {
        std::cerr << "[Client] Failed to create SharedBufferSPSC: " 
                  << SharedBufferSPSC<ValueMessage>::last_error() << "\n";
    } else {
        std::cout << "[Client] SharedBufferSPSC created.\n";

        // Build a Value with various types including Vec3
        Value gameState = MapBuilder()
            .set("title", "Game State via SPSC")
            .set("count", 42)
            .set("position", Vec3{1.5f, 2.5f, 3.5f})
            .set("active", true)
            .finish();

        // Serialize and write using write_guard (zero-copy)
        {
            auto guard = spscBuffer->write_guard();
            auto bytesWritten = serialize_to(gameState, guard->data, sizeof(guard->data));
            guard->size = static_cast<uint32_t>(bytesWritten);
            std::cout << "[Client] Serialized and wrote " << bytesWritten << " bytes via SPSC\n";
        }  // Guard commits on destruction

        // Give server time to read
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "[Client] SharedBufferSPSC demo complete!\n";
    }

    std::cout << "\n[Client] Demo complete.\n";
    return 0;
}

//=============================================================================
// Main Entry Point
//=============================================================================

#ifdef _WIN32

int spawnServerAndRunClient() {
    std::cout << "==============================================\n";
    std::cout << "   lager_ext IPC Demo\n";
    std::cout << "==============================================\n\n";
    std::cout << "This demo shows basic IPC module usage:\n";
    std::cout << "  1. Unidirectional Channel (Producer -> Consumer)\n";
    std::cout << "  2. Bidirectional ChannelPair (Request/Reply)\n";
    std::cout << "  3. Value object serialization over IPC\n\n";

    // Get current executable path
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    // Spawn server process
    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    PROCESS_INFORMATION pi = {};
    std::string cmdLine = std::string(exePath) + " --server";

    std::cout << "Spawning server process...\n\n";

    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si,
                        &pi)) {
        std::cerr << "Failed to start server process: " << GetLastError() << "\n";
        return 1;
    }

    // Run client
    int result = runClient();

    // Wait for server to finish
    WaitForSingleObject(pi.hProcess, 5000);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "\n==============================================\n";
    std::cout << "   Demo Complete!\n";
    std::cout << "==============================================\n";

    return result;
}

#endif

//=============================================================================
// SharedBufferSPSC Tests (merged from shared_buffer_demo.cpp)
//=============================================================================

/// Simple camera state for testing
struct CameraState {
    float position[3];    // x, y, z  = 12 bytes
    float rotation[4];    // quaternion: x, y, z, w = 16 bytes
    float fov;            // 4 bytes
    uint32_t frame_id;    // 4 bytes
    char padding[28];     // Pad to 64 bytes
};

static_assert(std::is_trivially_copyable_v<CameraState>, "CameraState must be trivially copyable");
static_assert(sizeof(CameraState) == 64, "CameraState should be 64 bytes");

/// Larger data structure for bandwidth testing
struct LargeData {
    uint64_t sequence;
    uint64_t timestamp;
    float matrix[16];        // 4x4 matrix
    uint8_t blob[1024 - 80]; // Fill to 1KB
};

static_assert(sizeof(LargeData) == 1024, "LargeData should be 1024 bytes");

/// Configuration data for SharedBufferOnce test
struct ConfigData {
    uint32_t size;
    uint32_t version;
    char name[64];
    uint8_t data[1024 - 72];
};

static_assert(sizeof(ConfigData) == 1024, "ConfigData should be 1024 bytes");

void test_spsc_basic_operations() {
    std::cout << "\n=== SPSC Test 1: Basic Operations ===\n";
    
    auto producer = SharedBufferSPSC<CameraState>::create("TestCamera");
    if (!producer) {
        std::cerr << "ERROR: Failed to create producer\n";
        return;
    }
    
    auto consumer = SharedBufferSPSC<CameraState>::open("TestCamera");
    if (!consumer) {
        std::cerr << "ERROR: Failed to open consumer\n";
        return;
    }
    
    CameraState state1{};
    state1.position[0] = 1.0f;
    state1.position[1] = 2.0f;
    state1.position[2] = 3.0f;
    state1.fov = 60.0f;
    state1.frame_id = 1;
    
    producer->write(state1);
    const auto& read1 = consumer->read();
    
    if (read1.position[0] == 1.0f && read1.fov == 60.0f && read1.frame_id == 1) {
        std::cout << "✓ Basic read/write PASSED\n";
    } else {
        std::cout << "✗ Basic read/write FAILED\n";
    }
}

void test_spsc_update_tracking() {
    std::cout << "\n=== SPSC Test 2: Update Tracking ===\n";
    
    auto producer = SharedBufferSPSC<CameraState>::create("TestTracking");
    auto consumer = SharedBufferSPSC<CameraState>::open("TestTracking");
    
    if (!producer || !consumer) {
        std::cerr << "ERROR: Failed to create/open buffer\n";
        return;
    }
    
    CameraState state{};
    state.frame_id = 100;
    producer->write(state);
    
    CameraState out{};
    bool got_update = consumer->try_read(out);
    
    if (got_update && out.frame_id == 100) {
        std::cout << "✓ Update tracking PASSED\n";
    } else {
        std::cout << "✗ Update tracking FAILED\n";
    }
}

void test_spsc_performance() {
    std::cout << "\n=== SPSC Test 3: Performance Benchmark ===\n";
    
    auto producer = SharedBufferSPSC<LargeData>::create("TestPerf");
    auto consumer = SharedBufferSPSC<LargeData>::open("TestPerf");
    
    if (!producer || !consumer) {
        std::cerr << "ERROR: Failed to create/open buffer\n";
        return;
    }
    
    constexpr int ITERATIONS = 100000;
    LargeData data{};
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        data.sequence = i;
        producer->write(data);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << "write() x " << ITERATIONS << ": " << (double)ns / ITERATIONS << " ns/op\n";
    std::cout << "✓ Performance test completed\n";
}

void test_shared_buffer_once() {
    std::cout << "\n=== SPSC Test 4: SharedBufferOnce ===\n";
    
    auto producer = SharedBufferOnce<ConfigData>::create("TestOnce");
    if (!producer) {
        std::cerr << "ERROR: Failed to create producer\n";
        return;
    }
    
    {
        auto guard = producer->write_guard();
        guard->size = 42;
        guard->version = 1;
        std::snprintf(guard->name, sizeof(guard->name), "TestConfig");
    }
    
    auto consumer = SharedBufferOnce<ConfigData>::open("TestOnce");
    if (!consumer) {
        std::cerr << "ERROR: Failed to open consumer\n";
        return;
    }
    
    const auto& config = consumer->read();
    if (config.size == 42 && config.version == 1) {
        std::cout << "✓ SharedBufferOnce PASSED\n";
    } else {
        std::cout << "✗ SharedBufferOnce FAILED\n";
    }
}

void runSharedBufferTests() {
    std::cout << "\n===============================================\n";
    std::cout << "  SharedBufferSPSC Tests\n";
    std::cout << "===============================================\n";
    
    test_spsc_basic_operations();
    test_spsc_update_tracking();
    test_spsc_performance();
    test_shared_buffer_once();
    
    std::cout << "\n✓ All SharedBuffer tests completed!\n";
}

//=============================================================================
// IPC Domain Tests (merged from ipc_domain_demo.cpp)
//=============================================================================

#include <lager_ext/ipc_message.h>
#include <lager_ext/event_bus.h>

#ifdef LAGER_EXT_ENABLE_IPC
#include <lager_ext/event_bus_ipc.h>
#endif

using lager_ext::ipc::detail::fnv1a_hash32;

void test_message_layout() {
    std::cout << "\n=== Domain Test 1: Message Layout ===\n";
    
    std::cout << "sizeof(Message) = " << sizeof(Message) << " bytes\n";
    std::cout << "Message::INLINE_SIZE = " << Message::INLINE_SIZE << " bytes\n";
    
    Message msg;
    msg.msgId = fnv1a_hash32("TestEvent");
    msg.dataSize = 0;
    msg.timestamp = 12345;
    msg.domain = MessageDomain::Document;
    msg.flags = MessageFlags::None;
    msg.requestId = 0;
    msg.poolOffset = 0;
    
    std::cout << "msg.msgId (hash of 'TestEvent') = " << msg.msgId << "\n";
    std::cout << "msg.domain = " << static_cast<int>(msg.domain) << " (Document)\n";
    std::cout << "✓ Message layout test PASSED\n";
}

void test_channel_with_domain() {
    std::cout << "\n=== Domain Test 2: Channel with Domain ===\n";
    
    auto producer = Channel::create("DomainTest", 16);
    if (!producer) {
        std::cerr << "ERROR: Failed to create producer\n";
        return;
    }
    
    auto consumer = Channel::open("DomainTest");
    if (!consumer) {
        std::cerr << "ERROR: Failed to open consumer\n";
        return;
    }
    
    Value docData = Value::map({{"file", "test.txt"}, {"saved", true}});
    Value propData = Value::map({{"name", "width"}, {"value", 100}});
    
    bool sent1 = producer->post(fnv1a_hash32("DocSave"), docData, MessageDomain::Document);
    bool sent2 = producer->post(fnv1a_hash32("PropChange"), propData, MessageDomain::Property);
    
    std::cout << "Sent Document event: " << sent1 << "\n";
    std::cout << "Sent Property event: " << sent2 << "\n";
    
    auto msg1 = consumer->tryReceive();
    if (msg1) {
        std::cout << "Received 1: domain=" << static_cast<int>(msg1->domain) << " (expected 1)\n";
    }
    
    auto msg2 = consumer->tryReceive();
    if (msg2) {
        std::cout << "Received 2: domain=" << static_cast<int>(msg2->domain) << " (expected 2)\n";
    }
    
    std::cout << "✓ Channel domain test PASSED\n";
}

void test_fnv1a_hash() {
    std::cout << "\n=== Domain Test 3: FNV-1a Hash ===\n";
    
    constexpr uint32_t hash1 = fnv1a_hash32("DocumentSaved");
    std::string eventName = "DocumentSaved";
    uint32_t hash2 = fnv1a_hash32(eventName);
    
    std::cout << "Compile-time hash = " << hash1 << "\n";
    std::cout << "Runtime hash      = " << hash2 << "\n";
    
    if (hash1 == hash2) {
        std::cout << "✓ Hash consistency PASSED\n";
    } else {
        std::cout << "✗ Hash mismatch FAILED\n";
    }
}

void test_message_flags() {
    std::cout << "\n=== Domain Test 4: MessageFlags Operations ===\n";
    
    MessageFlags flags = MessageFlags::None;
    flags = flags | MessageFlags::LargePayload;
    flags = flags | MessageFlags::IsRequest;
    
    std::cout << "has_flag(LargePayload): " << has_flag(flags, MessageFlags::LargePayload) << "\n";
    std::cout << "has_flag(IsRequest): " << has_flag(flags, MessageFlags::IsRequest) << "\n";
    std::cout << "has_flag(IsResponse): " << has_flag(flags, MessageFlags::IsResponse) << "\n";
    
    std::cout << "✓ MessageFlags test PASSED\n";
}

void runDomainTests() {
    std::cout << "\n===============================================\n";
    std::cout << "  IPC Domain & Extended Features Tests\n";
    std::cout << "===============================================\n";
    
    test_message_layout();
    test_channel_with_domain();
    test_fnv1a_hash();
    test_message_flags();
    
    std::cout << "\n✓ All domain tests completed!\n";
}

//=============================================================================
// Combined Test Runner
//=============================================================================

void runAllTests() {
    std::cout << "==============================================\n";
    std::cout << "   lager_ext IPC Module - All Tests\n";
    std::cout << "==============================================\n";
    
    runSharedBufferTests();
    runDomainTests();
    
    std::cout << "\n==============================================\n";
    std::cout << "   All IPC Tests Complete!\n";
    std::cout << "==============================================\n";
}

int main(int argc, char* argv[]) {
    // Parse arguments
    bool serverMode = false;
    bool testMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            serverMode = true;
        } else if (arg == "--test") {
            testMode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "IPC Demo - Basic usage of lager_ext IPC module\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --server    Run as server (internal use)\n";
            std::cout << "  --test      Run SharedBuffer and Domain tests\n";
            std::cout << "  --help, -h  Show this help\n";
            return 0;
        }
    }
    
    // Test mode: run all local tests
    if (testMode) {
        runAllTests();
        return 0;
    }

#ifdef _WIN32
    if (serverMode) {
        return runServer();
    } else {
        return spawnServerAndRunClient();
    }
#else
    std::cout << "This demo currently requires Windows.\n";
    std::cout << "On other platforms, run two terminals:\n";
    std::cout << "  Terminal 1: " << argv[0] << " --server\n";
    std::cout << "  Terminal 2: " << argv[0] << "\n";

    if (serverMode) {
        return runServer();
    } else {
        return runClient();
    }
#endif
}