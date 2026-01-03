// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_core.h
/// @brief Core path traversal engine for Value trees.
///
/// This file provides the fundamental path traversal functions used by
/// PathLens, ZoomedValue, and other high-level path abstractions.
///
/// ## Path Types
///
/// - **PathView**: Zero-allocation path for static/literal paths (recommended for most cases)
/// - **Path**: Owning path for dynamic paths (when keys come from runtime)
///
/// All path functions accept `PathView`, and `Path` implicitly converts to `PathView`.
///
/// ## Usage Examples
///
/// ```cpp
/// using namespace std::string_view_literals;
///
/// // Static path (zero allocation)
/// auto val = get_at_path(root, {{"users"sv, 0, "name"sv}});
///
/// // Dynamic path
/// Path path;
/// path.push_back(get_key());
/// path.push_back(0);
/// auto val = get_at_path(root, path);
/// ```
///
/// For most use cases, prefer the unified `path::` namespace in <lager_ext/path.h>.
/// Use this header directly only when you need:
/// - Maximum performance (inline functions)
/// - Low-level control without lens overhead
/// - Building custom path abstractions

#pragma once

#include <lager_ext/value.h>
#include <lager_ext/path_types.h>
#include <lager_ext/api.h>

namespace lager_ext {

// ============================================================
// Detail namespace - Internal helpers (not part of public API)
// ============================================================

namespace detail {

/// Get value at a single path element (key or index)
/// @note Internal helper - prefer get_at_path() for public use
/// @note Uses transparent lookup for zero-allocation string_view access
[[nodiscard]] inline Value get_at_path_element(const Value& current, const PathElement& elem)
{
    if (auto* key = std::get_if<std::string_view>(&elem)) {
        return current.at(*key);  // Zero-allocation: uses transparent lookup
    } else {
        return current.at(std::get<std::size_t>(elem));
    }
}

/// Set value at a single path element (key or index)
/// @note Internal helper - prefer set_at_path() for public use
/// @note Now uses Value::set(string_view) overload for cleaner code
[[nodiscard]] inline Value set_at_path_element(const Value& current, const PathElement& elem, Value new_val)
{
    if (auto* key = std::get_if<std::string_view>(&elem)) {
        return current.set(*key, std::move(new_val));  // Uses set(string_view) overload
    } else {
        return current.set(std::get<std::size_t>(elem), std::move(new_val));
    }
}

/// Erase a key from a map value
/// @note Internal helper
[[nodiscard]] inline Value erase_key_from_map(const Value& val, std::string_view key)
{
    if (auto* m = val.get_if<ValueMap>()) {
        return m->erase(std::string{key});
    }
    return val;
}

/// Check if a path element can be accessed in the given value
/// @note Internal helper - prefer is_valid_path() for public use
/// @note Uses transparent lookup for zero-allocation string_view access
[[nodiscard]] inline bool can_access_element(const Value& val, const PathElement& elem)
{
    if (auto* key = std::get_if<std::string_view>(&elem)) {
        if (const auto* map = val.get_if<ValueMap>()) {
            return map->count(*key) > 0;  // Zero-allocation: uses transparent lookup
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
/// @param path The path to follow (PathView for zero-copy, or Path which implicitly converts)
/// @return The value at the path, or null Value if any step fails
/// @note This is an inline function for maximum performance
[[nodiscard]] inline Value get_at_path(const Value& root, PathView path)
{
    Value current = root;
    for (const auto& elem : path) {
        current = detail::get_at_path_element(current, elem);
        if (current.is_null()) [[unlikely]] {
            break;  // Early exit on null (path errors are uncommon)
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
[[nodiscard]] LAGER_EXT_API Value set_at_path(const Value& root, PathView path, Value new_val);

/// @brief Set value at a path with auto-vivification
/// Creates intermediate maps/vectors as needed when path doesn't exist.
/// @param root The root value
/// @param path The path to the target location
/// @param new_val The new value to set
/// @return New root with the update applied
/// @example
///   Value result = set_at_path_vivify(Value{}, {{"a"sv, "b"sv, "c"sv}}, Value{100});
///   // result: {"a": {"b": {"c": 100}}}
[[nodiscard]] LAGER_EXT_API Value set_at_path_vivify(const Value& root, PathView path, Value new_val);

/// @brief Erase value at a path
/// For maps: actually erases the key.
/// For vectors/arrays: sets to null (cannot shrink without reindexing).
/// @param root The root value
/// @param path The path to the value to erase
/// @return New root with the element erased
[[nodiscard]] LAGER_EXT_API Value erase_at_path(const Value& root, PathView path);

// ============================================================
// Path Validation
// ============================================================

/// @brief Check if an entire path can be traversed
/// @param root The root value
/// @param path The path to check
/// @return true if all elements in the path exist and can be accessed
[[nodiscard]] LAGER_EXT_API bool is_valid_path(const Value& root, PathView path);

/// @brief Get the depth of valid traversal for a path
/// @param root The root value
/// @param path The path to check
/// @return Number of path elements that can be successfully traversed (0 to path.size())
[[nodiscard]] LAGER_EXT_API std::size_t valid_path_depth(const Value& root, PathView path);

} // namespace lager_ext
