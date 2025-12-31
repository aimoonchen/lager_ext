// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_utils.h
/// @brief Common path traversal utilities shared by lens implementations.
///
/// This file provides low-level path traversal functions used by both
/// ErasedLens and LagerValueLens implementations, avoiding code duplication.

#pragma once

#include <lager_ext/value.h>
#include <lager_ext/api.h>

namespace lager_ext {

// ============================================================
// Simple inline utilities (kept in header for performance)
// ============================================================

/// Get value at a single path element (key or index)
/// Optimized: uses if-else instead of std::visit to avoid indirect call overhead
[[nodiscard]] inline Value get_at_path_element(const Value& current, const PathElement& elem)
{
    if (auto* key = std::get_if<std::string>(&elem)) {
        return current.at(*key);
    } else {
        return current.at(std::get<std::size_t>(elem));
    }
}

/// Set value at a single path element (key or index)
/// Optimized: uses if-else instead of std::visit to avoid indirect call overhead
[[nodiscard]] inline Value set_at_path_element(const Value& current, const PathElement& elem, Value new_val)
{
    if (auto* key = std::get_if<std::string>(&elem)) {
        return current.set(*key, std::move(new_val));
    } else {
        return current.set(std::get<std::size_t>(elem), std::move(new_val));
    }
}

/// Get value at a full path using direct traversal
/// @param root The root value
/// @param path The path to traverse
/// @return The value at the path, or monostate if any step fails
[[nodiscard]] inline Value get_at_path_direct(const Value& root, const Path& path)
{
    Value current = root;
    for (const auto& elem : path) {
        current = get_at_path_element(current, elem);
        if (current.is_null()) {
            break;  // Early exit on null
        }
    }
    return current;
}

/// Erase a key from a map value
/// @param val The value (should be a ValueMap)
/// @param key The key to erase
/// @return New map with the key removed, or original value if not a map
[[nodiscard]] inline Value erase_key_from_map(const Value& val, const std::string& key)
{
    if (auto* m = val.get_if<ValueMap>()) {
        return m->erase(key);
    }
    return val;
}

/// Check if a path element can be accessed in the given value
/// Optimized: uses if-else instead of std::visit to avoid indirect call overhead
/// @param val The value to check
/// @param elem The path element to access
/// @return true if the element can be accessed
[[nodiscard]] inline bool can_access(const Value& val, const PathElement& elem)
{
    if (auto* key = std::get_if<std::string>(&elem)) {
        // For map access, check if it's a map and has the key
        if (const auto* map = val.get_if<ValueMap>()) {
            return map->count(*key) > 0;
        }
        return false;
    } else {
        // For index access, check if it's a vector/array with valid index
        auto idx = std::get<std::size_t>(elem);
        if (const auto* vec = val.get_if<ValueVector>()) {
            return idx < vec->size();
        }
        if (const auto* arr = val.get_if<ValueArray>()) {
            return idx < arr->size();
        }
        return false;
    }
}

// ============================================================
// Complex functions (implemented in path_utils.cpp)
// ============================================================

/// Set value at a path using recursive traversal (internal helper)
/// @param root The root value
/// @param path The full path
/// @param path_index Current position in path (0 = start)
/// @param new_val The new value to set
/// @return New root with the update applied
[[nodiscard]] LAGER_EXT_API Value set_at_path_recursive(
    const Value& root,
    const Path& path,
    std::size_t path_index,
    Value new_val);

/// Set value at a full path using direct traversal
/// @param root The root value
/// @param path The path to the value
/// @param new_val The new value to set
/// @return New root with the update applied
[[nodiscard]] LAGER_EXT_API Value set_at_path_direct(const Value& root, const Path& path, Value new_val);

/// Set value at a single path element with auto-vivification
/// Creates a map if current value is null and key is string
/// @param current The current container value (may be null)
/// @param elem The path element
/// @param new_val The new value to set
/// @return New container with the element updated
[[nodiscard]] LAGER_EXT_API Value set_at_path_element_vivify(const Value& current, const PathElement& elem, Value new_val);

/// Set value at a path using recursive traversal with auto-vivification
/// @param root The root value
/// @param path The full path
/// @param path_index Current position in path (0 = start)
/// @param new_val The new value to set
/// @return New root with the update applied
[[nodiscard]] LAGER_EXT_API Value set_at_path_recursive_vivify(
    const Value& root,
    const Path& path,
    std::size_t path_index,
    Value new_val);

/// Set value at a full path with auto-vivification
/// Creates intermediate maps/vectors as needed when path doesn't exist
/// @param root The root value
/// @param path The path to the value
/// @param new_val The new value to set
/// @return New root with the update applied
/// @example
///   Value root{ValueMap{}};
///   auto result = set_at_path_vivify(root, {"a", "b", "c"}, Value{100});
///   // result: {"a": {"b": {"c": 100}}}
[[nodiscard]] LAGER_EXT_API Value set_at_path_vivify(const Value& root, const Path& path, Value new_val);

/// Erase value at a full path
/// For maps: actually erases the key
/// For vectors/arrays: sets to null (cannot shrink without reindexing)
/// @param root The root value
/// @param path The path to the value to erase
/// @return New root with the element erased
[[nodiscard]] LAGER_EXT_API Value erase_at_path_direct(const Value& root, const Path& path);

/// Check if an entire path can be traversed
/// @param root The root value
/// @param path The path to check
/// @return true if all elements in the path can be accessed
[[nodiscard]] LAGER_EXT_API bool is_valid_path(const Value& root, const Path& path);

/// Get the depth of valid traversal for a path
/// @param root The root value
/// @param path The path to check
/// @return Number of path elements that can be successfully traversed
[[nodiscard]] LAGER_EXT_API std::size_t valid_path_depth(const Value& root, const Path& path);

} // namespace lager_ext