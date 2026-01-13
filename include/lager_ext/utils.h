// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file utils.h
/// @brief Utility functions for lager_ext types.
///
/// This file provides conversion utilities between MutableValue and Value types,
/// as well as other common operations.
///
/// ## Performance Considerations
///
/// ### Direct Value Construction vs MutableValue + Conversion
///
/// | Scenario | Direct Value | MutableValue -> Value |
/// |----------|--------------|----------------------|
/// | Single construction | ⭐ Faster | Slower (2x alloc) |
/// | Many updates then freeze | Slower (COW overhead) | ⭐ Faster |
/// | Deep tree building | Slower (immer::box per node) | ⭐ Faster |
/// | Read-heavy after build | Same | Same |
///
/// **Recommendation:**
/// - Use `Value` directly for: simple, mostly-read data structures
/// - Use `MutableValue` + `to_value()` for: complex tree building, reflection data,
///   or scenarios with many intermediate modifications before the final immutable state
///
/// ## Example
/// ```cpp
/// // Build mutable tree from reflection
/// MutableValue root = MutableValue::make_map();
/// root.set("name", "Player");
/// root.set("health", 100);
/// root.set("position", Vec3{1.0f, 2.0f, 3.0f});
///
/// // Convert to immutable Value for lager store
/// Value immutable = to_value(root);
/// ```

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/mutable_value.h>
#include <lager_ext/value.h>

namespace lager_ext {

// ============================================================
// MutableValue -> Value Conversion
// ============================================================

/// Convert a MutableValue to an immutable Value.
///
/// This performs a deep copy, converting:
/// - MutableValueMap -> ValueMap (immer::map)
/// - MutableValueVector -> ValueVector (immer::vector)
/// - MutableBoxedMat3/MutableBoxedMat4x3 -> boxed_mat3/boxed_mat4x3 (immer::box)
/// - All other types are copied directly
///
/// @param mv The MutableValue to convert
/// @return An immutable Value with the same structure and data
///
/// @note This is an O(N) operation where N is the total number of nodes.
///       For large trees, consider whether you need full immutability.
[[nodiscard]] LAGER_EXT_API Value to_value(const MutableValue& mv);

/// Convert a MutableValue to Value (move semantics variant).
/// Same as above but may avoid some copies for string data.
[[nodiscard]] LAGER_EXT_API Value to_value(MutableValue&& mv);

// ============================================================
// Value -> MutableValue Conversion
// ============================================================

/// Convert an immutable Value to a MutableValue.
///
/// This performs a deep copy, converting:
/// - ValueMap -> MutableValueMap (tsl::robin_map)
/// - ValueVector -> MutableValueVector (std::vector)
/// - boxed_mat3/boxed_mat4x3 -> MutableBoxedMat3/MutableBoxedMat4x3 (unique_ptr)
/// - All other types are copied directly
///
/// @param v The Value to convert
/// @return A MutableValue with the same structure and data
///
/// @note Use this when you need to modify an immutable Value tree.
[[nodiscard]] LAGER_EXT_API MutableValue to_mutable_value(const Value& v);

} // namespace lager_ext
