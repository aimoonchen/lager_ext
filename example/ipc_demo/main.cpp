// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file main.cpp
/// @brief IPC Demo - Basic usage of lager_ext IPC module
///
/// This demo shows:
/// 1. Unidirectional Channel (Producer -> Consumer)
/// 2. Bidirectional ChannelPair (Request/Reply pattern)
/// 3. Sending/receiving raw data and Value objects
///
/// Usage:
///   ipc_demo                 # Run as client (spawns server automatically)
///   ipc_demo --server        # Run as server (internal use)

#include <lager_ext/builders.h>
#include <lager_ext/ipc.h>
#include <lager_ext/value.h>

#include <chrono>
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

            pair->send(msg->msgId + 1000, reply);
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
            pair->send(msg->msgId + 2000, ack);
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
        bool sent = producer->sendRaw(i + 1, // message ID
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
        pair->send(i + 1, request);

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
    pair->send(100, userData);

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

int main(int argc, char* argv[]) {
    // Parse arguments
    bool serverMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            serverMode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "IPC Demo - Basic usage of lager_ext IPC module\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --server    Run as server (internal use)\n";
            std::cout << "  --help, -h  Show this help\n";
            return 0;
        }
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