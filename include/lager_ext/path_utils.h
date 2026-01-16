// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_utils.h
/// @brief Path traversal engine and unified Path API for Value trees.
///
/// This file provides:
/// 1. Core path traversal functions (get_at_path, set_at_path, etc.)
/// 2. Unified `path::` namespace for convenient path operations
///
/// ## Path Types (defined in path.h)
///
/// - **PathView**: Zero-allocation path for static/literal paths (recommended for most cases)
/// - **Path**: Owning path for dynamic paths (when keys come from runtime)
///
/// All path functions accept `PathView`, and `Path` implicitly converts to `PathView`.
///
/// ## Usage Examples
///
/// ### Core Functions (low-level)
/// ```cpp
/// using namespace std::string_view_literals;
/// auto val = get_at_path(root, {{"users"sv, 0, "name"sv}});
/// auto updated = set_at_path(root, {{"users"sv, 0, "age"sv}}, Value{30});
/// ```
///
/// ### Unified path:: API (recommended)
/// ```cpp
/// // String path
/// Value name = path::get(data, "/users/0/name");
///
/// // Variadic path
/// Value name = path::get(data, "users", 0, "name");
///
/// // Compile-time lens
/// auto lens = path::lens<"/users/0/name">();
/// ```

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/lager_lens.h>
#include <lager_ext/path.h>
#include <lager_ext/static_path.h>
#include <lager_ext/value.h>

namespace lager_ext {

// ============================================================
// Detail namespace - Internal helpers (not part of public API)
// ============================================================

namespace detail {

/// Get value at a single path element (key or index)
/// @note Internal helper - prefer get_at_path() for public use
/// @note Uses transparent lookup for zero-allocation string_view access
[[nodiscard]] inline Value get_at_path_element(const Value& current, const PathElement& elem) {
    if (auto* key = std::get_if<std::string_view>(&elem)) {
        return current.at(*key); // Zero-allocation: uses transparent lookup
    } else {
        return current.at(std::get<std::size_t>(elem));
    }
}

/// Set value at a single path element (key or index)
/// @note Internal helper - prefer set_at_path() for public use
/// @note Now uses Value::set(string_view) overload for cleaner code
[[nodiscard]] inline Value set_at_path_element(const Value& current, const PathElement& elem, Value new_val) {
    if (auto* key = std::get_if<std::string_view>(&elem)) {
        return current.set(*key, std::move(new_val)); // Uses set(string_view) overload
    } else {
        return current.set(std::get<std::size_t>(elem), std::move(new_val));
    }
}

/// Erase a key from a map value
/// @note Internal helper
/// @note Container Boxing: uses BoxedValueMap, unbox -> modify -> rebox
[[nodiscard]] inline Value erase_key_from_map(const Value& val, std::string_view key) {
    if (auto* boxed_map = val.get_if<BoxedValueMap>()) {
        auto new_map = boxed_map->get().erase(std::string{key});
        return Value{BoxedValueMap{std::move(new_map)}};
    }
    return val;
}

/// Check if a path element can be accessed in the given value
/// @note Internal helper - prefer is_valid_path() for public use
/// @note Uses transparent lookup for zero-allocation string_view access
/// @note Container Boxing: accesses BoxedValueMap/BoxedValueVector/BoxedValueArray
[[nodiscard]] inline bool can_access_element(const Value& val, const PathElement& elem) {
    if (auto* key = std::get_if<std::string_view>(&elem)) {
        if (const auto* boxed_map = val.get_if<BoxedValueMap>()) {
            return boxed_map->get().count(*key) > 0; // Zero-allocation: uses transparent lookup
        }
        return false;
    } else {
        auto idx = std::get<std::size_t>(elem);
        if (const auto* boxed_vec = val.get_if<BoxedValueVector>()) {
            return idx < boxed_vec->get().size();
        }
        if (const auto* boxed_arr = val.get_if<BoxedValueArray>()) {
            return idx < boxed_arr->get().size();
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
[[nodiscard]] LAGER_EXT_API Value get_at_path(const Value& root, PathView path);

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

// ============================================================
// Unified path:: Namespace - Convenient High-Level API
// ============================================================

/// @brief Unified path namespace - provides all path access patterns
namespace path {

// ============================================================
// Type Aliases
// ============================================================

/// Path lens type (type-erased for lager integration)
using Lens = LagerValueLens;

/// Path builder type (recommended for runtime paths)
using Builder = PathLens;

/// Path element (string key or size_t index)
using Element = PathElement;

/// Path container (vector of elements)
using PathVec = Path;

// ============================================================
// Unified Lens API
// ============================================================

/// @brief Create a lens from compile-time string literal (C++20 NTTP)
/// @tparam PathStr JSON Pointer style path (e.g., "/users/0/name")
/// @return Type-erased LagerValueLens
template <FixedString PathStr>
[[nodiscard]] Lens lens() {
    return static_path_lens<PathStr>();
}

/// @brief Create a lens from runtime string path
/// @param path_str JSON Pointer style path string
/// @return Type-erased LagerValueLens
[[nodiscard]] inline Lens lens(std::string_view path_str) {
    return lager_path_lens(Path{path_str});
}

/// @brief Create a lens from variadic path elements
template <PathElementType... Elems>
[[nodiscard]] auto lens(Elems&&... elements) {
    return static_path_lens(std::forward<Elems>(elements)...);
}

// ============================================================
// Builder-Style Path API
// ============================================================

/// @brief Create a path builder starting from root
[[nodiscard]] inline Builder builder() {
    return Builder{};
}

/// @brief Create a PathLens from variadic elements
template <PathElementType... Elems>
[[nodiscard]] Builder make(Elems&&... elements) {
    return make_path(std::forward<Elems>(elements)...);
}

// ============================================================
// Get - Read value at path
// ============================================================

/// @brief Get value at Path container
[[nodiscard]] inline Value get(const Value& data, const PathVec& path) {
    return get_at_path(data, path);
}

/// @brief Get value at string path
[[nodiscard]] inline Value get(const Value& data, std::string_view path_str) {
    return get_at_path(data, Path{path_str});
}

/// @brief Get value at variadic path
template <PathElementType... Elems>
[[nodiscard]] Value get(const Value& data, Elems&&... path_elements) {
    return get_at_path(data, make_path(std::forward<Elems>(path_elements)...).path());
}

// ============================================================
// Set - Write value at path (strict mode)
// ============================================================

/// @brief Set value at Path container
[[nodiscard]] inline Value set(const Value& data, const PathVec& path, Value new_value) {
    return set_at_path(data, path, std::move(new_value));
}

/// @brief Set value at string path
[[nodiscard]] inline Value set(const Value& data, std::string_view path_str, Value new_value) {
    return set_at_path(data, Path{path_str}, std::move(new_value));
}

/// @brief Set value at variadic path
/// @note Parameters: (data, path_elem1, path_elem2, ..., new_value)
/// @example set(data, "users", 0, "name", Value{"Alice"})
template <typename... Args>
    requires(sizeof...(Args) >= 2) // At least one path element + value
[[nodiscard]] Value set(const Value& data, Args&&... args) {
    // Extract the last argument as value, rest as path elements
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto args_tuple = std::forward_as_tuple(std::forward<Args>(args)...);
        constexpr std::size_t N = sizeof...(Args);
        Path p = make_path(std::get<Is>(std::move(args_tuple))...).path();
        return set_at_path(data, p, Value{std::get<N - 1>(std::move(args_tuple))});
    }(std::make_index_sequence<sizeof...(Args) - 1>{});
}

// ============================================================
// Over - Update value at path using a function
// ============================================================

/// @brief Update value at Path container using a function
template <typename Fn>
[[nodiscard]] Value over(const Value& data, const PathVec& path, Fn&& fn) {
    return set_at_path(data, path, std::forward<Fn>(fn)(get_at_path(data, path)));
}

/// @brief Update value at string path using a function
template <typename Fn>
[[nodiscard]] Value over(const Value& data, std::string_view path_str, Fn&& fn) {
    Path path{path_str};
    return set_at_path(data, path, std::forward<Fn>(fn)(get_at_path(data, path)));
}

/// @brief Update value at variadic path using a function
/// @note Parameters: (data, path_elem1, ..., path_elemN, fn)
template <typename Fn, PathElementType... Elems>
[[nodiscard]] Value over(const Value& data, Fn&& fn, Elems&&... path_elements) {
    auto path = make_path(std::forward<Elems>(path_elements)...).path();
    return set_at_path(data, path, std::forward<Fn>(fn)(get_at_path(data, path)));
}

// ============================================================
// Set Vivify - Write with auto-creation of intermediate nodes
// ============================================================

/// @brief Set value at Path container, creating intermediate nodes as needed
[[nodiscard]] inline Value set_vivify(const Value& data, const PathVec& path, Value new_value) {
    return set_at_path_vivify(data, path, std::move(new_value));
}

/// @brief Set value at string path with auto-vivification
[[nodiscard]] inline Value set_vivify(const Value& data, std::string_view path_str, Value new_value) {
    return set_at_path_vivify(data, Path{path_str}, std::move(new_value));
}

/// @brief Set value at variadic path with auto-vivification
/// @note Parameters: (data, path_elem1, ..., path_elemN, new_value)
template <typename... Args>
    requires(sizeof...(Args) >= 2)
[[nodiscard]] Value set_vivify(const Value& data, Args&&... args) {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto args_tuple = std::forward_as_tuple(std::forward<Args>(args)...);
        constexpr std::size_t N = sizeof...(Args);
        Path p = make_path(std::get<Is>(std::move(args_tuple))...).path();
        return set_at_path_vivify(data, p, Value{std::get<N - 1>(std::move(args_tuple))});
    }(std::make_index_sequence<sizeof...(Args) - 1>{});
}

// ============================================================
// Erase - Remove value at path
// ============================================================

/// @brief Erase value at Path container
/// For maps: erases the key. For vectors: sets to null.
[[nodiscard]] inline Value erase(const Value& data, const PathVec& path) {
    return erase_at_path(data, path);
}

/// @brief Erase value at string path
[[nodiscard]] inline Value erase(const Value& data, std::string_view path_str) {
    return erase_at_path(data, Path{path_str});
}

/// @brief Erase value at variadic path
template <PathElementType... Elems>
[[nodiscard]] Value erase(const Value& data, Elems&&... path_elements) {
    return erase_at_path(data, make_path(std::forward<Elems>(path_elements)...).path());
}

// ============================================================
// Exists - Check if path exists
// ============================================================

/// @brief Check if a Path container exists in the data
[[nodiscard]] inline bool exists(const Value& data, const PathVec& path) {
    return is_valid_path(data, path);
}

/// @brief Check if a string path exists in the data
[[nodiscard]] inline bool exists(const Value& data, std::string_view path_str) {
    return is_valid_path(data, Path{path_str});
}

/// @brief Check if a variadic path exists in the data
template <PathElementType... Elems>
[[nodiscard]] bool exists(const Value& data, Elems&&... path_elements) {
    return is_valid_path(data, make_path(std::forward<Elems>(path_elements)...).path());
}

// ============================================================
// Valid Depth - How far can we traverse?
// ============================================================

/// @brief Get how deep a path can be traversed
[[nodiscard]] inline std::size_t valid_depth(const Value& data, const PathVec& path) {
    return valid_path_depth(data, path);
}

/// @brief Get how deep a string path can be traversed
[[nodiscard]] inline std::size_t valid_depth(const Value& data, std::string_view path_str) {
    return valid_path_depth(data, Path{path_str});
}

// ============================================================
// Safe Access - Operations with detailed error handling
// ============================================================

/// @brief Safe get with detailed error information
[[nodiscard]] inline PathAccessResult safe_get(const Value& data, const PathVec& path) {
    return get_at_path_safe(data, path);
}

/// @brief Safe get at string path with detailed error information
[[nodiscard]] inline PathAccessResult safe_get(const Value& data, std::string_view path_str) {
    return get_at_path_safe(data, Path{path_str});
}

/// @brief Safe set with detailed error information
[[nodiscard]] inline PathAccessResult safe_set(const Value& data, const PathVec& path, Value new_value) {
    return set_at_path_safe(data, path, std::move(new_value));
}

/// @brief Safe set at string path with detailed error information
[[nodiscard]] inline PathAccessResult safe_set(const Value& data, std::string_view path_str, Value new_value) {
    return set_at_path_safe(data, Path{path_str}, std::move(new_value));
}

// ============================================================
// Path Utilities
// ============================================================

/// @brief Parse a string path into path elements
[[nodiscard]] inline PathVec parse(std::string_view path_str) {
    return Path{path_str};
}

/// @brief Convert path elements to string (e.g., ".users[0].name")
[[nodiscard]] inline std::string to_string(const PathVec& path) {
    return path.to_dot_notation();
}

/// @brief Convert path elements to JSON Pointer string (e.g., "/users/0/name")
[[nodiscard]] inline std::string to_json_pointer(const PathVec& path) {
    return path.to_string_path();
}

// ============================================================
// Cache Management
// ============================================================

/// @brief Clear the lens cache
inline void clear_cache() {
    clear_lens_cache();
}

/// @brief Get lens cache statistics
[[nodiscard]] inline LensCacheStats cache_stats() {
    return get_lens_cache_stats();
}

} // namespace path

} // namespace lager_ext
