// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file ipc_message.h
/// @brief Message domain and protocol types for IPC communication
///
/// This header defines:
/// - Message domains for categorizing IPC messages (Global/Document/Property/Asset)
/// - Protocol constants for message types
///
/// Design Philosophy:
/// - Lightweight domain classification (like xeditor's communicator pattern)
/// - No redundant envelope (uses existing ipc::Message structure)
/// - FNV-1a hash for compile-time event name hashing

#pragma once

#include <lager_ext/lager_ext_config.h>
#include <lager_ext/api.h>

#include <cstdint>
#include <string_view>

namespace lager_ext {
namespace ipc {

//=============================================================================
// Message Domain
//=============================================================================

/// @brief Message domain for categorizing IPC messages
///
/// Domains provide logical grouping of messages similar to xeditor's
/// GlobalCommunicator/PreviewCommunicator pattern, but without the overhead
/// of multiple class hierarchies.
///
/// Usage:
/// @code
///     LAGER_EXT_IPC_EVENT_DOMAIN(Document, DocumentSaved,
///         std::string doc_id;
///         std::string path;
///     , ...);
///
///     // Subscribe with domain filter
///     bus.subscribe_domain(MessageDomain::Document, handler);
/// @endcode
enum class MessageDomain : uint8_t {
    Global = 0,      ///< Global/process-level messages (single instance per process pair)
    Document = 1,    ///< Document-level messages (one per document/window)
    Property = 2,    ///< Property panel messages
    Asset = 3,       ///< Asset-related messages
    Custom = 255     ///< User-defined domain
};

/// @brief Convert domain to string name (for debugging)
constexpr std::string_view domain_name(MessageDomain d) noexcept {
    switch (d) {
        case MessageDomain::Global:   return "Global";
        case MessageDomain::Document: return "Document";
        case MessageDomain::Property: return "Property";
        case MessageDomain::Asset:    return "Asset";
        case MessageDomain::Custom:   return "Custom";
        default:                      return "Unknown";
    }
}

//=============================================================================
// Message Flags (encoded in Message::flags field)
//=============================================================================

/// @brief Flags for IPC messages
///
/// These flags are stored in the high bits of msgId or a dedicated flags field.
enum class MessageFlags : uint8_t {
    None = 0,
    
    /// Message uses external pool for large payload (>240 bytes)
    /// When set, Message::inlineData contains pool offset instead of actual data
    LargePayload = 1 << 0,
    
    /// Message is a request expecting a response
    IsRequest = 1 << 1,
    
    /// Message is a response to a previous request
    IsResponse = 1 << 2,
    
    /// Reserved for future use
    Reserved = 1 << 7
};

inline constexpr MessageFlags operator|(MessageFlags a, MessageFlags b) noexcept {
    return static_cast<MessageFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline constexpr MessageFlags operator&(MessageFlags a, MessageFlags b) noexcept {
    return static_cast<MessageFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline constexpr bool has_flag(MessageFlags flags, MessageFlags test) noexcept {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(test)) != 0;
}

//=============================================================================
// FNV-1a Hash (compile-time event name hashing)
//=============================================================================

namespace detail {

/// @brief FNV-1a hash for event name lookup
///
/// This allows O(1) dispatch based on event name at runtime,
/// while also enabling compile-time hash computation.
///
/// Usage:
/// @code
///     constexpr uint32_t hash = fnv1a_hash32("DocumentSaved");
///     // hash can be used as msgId
/// @endcode
constexpr uint32_t fnv1a_hash32(std::string_view sv) noexcept {
    uint32_t hash = 0x811c9dc5u;
    for (char c : sv) {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        hash *= 0x01000193u;
    }
    return hash;
}

} // namespace detail

} // namespace ipc
} // namespace lager_ext
