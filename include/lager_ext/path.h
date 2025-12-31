// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path.h
/// @brief Unified Path API - Single entry point for all path-based operations.
///
/// This file provides a unified namespace for path-based lens access:
///
/// 1. Compile-time paths (highest performance):
///    @code
///    using namespace lager_ext::path;
///    auto lens = compile_time<"/users/0/name">();
///    @endcode
///
/// 2. Runtime paths (flexible):
///    @code
///    auto lens = lager_ext::path::runtime("/users/0/name");
///    @endcode
///
/// 3. Builder-style paths (chainable):
///    @code
///    auto lens = lager_ext::path::builder() / "users" / 0 / "name";
///    @endcode
///
/// 4. Variadic template paths (type-safe, compile-time checked):
///    @code
///    auto lens = lager_ext::path::make("users", 0, "name");
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

/// Path lens type (type-erased)
using Lens = LagerValueLens;

/// Path builder type
using Builder = PathLens;

/// Path element (string key or size_t index)
using Element = PathElement;

/// Path (vector of elements)
using Elements = Path;

// ============================================================
// Compile-Time Path API (Highest Performance)
// ============================================================

/// @brief Create a compile-time lens from JSON Pointer syntax
/// @tparam Path JSON Pointer style path (e.g., "/users/0/name")
/// @return A lens object that can be used for get/set operations
///
/// @example
/// @code
/// auto lens = path::compile_time<"/users/0/name">();
/// Value name = lager::view(lens, data);
/// @endcode
template<static_path::FixedString Path>
constexpr auto compile_time() {
    return static_path::JsonPointerPath<Path>::to_lens();
}

/// @brief Alias for compile_time - shorter name
template<static_path::FixedString Path>
constexpr auto ct() {
    return compile_time<Path>();
}

// ============================================================
// Runtime Path API (Flexible)
// ============================================================

/// @brief Create a lens from a runtime string path
/// @param path_str JSON Pointer style path string (e.g., "/users/0/name")
/// @return Type-erased lens
///
/// @example
/// @code
/// std::string user_path = "/users/" + std::to_string(user_id) + "/name";
/// auto lens = path::runtime(user_path);
/// Value name = lager::view(lens, data);
/// @endcode
[[nodiscard]] inline Lens runtime(std::string_view path_str) {
    return string_path_lens(path_str);
}

/// @brief Get value at runtime path
/// @param data The root value
/// @param path_str JSON Pointer style path
/// @return Value at path, or null Value if not found
[[nodiscard]] inline Value get(const Value& data, std::string_view path_str) {
    return get_by_path(data, path_str);
}

/// @brief Set value at runtime path
/// @param data The root value
/// @param path_str JSON Pointer style path
/// @param new_value The new value to set
/// @return New root value with updated path
[[nodiscard]] inline Value set(const Value& data, std::string_view path_str, Value new_value) {
    return set_by_path(data, path_str, std::move(new_value));
}

/// @brief Update value at runtime path using a function
/// @param data The root value
/// @param path_str JSON Pointer style path
/// @param fn Update function (Value -> Value)
/// @return New root value with updated path
template<typename Fn>
[[nodiscard]] Value over(const Value& data, std::string_view path_str, Fn&& fn) {
    return over_by_path(data, path_str, std::forward<Fn>(fn));
}

// ============================================================
// Builder-Style Path API (Chainable)
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

/// @brief Global root path - can be used directly with / operator
/// @example
/// @code
/// auto path = path::root / "users" / 0;
/// @endcode
inline const Builder root{};

// ============================================================
// Variadic Template Path API (Type-Safe)
// ============================================================

/// @brief Create a lens from variadic path elements
/// @tparam Elements Path element types (string-like or integral)
/// @param elements Path elements
/// @return Composed lens
///
/// @example
/// @code
/// auto lens = path::make("users", 0, "name");
/// // Equivalent to: (zug::identity | key_lens("users") | index_lens(0) | key_lens("name"))
/// @endcode
template<PathElementType... Elements>
[[nodiscard]] auto make(Elements&&... elements) {
    return static_path_lens(std::forward<Elements>(elements)...);
}

/// @brief Create a PathLens from variadic elements
/// @tparam Elements Path element types
/// @param elements Path elements
/// @return PathLens object
template<PathElementType... Elements>
[[nodiscard]] Builder make_builder(Elements&&... elements) {
    return make_path(std::forward<Elements>(elements)...);
}

// ============================================================
// Convenience Functions
// ============================================================

/// @brief Get value at variadic path
template<PathElementType... Elements>
[[nodiscard]] Value get(const Value& data, Elements&&... path_elements) {
    return get_at(data, std::forward<Elements>(path_elements)...);
}

/// @brief Set value at variadic path
template<PathElementType... Elements>
[[nodiscard]] Value set(const Value& data, Value new_value, Elements&&... path_elements) {
    return set_at(data, std::move(new_value), std::forward<Elements>(path_elements)...);
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
/// @param data The root value
/// @param path Path elements
/// @return PathAccessResult with success/error info
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
/// @param path_str JSON Pointer style path
/// @return Vector of path elements
[[nodiscard]] inline Elements parse(std::string_view path_str) {
    return parse_string_path(path_str);
}

/// @brief Convert path elements to string
/// @param path Path elements
/// @return String representation (e.g., ".users[0].name")
[[nodiscard]] inline std::string to_string(const Elements& path) {
    return path_to_string(path);
}

/// @brief Convert path elements to JSON Pointer string
/// @param path Path elements
/// @return JSON Pointer string (e.g., "/users/0/name")
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
