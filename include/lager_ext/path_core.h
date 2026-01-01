// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_core.h
/// @brief Core path traversal engine for Value trees.
///
/// This file provides the fundamental path traversal functions used by
/// PathLens, ZoomedValue, and other high-level path abstractions.
///
/// For most use cases, prefer the unified `path::` namespace in <lager_ext/path.h>.
/// Use this header directly only when you need:
/// - Maximum performance (inline functions)
/// - Low-level control without lens overhead
/// - Building custom path abstractions

#pragma once

#include <lager_ext/value.h>
#include <lager_ext/api.h>

namespace lager_ext {

// ============================================================
// Detail namespace - Internal helpers (not part of public API)
// ============================================================

namespace detail {

/// Get value at a single path element (key or index)
/// @note Internal helper - prefer get_at_path() for public use
[[nodiscard]] inline Value get_at_path_element(const Value& current, const PathElement& elem)
{
    if (auto* key = std::get_if<std::string>(&elem)) {
        return current.at(*key);
    } else {
        return current.at(std::get<std::size_t>(elem));
    }
}

/// Set value at a single path element (key or index)
/// @note Internal helper - prefer set_at_path() for public use
[[nodiscard]] inline Value set_at_path_element(const Value& current, const PathElement& elem, Value new_val)
{
    if (auto* key = std::get_if<std::string>(&elem)) {
        return current.set(*key, std::move(new_val));
    } else {
        return current.set(std::get<std::size_t>(elem), std::move(new_val));
    }
}

/// Erase a key from a map value
/// @note Internal helper
[[nodiscard]] inline Value erase_key_from_map(const Value& val, const std::string& key)
{
    if (auto* m = val.get_if<ValueMap>()) {
        return m->erase(key);
    }
    return val;
}

/// Check if a path element can be accessed in the given value
/// @note Internal helper - prefer is_valid_path() for public use
[[nodiscard]] inline bool can_access_element(const Value& val, const PathElement& elem)
{
    if (auto* key = std::get_if<std::string>(&elem)) {
        if (const auto* map = val.get_if<ValueMap>()) {
            return map->count(*key) > 0;
        }
        return false;
    } else {
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

} // namespace detail

// ============================================================
// Public API - Core Path Operations
// ============================================================

/// @brief Get value at a path
/// @param root The root value to traverse
/// @param path The path to follow
/// @return The value at the path, or null Value if any step fails
/// @note This is an inline function for maximum performance
[[nodiscard]] inline Value get_at_path(const Value& root, const Path& path)
{
    Value current = root;
    for (const auto& elem : path) {
        current = detail::get_at_path_element(current, elem);
        if (current.is_null()) {
            break;  // Early exit on null
        }
    }
    return current;
}

/// @brief Set value at a path (strict mode)
/// @param root The root value
/// @param path The path to the target location
/// @param new_val The new value to set
/// @return New root with the update applied
/// @note If the path doesn't exist, the operation may silently fail.
///       Use set_at_path_vivify() to auto-create intermediate nodes.
[[nodiscard]] LAGER_EXT_API Value set_at_path(const Value& root, const Path& path, Value new_val);

/// @brief Set value at a path with auto-vivification
/// Creates intermediate maps/vectors as needed when path doesn't exist.
/// @param root The root value
/// @param path The path to the target location
/// @param new_val The new value to set
/// @return New root with the update applied
/// @example
///   Value result = set_at_path_vivify(Value{}, {"a", "b", "c"}, Value{100});
///   // result: {"a": {"b": {"c": 100}}}
[[nodiscard]] LAGER_EXT_API Value set_at_path_vivify(const Value& root, const Path& path, Value new_val);

/// @brief Erase value at a path
/// For maps: actually erases the key.
/// For vectors/arrays: sets to null (cannot shrink without reindexing).
/// @param root The root value
/// @param path The path to the value to erase
/// @return New root with the element erased
[[nodiscard]] LAGER_EXT_API Value erase_at_path(const Value& root, const Path& path);

// ============================================================
// Path Validation
// ============================================================

/// @brief Check if an entire path can be traversed
/// @param root The root value
/// @param path The path to check
/// @return true if all elements in the path exist and can be accessed
[[nodiscard]] LAGER_EXT_API bool is_valid_path(const Value& root, const Path& path);

/// @brief Get the depth of valid traversal for a path
/// @param root The root value
/// @param path The path to check
/// @return Number of path elements that can be successfully traversed (0 to path.size())
[[nodiscard]] LAGER_EXT_API std::size_t valid_path_depth(const Value& root, const Path& path);

} // namespace lager_ext
