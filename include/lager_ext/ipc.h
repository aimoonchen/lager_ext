// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file ipc.h
/// @brief High-performance lock-free IPC channel (Single Producer Single Consumer)
///
/// This is an optimized IPC implementation for the common case where:
/// - Exactly ONE process sends messages (Producer)
/// - Exactly ONE process receives messages (Consumer)
///
/// Key features:
/// - Lock-free ring buffer using atomic operations
/// - No system calls in the hot path
/// - Cache-line aligned to avoid false sharing
/// - Supports both polling and blocking modes
/// - Message domain support for categorization
/// - Shared memory pool for large payloads (>240 bytes)
///
/// Usage:
/// @code
///     // Process A (Producer) - creates the channel
///     auto channel = Channel::create("MyChannel", 1024);
///     channel->post(msgId, data);  // Non-blocking, like PostMessage
///
///     // Process B (Consumer) - opens existing channel
///     auto channel = Channel::open("MyChannel");
///     while (auto msg = channel->receive()) {
///         process(msg->msgId, msg->data);
///     }
/// @endcode

#pragma once

#include <lager_ext/lager_ext_config.h>
#include <lager_ext/api.h>
#include <lager_ext/ipc_message.h>
#include <lager_ext/value.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace lager_ext {
namespace ipc {

//=============================================================================
// Constants
//=============================================================================

/// Default queue capacity (number of messages)
constexpr size_t DEFAULT_CAPACITY = 4096;

/// Cache line size for padding (avoid false sharing)
/// C++20: prefer std::hardware_destructive_interference_size when available
#if __cpp_lib_hardware_interference_size >= 201703L
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
constexpr size_t CACHE_LINE_SIZE = 64; // Common value for x86/ARM
#endif

//=============================================================================
// Message - Fixed-size message structure
//=============================================================================

/// Message structure optimized for IPC transfer
/// Uses inline storage for small data, shared memory pool offset for large data
///
/// Memory layout (256 bytes total, 4 cache lines):
/// @code
///     Offset  Size  Field
///     0       4     msgId (event name hash or user-defined ID)
///     4       4     dataSize (payload size in bytes)
///     8       8     timestamp (steady_clock nanoseconds)
///     16      1     domain (MessageDomain enum)
///     17      1     flags (MessageFlags bitmask)
///     18      2     requestId (for request/response correlation, 0 for events)
///     20      4     poolOffset (offset in SharedMemoryPool if LargePayload flag set)
///     24      232   inlineData (payload if <= 232 bytes)
/// @endcode
struct Message {
    uint32_t msgId;                     ///< Event name hash or user-defined message ID
    uint32_t dataSize;                  ///< Payload size in bytes
    uint64_t timestamp;                 ///< Timestamp (steady_clock nanoseconds)
    
    MessageDomain domain;               ///< Message domain for categorization
    MessageFlags flags;                 ///< Message flags (LargePayload, IsRequest, etc.)
    uint16_t requestId;                 ///< Request/response correlation ID (0 for events)
    uint32_t poolOffset;                ///< Offset in SharedMemoryPool (if LargePayload)

    // Header: 4 + 4 + 8 + 1 + 1 + 2 + 4 = 24 bytes
    // Inline data: 232 bytes
    // Total: 256 bytes (fits in 4 cache lines)
    static constexpr size_t INLINE_SIZE = 232;

    uint8_t inlineData[INLINE_SIZE];    ///< Inline payload (if dataSize <= INLINE_SIZE)

    /// Default constructor
    Message() noexcept
        : msgId(0)
        , dataSize(0)
        , timestamp(0)
        , domain(MessageDomain::Global)
        , flags(MessageFlags::None)
        , requestId(0)
        , poolOffset(0) {
        std::memset(inlineData, 0, INLINE_SIZE);
    }

    /// Check if this message uses external pool storage
    [[nodiscard]] bool uses_pool() const noexcept {
        return has_flag(flags, MessageFlags::LargePayload);
    }

    /// Check if this is a request message
    [[nodiscard]] bool is_request() const noexcept {
        return has_flag(flags, MessageFlags::IsRequest);
    }

    /// Check if this is a response message
    [[nodiscard]] bool is_response() const noexcept {
        return has_flag(flags, MessageFlags::IsResponse);
    }
};

static_assert(sizeof(Message) == 256, "Message should be 256 bytes for cache efficiency");
static_assert(std::is_trivially_copyable_v<Message>, "Message must be trivially copyable for shared memory");

//=============================================================================
// Channel - Lock-free IPC channel
//=============================================================================

/// @brief High-performance lock-free channel for cross-process communication
///
/// This channel is optimized for the common pattern where one process
/// continuously sends data to another process.
///
/// Thread safety:
/// - Only ONE thread in the producer process should call send()
/// - Only ONE thread in the consumer process should call receive()
/// - Multiple producers or consumers will cause data corruption!
///
/// Performance characteristics:
/// - send(): ~50-100 ns (no locks, no syscalls)
/// - receive(): ~50-100 ns polling, ~1 us blocking
/// - Throughput: millions of messages per second
class LAGER_EXT_API Channel {
public:
    //-------------------------------------------------------------------------
    // Factory Methods
    //-------------------------------------------------------------------------

    /// Create the channel as producer (creates shared memory)
    /// @param name Unique channel name
    /// @param capacity Number of messages the queue can hold
    /// @return Channel instance, nullptr on failure
    static std::unique_ptr<Channel> create(const std::string& name, size_t capacity = DEFAULT_CAPACITY);

    /// Open the channel as consumer (attaches to existing shared memory)
    /// @param name Channel name (must match producer)
    /// @return Channel instance, nullptr on failure
    static std::unique_ptr<Channel> open(const std::string& name);

    //-------------------------------------------------------------------------
    // Producer Operations (Non-blocking, like PostMessage)
    //-------------------------------------------------------------------------

    /// Post a message to the queue (producer only, non-blocking)
    /// @param msgId Message type identifier
    /// @param data Message data (will be serialized)
    /// @param domain Message domain for categorization (default: Global)
    /// @return true if message was queued, false if queue is full
    /// @note This is non-blocking - returns immediately after queuing (like PostMessage)
    /// @note Large payloads (>232 bytes) require a shared memory pool
    bool post(uint32_t msgId, const ImmerValue& data = {}, MessageDomain domain = MessageDomain::Global);

    /// Post raw bytes to the queue (producer only, no serialization, non-blocking)
    /// @param msgId Message type identifier
    /// @param data Pointer to data
    /// @param size Data size in bytes
    /// @param domain Message domain for categorization (default: Global)
    /// @return true if message was queued
    bool postRaw(uint32_t msgId, const void* data, size_t size, MessageDomain domain = MessageDomain::Global);

    /// Check if queue has space for more messages
    bool canPost() const;

    /// Get number of messages waiting to be consumed
    size_t pendingCount() const;

    //-------------------------------------------------------------------------
    // Consumer Operations
    //-------------------------------------------------------------------------

    /// Received message structure with full metadata
    struct ReceivedMessage {
        uint32_t msgId;             ///< Message ID (event name hash or user-defined)
        ImmerValue data;                 ///< Deserialized payload
        uint64_t timestamp;         ///< Message timestamp
        MessageDomain domain;       ///< Message domain
        MessageFlags flags;         ///< Message flags
        uint16_t requestId;         ///< Request/response correlation ID

        ReceivedMessage() noexcept
            : msgId(0)
            , timestamp(0)
            , domain(MessageDomain::Global)
            , flags(MessageFlags::None)
            , requestId(0) {}
    };

    /// Receive a message (consumer only, non-blocking)
    /// @return Message if available, std::nullopt if queue is empty
    std::optional<ReceivedMessage> tryReceive();

    /// Receive a message (consumer only, blocking)
    /// @param timeout Maximum time to wait
    /// @return Message if received, std::nullopt on timeout
    std::optional<ReceivedMessage> receive(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

    /// Receive raw bytes (consumer only, no deserialization)
    /// @param outMsgId Receives the message ID
    /// @param outData Buffer to receive data
    /// @param maxSize Maximum bytes to copy
    /// @return Actual data size, 0 if queue empty, -1 if buffer too small
    int tryReceiveRaw(uint32_t& outMsgId, void* outData, size_t maxSize);

    //-------------------------------------------------------------------------
    // Properties
    //-------------------------------------------------------------------------

    /// Get channel name
    const std::string& name() const;

    /// Check if this is the producer side
    bool isProducer() const;

    /// Get queue capacity
    size_t capacity() const;

    /// Get last error message
    const std::string& lastError() const;

    //-------------------------------------------------------------------------
    // Lifecycle
    //-------------------------------------------------------------------------

    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) noexcept;
    Channel& operator=(Channel&&) noexcept;

private:
    Channel();
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// ChannelPair - Bidirectional channel pair
//=============================================================================

/// @brief Bidirectional communication using two channels
///
/// Creates two channels for request/reply pattern:
/// - Channel A->B: Process A sends, Process B receives
/// - Channel B->A: Process B sends, Process A receives
///
/// Usage:
/// @code
///     // Process A (creates the channel pair)
///     auto pair = ChannelPair::create("MyPair");
///     pair->post(MSG_REQUEST, data);  // Non-blocking
///     auto reply = pair->receive();
///
///     // Process B (connects to existing pair)
///     auto pair = ChannelPair::connect("MyPair");
///     auto request = pair->receive();
///     pair->post(MSG_REPLY, response);
///
///     // For synchronous request-response (like SendMessage):
///     auto result = pair->send(MSG_QUERY, queryData);  // Blocking
/// @endcode
class LAGER_EXT_API ChannelPair {
public:
    /// Create the channel pair (creates both underlying channels)
    /// @param name Unique pair name
    /// @param capacity Number of messages each channel can hold
    /// @return ChannelPair instance, nullptr on failure
    static std::unique_ptr<ChannelPair> create(const std::string& name, size_t capacity = DEFAULT_CAPACITY);

    /// Connect to an existing channel pair
    /// @param name Pair name (must match creator)
    /// @return ChannelPair instance, nullptr on failure
    static std::unique_ptr<ChannelPair> connect(const std::string& name);

    /// Post a message to the other endpoint (non-blocking, like PostMessage)
    /// @return true if message was queued
    bool post(uint32_t msgId, const ImmerValue& data = {});

    /// Post raw binary data to the other endpoint (non-blocking, zero-copy)
    /// @param msgId Message type identifier
    /// @param data Pointer to data
    /// @param size Data size in bytes
    /// @return true if message was queued
    bool postRaw(uint32_t msgId, const void* data, size_t size);

    /// Receive a message from the other endpoint (non-blocking)
    std::optional<Channel::ReceivedMessage> tryReceive();

    /// Receive raw binary data from the other endpoint (non-blocking, zero-copy)
    /// @param outMsgId Receives the message ID
    /// @param outData Buffer to receive data
    /// @param maxSize Maximum bytes to copy
    /// @return Actual data size, 0 if queue empty, -1 if buffer too small
    int tryReceiveRaw(uint32_t& outMsgId, void* outData, size_t maxSize);

    /// Receive a message from the other endpoint (blocking)
    std::optional<Channel::ReceivedMessage>
    receive(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

    /// Synchronous send with reply (blocking, like SendMessage)
    /// Sends a message and waits for a reply with matching correlation
    /// @return Response value, or nullopt on timeout
    std::optional<ImmerValue> send(uint32_t msgId, const ImmerValue& data,
                              std::chrono::milliseconds timeout = std::chrono::seconds(30));

    const std::string& name() const;

    /// Check if this is the creator side (called create())
    bool isCreator() const;

    const std::string& lastError() const;

    ~ChannelPair();
    ChannelPair(const ChannelPair&) = delete;
    ChannelPair& operator=(const ChannelPair&) = delete;
    ChannelPair(ChannelPair&&) noexcept;
    ChannelPair& operator=(ChannelPair&&) noexcept;

private:
    ChannelPair();
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ipc
} // namespace lager_ext
