// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file windows_message.h
/// @brief Windows Message forwarding via high-performance IPC
///
/// This header provides a way to forward Windows messages across processes
/// using the lock-free shared memory channel instead of SendMessage/PostMessage.
///
/// Performance advantage:
/// - SendMessage/PostMessage: ~5-20 μs (kernel transition)
/// - IPC Channel: ~0.1-0.6 μs (user-mode only)
///
/// Usage:
/// @code
///   // Process A (Sender)
///   WindowsMessageBridge bridge("my_channel", WindowsMessageBridge::Role::Sender);
///   bridge.forward(WM_USER + 1, wParam, lParam);  // Non-blocking
///   
///   // Process B (Receiver)
///   WindowsMessageBridge bridge("my_channel", WindowsMessageBridge::Role::Receiver);
///   bridge.on_message([](UINT msg, WPARAM w, LPARAM l) {
///       // Handle as if it came from Windows message queue
///   });
///   bridge.poll();  // Call in your message loop
///   
///   // Or dispatch to a real window:
///   bridge.dispatch_to(hWnd);
///   bridge.poll();  // Will call SendMessage(hWnd, msg, w, l)
/// @endcode
///
/// Integration with existing WndProc:
/// @code
///   // In your WndProc, forward certain messages to another process:
///   LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
///       if (msg >= WM_USER && msg < WM_USER + 100) {
///           g_bridge.forward(msg, wParam, lParam);
///       }
///       return DefWindowProc(hWnd, msg, wParam, lParam);
///   }
/// @endcode

#pragma once

#include <lager_ext/lager_ext_config.h>

#ifdef LAGER_EXT_ENABLE_IPC

#include <lager_ext/api.h>
#include <lager_ext/ipc.h>

#ifdef _WIN32
#include <Windows.h>
#else
// Provide type definitions for non-Windows platforms (for compilation only)
using UINT = unsigned int;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using LRESULT = intptr_t;
using HWND = void*;
#endif

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace lager_ext {
namespace ipc {

// ============================================================================
// Windows Message Structure (24 bytes, fits inline in IPC Message)
// ============================================================================

/// @brief Packed Windows message for IPC transfer
struct alignas(8) WindowsMessageData {
    uint32_t message;    ///< UINT message
    uint32_t reserved;   ///< Padding for alignment
    uint64_t wParam;     ///< WPARAM (64-bit on x64)
    int64_t  lParam;     ///< LPARAM (64-bit on x64, signed for pointer math)
    
    // Total: 24 bytes (fits well within 232-byte inline limit)
};

static_assert(sizeof(WindowsMessageData) == 24, "WindowsMessageData should be 24 bytes");

// ============================================================================
// Windows Message Bridge
// ============================================================================

/// @brief High-performance cross-process Windows message forwarding
///
/// This class provides a way to forward Windows messages between processes
/// using the lock-free IPC channel, achieving 10-60x better latency than
/// native SendMessage/PostMessage.
///
/// Thread Safety: NOT thread-safe. Use from a single thread (typically UI thread).
class LAGER_EXT_API WindowsMessageBridge {
public:
    /// @brief Role in the IPC connection
    enum class Role {
        Sender,     ///< Forwards messages to remote process
        Receiver,   ///< Receives messages from remote process
        Bidirectional ///< Both send and receive
    };

    /// @brief Message handler callback type
    /// @param message The Windows message ID
    /// @param wParam The WPARAM value
    /// @param lParam The LPARAM value
    /// @return Optional LRESULT (for synchronous handling)
    using MessageHandler = std::function<std::optional<LRESULT>(UINT message, WPARAM wParam, LPARAM lParam)>;

    /// @brief Simple message handler (no return value)
    using SimpleHandler = std::function<void(UINT message, WPARAM wParam, LPARAM lParam)>;

    /// @brief Message filter predicate
    using MessageFilter = std::function<bool(UINT message)>;

    // ========================================================================
    // Construction
    // ========================================================================

    /// @brief Create a Windows message bridge
    /// @param channel_name Unique name for the IPC channel
    /// @param role Role of this process in the communication
    /// @param capacity Queue capacity (default 4096 messages)
    WindowsMessageBridge(std::string_view channel_name, Role role,
                         std::size_t capacity = DEFAULT_CAPACITY);
    
    ~WindowsMessageBridge();

    WindowsMessageBridge(const WindowsMessageBridge&) = delete;
    WindowsMessageBridge& operator=(const WindowsMessageBridge&) = delete;
    WindowsMessageBridge(WindowsMessageBridge&&) noexcept;
    WindowsMessageBridge& operator=(WindowsMessageBridge&&) noexcept;

    // ========================================================================
    // Sending Messages (Sender/Bidirectional role)
    // ========================================================================

    /// @brief Forward a Windows message to remote process (non-blocking)
    /// @param message The Windows message ID
    /// @param wParam The WPARAM value
    /// @param lParam The LPARAM value
    /// @return true if message was queued successfully
    bool forward(UINT message, WPARAM wParam, LPARAM lParam);

    /// @brief Forward a Windows message with filter
    /// @param message The Windows message ID
    /// @param wParam The WPARAM value  
    /// @param lParam The LPARAM value
    /// @param filter Only forward if filter returns true
    /// @return true if message was queued (or filtered out)
    bool forward_if(UINT message, WPARAM wParam, LPARAM lParam, MessageFilter filter);

    /// @brief Forward a range of messages (batch)
    /// @param messages Array of WindowsMessageData
    /// @param count Number of messages
    /// @return Number of messages successfully queued
    std::size_t forward_batch(const WindowsMessageData* messages, std::size_t count);

    // ========================================================================
    // Receiving Messages (Receiver/Bidirectional role)
    // ========================================================================

    /// @brief Set message handler callback
    /// @param handler Callback invoked for each received message
    void on_message(MessageHandler handler);

    /// @brief Set simple message handler (no return value)
    /// @param handler Callback invoked for each received message
    void on_message(SimpleHandler handler);

    /// @brief Set message handler for specific message range
    /// @param msg_begin Start of message range (inclusive)
    /// @param msg_end End of message range (exclusive)
    /// @param handler Callback for messages in range
    void on_message_range(UINT msg_begin, UINT msg_end, SimpleHandler handler);

#ifdef _WIN32
    /// @brief Dispatch received messages to a window
    /// @param hwnd Target window handle
    /// @param use_post Use PostMessage instead of SendMessage
    ///
    /// When set, poll() will dispatch messages to this window using
    /// SendMessage (synchronous) or PostMessage (asynchronous).
    void dispatch_to(HWND hwnd, bool use_post = false);
#endif

    /// @brief Poll for incoming messages (non-blocking)
    /// @return Number of messages processed
    std::size_t poll();

    /// @brief Poll with timeout
    /// @param timeout Maximum time to wait for messages
    /// @return Number of messages processed
    std::size_t poll(std::chrono::milliseconds timeout);

    // ========================================================================
    // Properties
    // ========================================================================

    /// @brief Check if connected to remote process
    [[nodiscard]] bool connected() const;

    /// @brief Get the channel name
    [[nodiscard]] const std::string& channel_name() const;

    /// @brief Get last error message
    [[nodiscard]] const std::string& last_error() const;

    /// @brief Get number of messages sent
    [[nodiscard]] std::size_t messages_sent() const;

    /// @brief Get number of messages received
    [[nodiscard]] std::size_t messages_received() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Utility: Message ID for IPC protocol
// ============================================================================

/// @brief Reserved message ID for Windows message forwarding
constexpr uint32_t WINDOWS_MSG_FORWARD = 0xF0000001;

} // namespace ipc
} // namespace lager_ext

#endif // LAGER_EXT_ENABLE_IPC
