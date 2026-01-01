// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path.h
/// @brief Unified Path API - Single entry point for all path-based operations.
///
/// This file provides a unified namespace for path-based lens access:
///
/// 1. Compile-time paths (C++20 string literal):
///    @code
///    using UserNamePath = lager_ext::LiteralPath<"/users/0/name">;
///    Value name = UserNamePath::get(data);
///    @endcode
///
/// 2. Runtime paths (flexible navigation):
///    @code
///    PathLens path = root / "users" / 0 / "name";
///    Value name = path.get(data);
///    @endcode
///
/// 3. String path parsing:
///    @code
///    Value name = path::get(data, "/users/0/name");
///    @endcode
///
/// 4. Variadic path elements:
///    @code
///    Value name = path::get(data, "users", 0, "name");
///    @endcode

#pragma once

#include <lager_ext/lager_lens.h>
#include <lager_ext/static_path.h>
#include <lager_ext/string_path.h>

namespace lager_ext {

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
template<FixedString PathStr>
[[nodiscard]] Lens lens() {
    return static_path_lens<PathStr>();
}

/// @brief Create a lens from runtime string path
/// @param path_str JSON Pointer style path string
/// @return Type-erased LagerValueLens
[[nodiscard]] inline Lens lens(std::string_view path_str) {
    return lager_path_lens(parse_string_path(path_str));
}

/// @brief Create a lens from variadic path elements
template<PathElementType... Elems>
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
template<PathElementType... Elems>
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
    return get_at_path(data, parse_string_path(path_str));
}

/// @brief Get value at variadic path
template<PathElementType... Elems>
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
    return set_at_path(data, parse_string_path(path_str), std::move(new_value));
}

/// @brief Set value at variadic path
/// @note Parameters: (data, path_elem1, path_elem2, ..., new_value)
/// @example set(data, "users", 0, "name", Value{"Alice"})
template<typename... Args>
    requires (sizeof...(Args) >= 2)  // At least one path element + value
[[nodiscard]] Value set(const Value& data, Args&&... args) {
    // Extract the last argument as value, rest as path elements
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto args_tuple = std::forward_as_tuple(std::forward<Args>(args)...);
        constexpr std::size_t N = sizeof...(Args);
        Path p = make_path(std::get<Is>(std::move(args_tuple))...).path();
        return set_at_path(data, p, Value{std::get<N-1>(std::move(args_tuple))});
    }(std::make_index_sequence<sizeof...(Args) - 1>{});
}

// ============================================================
// Over - Update value at path using a function
// ============================================================

/// @brief Update value at Path container using a function
template<typename Fn>
[[nodiscard]] Value over(const Value& data, const PathVec& path, Fn&& fn) {
    return set_at_path(data, path, std::forward<Fn>(fn)(get_at_path(data, path)));
}

/// @brief Update value at string path using a function
template<typename Fn>
[[nodiscard]] Value over(const Value& data, std::string_view path_str, Fn&& fn) {
    auto path = parse_string_path(path_str);
    return set_at_path(data, path, std::forward<Fn>(fn)(get_at_path(data, path)));
}

/// @brief Update value at variadic path using a function
/// @note Parameters: (data, path_elem1, ..., path_elemN, fn)
template<typename Fn, PathElementType... Elems>
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
    return set_at_path_vivify(data, parse_string_path(path_str), std::move(new_value));
}

/// @brief Set value at variadic path with auto-vivification
/// @note Parameters: (data, path_elem1, ..., path_elemN, new_value)
template<typename... Args>
    requires (sizeof...(Args) >= 2)
[[nodiscard]] Value set_vivify(const Value& data, Args&&... args) {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto args_tuple = std::forward_as_tuple(std::forward<Args>(args)...);
        constexpr std::size_t N = sizeof...(Args);
        Path p = make_path(std::get<Is>(std::move(args_tuple))...).path();
        return set_at_path_vivify(data, p, Value{std::get<N-1>(std::move(args_tuple))});
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
    return erase_at_path(data, parse_string_path(path_str));
}

/// @brief Erase value at variadic path
template<PathElementType... Elems>
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
    return is_valid_path(data, parse_string_path(path_str));
}

/// @brief Check if a variadic path exists in the data
template<PathElementType... Elems>
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
    return valid_path_depth(data, parse_string_path(path_str));
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
    return get_at_path_safe(data, parse_string_path(path_str));
}

/// @brief Safe set with detailed error information
[[nodiscard]] inline PathAccessResult safe_set(const Value& data, const PathVec& path, Value new_value) {
    return set_at_path_safe(data, path, std::move(new_value));
}

/// @brief Safe set at string path with detailed error information
[[nodiscard]] inline PathAccessResult safe_set(const Value& data, std::string_view path_str, Value new_value) {
    return set_at_path_safe(data, parse_string_path(path_str), std::move(new_value));
}

// ============================================================
// Path Utilities
// ============================================================

/// @brief Parse a string path into path elements
[[nodiscard]] inline PathVec parse(std::string_view path_str) {
    return parse_string_path(path_str);
}

/// @brief Convert path elements to string (e.g., ".users[0].name")
[[nodiscard]] inline std::string to_string(const PathVec& path) {
    return path_to_string(path);
}

/// @brief Convert path elements to JSON Pointer string (e.g., "/users/0/name")
[[nodiscard]] inline std::string to_json_pointer(const PathVec& path) {
    return path_to_string_path(path);
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