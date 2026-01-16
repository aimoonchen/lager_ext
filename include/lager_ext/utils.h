// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file utils.h
/// @brief Utility functions for lager_ext types.
///
/// This file provides conversion utilities between MutableValue and ImmerValue types,
/// as well as other common operations.
///
/// ## Performance Considerations
///
/// ### Direct ImmerValue Construction vs MutableValue + Conversion
///
/// | Scenario | Direct ImmerValue | MutableValue -> ImmerValue |
/// |----------|--------------|----------------------|
/// | Single construction | [*] Faster | Slower (2x alloc) |
/// | Many updates then freeze | Slower (COW overhead) | [*] Faster |
/// | Deep tree building | Slower (immer::box per node) | [*] Faster |
/// | Read-heavy after build | Same | Same |
///
/// **Recommendation:**
/// - Use `ImmerValue` directly for: simple, mostly-read data structures
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
/// // Convert to immutable ImmerValue for lager store
/// ImmerValue immutable = to_value(root);
/// ```

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/mutable_value.h>
#include <lager_ext/value.h>

namespace lager_ext {

// ============================================================
// MutableValue -> ImmerValue Conversion
// ============================================================

/// Convert a MutableValue to an immutable ImmerValue.
///
/// This performs a deep copy, converting:
/// - MutableValueMap -> ValueMap (immer::map)
/// - MutableValueVector -> ValueVector (immer::vector)
/// - MutableMat3Ptr/MutableMat4x3Ptr -> boxed_mat3/boxed_mat4x3 (immer::box)
/// - All other types are copied directly
///
/// @param mv The MutableValue to convert
/// @return An immutable ImmerValue with the same structure and data
///
/// @note This is an O(N) operation where N is the total number of nodes.
///       For large trees, consider whether you need full immutability.
[[nodiscard]] LAGER_EXT_API ImmerValue to_value(const MutableValue& mv);

/// Convert a MutableValue to ImmerValue (move semantics variant).
/// Same as above but may avoid some copies for string data.
[[nodiscard]] LAGER_EXT_API ImmerValue to_value(MutableValue&& mv);

// ============================================================
// ImmerValue -> MutableValue Conversion
// ============================================================

/// Convert an immutable ImmerValue to a MutableValue.
///
/// This performs a deep copy, converting:
/// - ValueMap -> MutableValueMap (tsl::robin_map)
/// - ValueVector -> MutableValueVector (std::vector)
/// - boxed_mat3/boxed_mat4x3 -> MutableMat3Ptr/MutableMat4x3Ptr (unique_ptr)
/// - All other types are copied directly
///
/// @param v The ImmerValue to convert
/// @return A MutableValue with the same structure and data
///
/// @note Use this when you need to modify an immutable ImmerValue tree.
[[nodiscard]] LAGER_EXT_API MutableValue to_mutable_value(const ImmerValue& v);

} // namespace lager_ext
