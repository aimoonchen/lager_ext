// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file serialization.h
/// @brief Serialization utilities for ImmerValue type (Binary and JSON formats).
///
/// This file provides serialization and deserialization functions:
/// - Binary format: Compact, efficient for storage and network transfer
/// - JSON format: Human-readable, for configuration and debugging
///
/// Usage:
/// @code
///   #include <lager_ext/serialization.h>
///
///   // Binary serialization
///   ImmerValue data = ImmerValue::map({{"key", "value"}});
///   ByteBuffer buffer = serialize(data);
///   ImmerValue restored = deserialize(buffer);
///
///   // JSON serialization
///   std::string json = to_json(data, false);  // pretty-printed
///   ImmerValue parsed = from_json(json);
/// @endcode
///
/// Binary Format Type Tags (1 byte):
///   0x00 = null (monostate)
///   0x01 = int (4 bytes, little-endian)
///   0x02 = float (4 bytes, IEEE 754)
///   0x03 = double (8 bytes, IEEE 754)
///   0x04 = bool (1 byte: 0x00=false, 0x01=true)
///   0x05 = string (4-byte length + UTF-8 data)
///   0x06 = map (4-byte count + entries)
///   0x07 = vector (4-byte count + elements)
///   0x08 = array (4-byte count + elements)
///   0x09 = table (4-byte count + entries)
///   0x0A = int64 (8 bytes, little-endian)
///   0x10 = Vec2 (8 bytes, 2 floats)
///   0x11 = Vec3 (12 bytes, 3 floats)
///   0x12 = Vec4 (16 bytes, 4 floats)
///   0x13 = Mat3 (36 bytes, 9 floats)
///   0x14 = Mat4x3 (48 bytes, 12 floats)
///
/// Note: This header must be included separately from value.h if you need serialization.

#pragma once

#include "api.h"
#include "value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lager_ext {

// ============================================================
// Binary Serialization
// ============================================================
// Note: ByteBuffer is defined in value.h

/// Serialize ImmerValue to binary buffer
/// @param val The ImmerValue to serialize
/// @return Byte buffer containing serialized data
LAGER_EXT_API ByteBuffer serialize(const ImmerValue& val);

/// Deserialize ImmerValue from binary buffer
/// @param buffer The byte buffer to deserialize
/// @return Reconstructed ImmerValue, or null ImmerValue on error
/// @throws std::runtime_error on invalid data format
LAGER_EXT_API ImmerValue deserialize(const ByteBuffer& buffer);

/// Deserialize from raw pointer and size
/// @param data Pointer to serialized data
/// @param size Size of serialized data in bytes
/// @return Reconstructed ImmerValue
/// @note Useful for memory-mapped data or network buffers
LAGER_EXT_API ImmerValue deserialize(const uint8_t* data, std::size_t size);

// ============================================================
// Serialization Utilities
// ============================================================

/// Get serialized size without actually serializing
/// @param val The ImmerValue to measure
/// @return Number of bytes required for serialization
/// @note Useful for pre-allocating buffers
LAGER_EXT_API std::size_t serialized_size(const ImmerValue& val);

/// Serialize to pre-allocated buffer
/// @param val The ImmerValue to serialize
/// @param buffer Pointer to output buffer
/// @param buffer_size Size of output buffer
/// @return Number of bytes written
/// @note Buffer must have at least serialized_size(val) bytes
LAGER_EXT_API std::size_t serialize_to(const ImmerValue& val, uint8_t* buffer, std::size_t buffer_size);

// ============================================================
// JSON Serialization
// ============================================================

/// Convert ImmerValue to JSON string
/// @param val The ImmerValue to convert
/// @param compact If true, produce minimal output; if false, pretty-print with indentation
/// @return JSON string representation
///
/// Special handling for math types:
/// - Vec2, Vec3, Vec4: JSON arrays [x, y, ...]
/// - Mat3, Mat4x3: JSON arrays of floats (row-major)
///
/// Limitations:
/// - Numbers are always double precision (int64 may lose precision)
/// - Null, true, false are reserved keywords
LAGER_EXT_API std::string to_json(const ImmerValue& val, bool compact = false);

/// Parse JSON string to ImmerValue
/// @param json_str The JSON string to parse
/// @param error_out If provided, receives error message on failure
/// @return Parsed ImmerValue, or null ImmerValue on parse error
LAGER_EXT_API ImmerValue from_json(const std::string& json_str, std::string* error_out = nullptr);

} // namespace lager_ext
