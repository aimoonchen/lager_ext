// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path.h
/// @brief Unified Path API - Single entry point for all path-based operations.
///
/// This file provides a unified namespace for path-based lens access:
///
/// 1. Compile-time paths (C++20 string literal):
///    @code
///    auto lens = lager_ext::path::lens<"/users/0/name">();
///    @endcode
///
/// 2. Runtime paths (flexible string parsing):
///    @code
///    auto lens = lager_ext::path::lens("/users/0/name");
///    @endcode
///
/// 3. Variadic template paths (type-safe):
///    @code
///    auto lens = lager_ext::path::lens("users", 0, "name");
///    @endcode
///
/// 4. Builder-style paths (chainable):
///    @code
///    auto path = lager_ext::path::builder() / "users" / 0 / "name";
///    @endcode
///
/// For direct access without lens, use get/set/over functions.

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

/// Path lens type (type-erased)
using Lens = LagerValueLens;

/// Path builder type
using Builder = PathLens;

/// Path element (string key or size_t index)
using Element = PathElement;

/// Path (vector of elements)
using Elements = Path;

// ============================================================
// Unified Lens API
// ============================================================

/// @brief Create a lens from compile-time string literal (C++20 NTTP)
/// @tparam Path JSON Pointer style path (e.g., "/users/0/name")
/// @return Type-erased LagerValueLens
///
/// @example
/// @code
/// auto lens = path::lens<"/users/0/name">();
/// Value name = lager::view(lens, data);
/// @endcode
template<FixedString Path>
[[nodiscard]] Lens lens() {
    return static_path_lens<Path>();
}

/// @brief Create a lens from runtime string path
/// @param path_str JSON Pointer style path string
/// @return Type-erased LagerValueLens
///
/// @example
/// @code
/// std::string user_path = "/users/" + std::to_string(user_id) + "/name";
/// auto lens = path::lens(user_path);
/// @endcode
[[nodiscard]] inline Lens lens(std::string_view path_str) {
    return string_path_lens(path_str);
}

/// @brief Create a lens from variadic path elements
/// @tparam Elements Path element types (string-like or integral)
/// @param elements Path elements
/// @return Composed lens (auto type, not type-erased)
///
/// @example
/// @code
/// auto lens = path::lens("users", 0, "name");
/// @endcode
template<PathElementType... Elements>
[[nodiscard]] auto lens(Elements&&... elements) {
    return static_path_lens(std::forward<Elements>(elements)...);
}

// ============================================================
// Builder-Style Path API
// ============================================================

/// @brief Create a path builder starting from root
/// @return Empty PathLens that can be extended with / operator
///
/// @example
/// @code
/// auto path = path::builder() / "users" / 0 / "name";
/// Value name = path.get(data);
/// @endcode
[[nodiscard]] inline Builder builder() {
    return Builder{};
}

/// @brief Create a PathLens from variadic elements
template<PathElementType... Elements>
[[nodiscard]] Builder make_builder(Elements&&... elements) {
    return make_path(std::forward<Elements>(elements)...);
}

// ============================================================
// Direct Access Functions (without lens)
// ============================================================

/// @brief Get value at string path
/// @param data The root value
/// @param path_str JSON Pointer style path
/// @return Value at path, or null Value if not found
[[nodiscard]] inline Value get(const Value& data, std::string_view path_str) {
    return get_by_path(data, path_str);
}

/// @brief Get value at variadic path
template<PathElementType... Elements>
[[nodiscard]] Value get(const Value& data, Elements&&... path_elements) {
    return get_at(data, std::forward<Elements>(path_elements)...);
}

/// @brief Set value at string path
/// @param data The root value
/// @param path_str JSON Pointer style path
/// @param new_value The new value to set
/// @return New root value with updated path
[[nodiscard]] inline Value set(const Value& data, std::string_view path_str, Value new_value) {
    return set_by_path(data, path_str, std::move(new_value));
}

/// @brief Set value at variadic path
template<PathElementType... Elements>
[[nodiscard]] Value set(const Value& data, Value new_value, Elements&&... path_elements) {
    return set_at(data, std::move(new_value), std::forward<Elements>(path_elements)...);
}

/// @brief Update value at string path using a function
template<typename Fn>
[[nodiscard]] Value over(const Value& data, std::string_view path_str, Fn&& fn) {
    return over_by_path(data, path_str, std::forward<Fn>(fn));
}

/// @brief Update value at variadic path
template<typename Fn, PathElementType... Elements>
[[nodiscard]] Value over(const Value& data, Fn&& fn, Elements&&... path_elements) {
    return over_at(data, std::forward<Fn>(fn), std::forward<Elements>(path_elements)...);
}

// ============================================================
// Safe Access (with error handling)
// ============================================================

/// @brief Safe get with detailed error information
[[nodiscard]] inline PathAccessResult safe_get(const Value& data, const Elements& path) {
    return get_at_path_safe(data, path);
}

/// @brief Safe set with detailed error information
[[nodiscard]] inline PathAccessResult safe_set(const Value& data, const Elements& path, Value new_value) {
    return set_at_path_safe(data, path, std::move(new_value));
}

// ============================================================
// Path Utilities
// ============================================================

/// @brief Parse a string path into path elements
[[nodiscard]] inline Elements parse(std::string_view path_str) {
    return parse_string_path(path_str);
}

/// @brief Convert path elements to string (e.g., ".users[0].name")
[[nodiscard]] inline std::string to_string(const Elements& path) {
    return path_to_string(path);
}

/// @brief Convert path elements to JSON Pointer string (e.g., "/users/0/name")
[[nodiscard]] inline std::string to_json_pointer(const Elements& path) {
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