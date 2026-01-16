// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file value.h
/// @brief Common Value type definition and utilities for JSON-like dynamic data.
///
/// This file defines the core Value type that can represent:
/// - Primitive types: int, float, double, bool, string
/// - Math types: Vec2, Vec3, Vec4, Mat3, Mat4x3 (fixed-size float arrays)
/// - Container types: map, vector, array, table (using immer's immutable containers)
/// - Null (std::monostate)
///
/// ## C++20 Features Used
/// - Concepts for type constraints
/// - std::source_location for error diagnostics
/// - [[nodiscard]] attributes
/// - Defaulted comparison operators (operator<=>)

#pragma once

// IMPORTANT: Include lager_ext configuration BEFORE any library headers
// This ensures consistent macro settings across all compilation units
#include <lager_ext/lager_ext_config.h>

#include "api.h"
#include "value_fwd.h" // Include forward declarations to avoid duplication


#include <lager_ext/path.h> // PathElement, PathView, Path

#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/map_transient.hpp>
#include <immer/memory_policy.hpp>
#include <immer/table.hpp>
#include <immer/table_transient.hpp>
#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>

#include <array>           // for Vec2, Vec3, Vec4, Mat3, Mat4x3
#include <compare>         // for std::strong_ordering (C++20)
#include <concepts>        // for C++20 Concepts (C++20)
#include <cstdint>
#include <cstring>         // for std::memcpy
#include <functional>      // for std::hash
#include <source_location> // for std::source_location (C++20) - used in function signatures
#include <string>
#include <variant>
#include <vector>

// Conditional include for verbose logging output (debug builds only)
#if !defined(NDEBUG) || (defined(lager_ext_VERBOSE_LOG) && lager_ext_VERBOSE_LOG)
#include <iostream>  // for std::cerr (debug logging)
#endif

// ============================================================
// Verbose Logging Configuration
//
// When lager_ext_VERBOSE_LOG is defined:
//   - at() and set() operations log errors to stderr
//   - Useful for debugging path access issues
//
// By default, verbose logging is DISABLED in release builds
// and ENABLED in debug builds.
//
// To explicitly enable: #define lager_ext_VERBOSE_LOG 1
// To explicitly disable: #define lager_ext_VERBOSE_LOG 0
// ============================================================

#ifndef lager_ext_VERBOSE_LOG
#if defined(NDEBUG)
#define lager_ext_VERBOSE_LOG 0
#else
#define lager_ext_VERBOSE_LOG 1
#endif
#endif

namespace lager_ext {

namespace detail {

inline void log_access_error(std::string_view func, std::string_view message,
                             std::source_location loc = std::source_location::current()) noexcept {
#if lager_ext_VERBOSE_LOG
    std::cerr << "[" << func << "] " << message << " (called from " << loc.file_name() << ":" << loc.line() << ")\n";
#else
    (void)func;
    (void)message;
    (void)loc;
#endif
}

inline void log_key_error(std::string_view func, std::string_view key, std::string_view reason,
                          std::source_location loc = std::source_location::current()) noexcept {
#if lager_ext_VERBOSE_LOG
    std::cerr << "[" << func << "] key '" << key << "' " << reason << " (called from " << loc.file_name() << ":"
              << loc.line() << ")\n";
#else
    (void)func;
    (void)key;
    (void)reason;
    (void)loc;
#endif
}

inline void log_index_error(std::string_view func, std::size_t index, std::string_view reason,
                            std::source_location loc = std::source_location::current()) noexcept {
#if lager_ext_VERBOSE_LOG
    std::cerr << "[" << func << "] index " << index << " " << reason << " (called from " << loc.file_name() << ":"
              << loc.line() << ")\n";
#else
    (void)func;
    (void)index;
    (void)reason;
    (void)loc;
#endif
}

} // namespace detail

} // namespace lager_ext

#include <lager_ext/concepts.h>

namespace lager_ext {

// Note: Vec2, Vec3, Vec4, Mat3, Mat4x3, Mat4 are defined in concepts.h
// They are already available here via the #include above.

// ============================================================
// Transparent Hash and Comparator
//
// These enable heterogeneous lookup in immer::map and immer::table,
// allowing queries with std::string_view without constructing
// a temporary std::string. This eliminates heap allocations
// during path traversal operations.
//
// The `is_transparent` type alias signals to the container that
// it supports heterogeneous lookup (C++14/C++20 feature).
// ============================================================

/// Transparent hash functor for string types
/// Supports: std::string, std::string_view, const char*
struct TransparentStringHash {
    using is_transparent = void; // Enable heterogeneous lookup

    [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }

    [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }

    [[nodiscard]] std::size_t operator()(const char* s) const noexcept { return std::hash<std::string_view>{}(s); }
};

/// Transparent equality comparator for string types
/// Supports: std::string, std::string_view, const char*
struct TransparentStringEqual {
    using is_transparent = void; // Enable heterogeneous lookup

    [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

// ============================================================
// Container Type Forward Declarations
// ============================================================

// Forward declare Value for container definitions
struct Value;

/// ValueBox wraps Value in an immer::box for efficient sharing
using ValueBox = immer::box<Value>;

/// Map container with transparent lookup support
/// Allows find/count/at operations with string_view without allocation
using ValueMap = immer::map<std::string, ValueBox, TransparentStringHash, TransparentStringEqual>;

/// Vector container for ordered sequences
using ValueVector = immer::vector<ValueBox>;

/// Array container for small fixed-size sequences
using ValueArray = immer::array<ValueBox>;

/// Table entry with string id
struct TableEntry {
    std::string id;
    ValueBox value;

    /// C++20 defaulted equality comparison
    /// Compiler generates operator== and operator!= automatically
    bool operator==(const TableEntry&) const = default;
};

/// Table container with transparent lookup support
/// Allows find/count operations with string_view without allocation
using ValueTable = immer::table<TableEntry, immer::table_key_fn, TransparentStringHash, TransparentStringEqual>;

// Boxed matrix types (reduces variant size from ~72 to ~40 bytes)
using BoxedMat3 = immer::box<Mat3>;
using BoxedMat4x3 = immer::box<Mat4x3>;
using BoxedMat4 = immer::box<Mat4>;

/// @brief Byte buffer type for binary serialization
using ByteBuffer = std::vector<uint8_t>;

struct Value {
    // Type aliases for container types
    using value_box = ValueBox;
    using value_map = ValueMap;
    using value_vector = ValueVector;
    using value_array = ValueArray;
    using value_table = ValueTable;
    using table_entry = TableEntry;
    using boxed_mat3 = BoxedMat3;
    using boxed_mat4x3 = BoxedMat4x3;
    using boxed_mat4 = BoxedMat4;

    std::variant<int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double, bool,
                 std::string, Vec2, Vec3, Vec4, boxed_mat3, boxed_mat4x3, boxed_mat4, value_map, value_vector,
                 value_array, value_table, std::monostate>
        data;

    // Constructors for primitive and math types
    // Note: constexpr is intentionally omitted because:
    //   1. Container types (map, vector, etc.) are not constexpr-constructible
    //   2. Value is designed for runtime dynamic data (like JSON), not compile-time constants
    //   3. Consistency: most constructors can't be constexpr anyway
    Value() noexcept : data(std::monostate{}) {}
    Value(int8_t v) noexcept : data(v) {}
    Value(int16_t v) noexcept : data(v) {}
    Value(int32_t v) noexcept : data(v) {}
    Value(int64_t v) noexcept : data(v) {}
    Value(uint8_t v) noexcept : data(v) {}
    Value(uint16_t v) noexcept : data(v) {}
    Value(uint32_t v) noexcept : data(v) {}
    Value(uint64_t v) noexcept : data(v) {}
    Value(float v) noexcept : data(v) {}
    Value(double v) noexcept : data(v) {}
    Value(bool v) noexcept : data(v) {}
    Value(const std::string& v) : data(v) {}
    Value(std::string&& v) noexcept : data(std::move(v)) {}
    Value(const char* v) : data(std::in_place_type<std::string>, v) {}
    Value(Vec2 v) noexcept : data(v) {}
    Value(Vec3 v) noexcept : data(v) {}
    Value(Vec4 v) noexcept : data(v) {}
    Value(const Mat3& v) : data(boxed_mat3{v}) {}
    Value(const Mat4x3& v) : data(boxed_mat4x3{v}) {}
    Value(const Mat4& v) : data(boxed_mat4{v}) {}
    Value(boxed_mat3 v) : data(std::move(v)) {}
    Value(boxed_mat4x3 v) : data(std::move(v)) {}
    Value(boxed_mat4 v) : data(std::move(v)) {}
    Value(value_map v) : data(std::move(v)) {}
    Value(value_vector v) : data(std::move(v)) {}
    Value(value_array v) : data(std::move(v)) {}
    Value(value_table v) : data(std::move(v)) {}

    // Factory functions for container types
    static Value map(std::initializer_list<std::pair<std::string, Value>> init) {
        auto t = value_map{}.transient();
        for (const auto& [key, val] : init) {
            t.set(key, value_box{val});
        }
        return Value{t.persistent()};
    }

    static Value vector(std::initializer_list<Value> init) {
        auto t = value_vector{}.transient();
        for (const auto& val : init) {
            t.push_back(value_box{val});
        }
        return Value{t.persistent()};
    }

    static Value array(std::initializer_list<Value> init) {
        value_array result;
        for (const auto& val : init) {
            result = std::move(result).push_back(value_box{val});
        }
        return Value{std::move(result)};
    }

    static Value table(std::initializer_list<std::pair<std::string, Value>> init) {
        auto t = value_table{}.transient();
        for (const auto& [id, val] : init) {
            t.insert(table_entry{id, value_box{val}});
        }
        return Value{t.persistent()};
    }

    /// Factory functions for math types with explicit parameters
    static Value vec2(float x, float y) { return Value{Vec2{x, y}}; }

    static Value vec3(float x, float y, float z) { return Value{Vec3{x, y, z}}; }

    static Value vec4(float x, float y, float z, float w) { return Value{Vec4{x, y, z, w}}; }

    /// Factory functions for math types from raw float pointers
    /// @note Useful for interop with other math libraries (GLM, Eigen, etc.)
    static Value vec2(const float* ptr) { return Value{Vec2{ptr[0], ptr[1]}}; }

    static Value vec3(const float* ptr) { return Value{Vec3{ptr[0], ptr[1], ptr[2]}}; }

    static Value vec4(const float* ptr) { return Value{Vec4{ptr[0], ptr[1], ptr[2], ptr[3]}}; }

    static Value mat3(const float* ptr) {
        Mat3 m;
        std::memcpy(m.data(), ptr, sizeof(Mat3));
        return Value{m};
    }

    static Value mat4x3(const float* ptr) {
        Mat4x3 m;
        std::memcpy(m.data(), ptr, sizeof(Mat4x3));
        return Value{m};
    }

    static Value mat4(const float* ptr) {
        Mat4 m;
        std::memcpy(m.data(), ptr, sizeof(Mat4));
        return Value{m};
    }

    /// Get pointer to contained value of type T, or nullptr if type mismatch
    template <typename T>
    [[nodiscard]] constexpr const T* get_if() const noexcept {
        return std::get_if<T>(&data);
    }

    /// Check if contained value is of type T
    template <typename T>
    [[nodiscard]] constexpr bool is() const noexcept {
        return std::holds_alternative<T>(data);
    }

    /// Get the type index in the variant
    [[nodiscard]] constexpr std::size_t type_index() const noexcept { return data.index(); }

    /// Type predicates (constexpr for compile-time usage)
    [[nodiscard]] constexpr bool is_null() const noexcept { return std::holds_alternative<std::monostate>(data); }
    [[nodiscard]] constexpr bool is_vec2() const noexcept { return is<Vec2>(); }
    [[nodiscard]] constexpr bool is_vec3() const noexcept { return is<Vec3>(); }
    [[nodiscard]] constexpr bool is_vec4() const noexcept { return is<Vec4>(); }
    [[nodiscard]] constexpr bool is_mat3() const noexcept { return is<boxed_mat3>(); }
    [[nodiscard]] constexpr bool is_mat4x3() const noexcept { return is<boxed_mat4x3>(); }
    [[nodiscard]] constexpr bool is_math_type() const noexcept {
        return is_vec2() || is_vec3() || is_vec4() || is_mat3() || is_mat4x3();
    }

    /// Access element by key (zero-allocation with transparent lookup)
    /// @note std::string and const char* implicitly convert to string_view (C++17+)
    [[nodiscard]] Value at(std::string_view key) const {
        if (auto* m = get_if<value_map>()) {
            if (auto* found = m->find(key))
                return found->get();
        }
        if (auto* t = get_if<value_table>()) {
            if (auto* found = t->find(key))
                return found->value.get();
        }
        detail::log_key_error("Value::at", key, "not found or type mismatch");
        return Value{};
    }

    [[nodiscard]] Value at(std::size_t index) const {
        if (auto* v = get_if<value_vector>()) {
            if (index < v->size())
                return (*v)[index].get();
        }
        if (auto* a = get_if<value_array>()) {
            if (index < a->size())
                return (*a)[index].get();
        }
        detail::log_index_error("Value::at", index, "out of range or type mismatch");
        return Value{};
    }

    /// Get value as type T, or return default if type mismatch
    /// @note Use this for all primitive types: as<int>(), as<double>(), as<Vec2>(), etc.
    /// @example val.as<int>(42), val.as<float>(), val.as<bool>(false)
    template <typename T>
    [[nodiscard]] T as(T default_val = T{}) const {
        if (auto* ptr = get_if<T>())
            return *ptr;
        return default_val;
    }

    // ============================================================
    // Special accessor functions (cannot be replaced by as<T>)
    // ============================================================

    /// Get string with lvalue/rvalue optimization
    // Lvalue version - copy the string
    [[nodiscard]] std::string as_string(std::string default_val = "") const& {
        if (auto* p = get_if<std::string>())
            return *p;
        return default_val;
    }

    // Rvalue version - move the string for zero-copy when Value is about to be destroyed
    [[nodiscard]] std::string as_string(std::string default_val = "") && {
        if (auto* p = get_if<std::string>())
            return std::move(*p);
        return default_val;
    }

    [[nodiscard]] std::string_view as_string_view() const noexcept {
        if (auto* p = get_if<std::string>())
            return *p;
        return {};
    }

    /// Get any numeric type as double (with automatic type conversion)
    /// @note Supports: double, float, int64_t, int32_t
    [[nodiscard]] double as_number(double default_val = 0.0) const {
        if (auto* p = get_if<double>())
            return *p;
        if (auto* p = get_if<float>())
            return static_cast<double>(*p);
        if (auto* p = get_if<int64_t>())
            return static_cast<double>(*p);
        if (auto* p = get_if<int>())
            return static_cast<double>(*p);
        return default_val;
    }

    /// Get Mat3 (requires unboxing from immer::box)
    [[nodiscard]] Mat3 as_mat3(Mat3 default_val = {}) const {
        if (auto* p = get_if<boxed_mat3>())
            return p->get();
        return default_val;
    }

    [[nodiscard]] Mat4x3 as_mat4x3(Mat4x3 default_val = {}) const {
        if (auto* p = get_if<boxed_mat4x3>())
            return p->get();
        return default_val;
    }

    /// Check if key exists (zero-allocation with transparent lookup)
    [[nodiscard]] bool contains(std::string_view key) const { return count(key) > 0; }

    [[nodiscard]] bool contains(std::size_t index) const {
        if (auto* v = get_if<value_vector>())
            return index < v->size();
        if (auto* a = get_if<value_array>())
            return index < a->size();
        return false;
    }

    /// Set value by string_view key
    /// @note Allocation is unavoidable since immer::map needs owned keys for persistence.
    [[nodiscard]] Value set(std::string_view key, Value val) const {
        if (auto* m = get_if<value_map>())
            return m->set(std::string{key}, value_box{std::move(val)});
        if (auto* t = get_if<value_table>())
            return t->insert(table_entry{std::string{key}, value_box{std::move(val)}});
        detail::log_key_error("Value::set", key, "cannot set on non-map type");
        return *this;
    }

    /// Set value by const char* key (disambiguation for string literals)
    [[nodiscard]] Value set(const char* key, Value val) const {
        return set(std::string_view{key}, std::move(val));
    }

    /// Set value by const string& key (disambiguation for string lvalue)
    [[nodiscard]] Value set(const std::string& key, Value val) const {
        return set(std::string_view{key}, std::move(val));
    }

    /// Set value by rvalue string key (zero-copy key transfer)
    /// @note Use this overload when you have a std::string that can be moved.
    [[nodiscard]] Value set(std::string&& key, Value val) const {
        if (auto* m = get_if<value_map>())
            return m->set(std::move(key), value_box{std::move(val)});
        if (auto* t = get_if<value_table>())
            return t->insert(table_entry{std::move(key), value_box{std::move(val)}});
        detail::log_key_error("Value::set", key, "cannot set on non-map type");
        return *this;
    }

    [[nodiscard]] Value set(std::size_t index, Value val) const {
        if (auto* v = get_if<value_vector>()) {
            if (index < v->size())
                return v->set(index, value_box{std::move(val)});
        }
        if (auto* a = get_if<value_array>()) {
            if (index < a->size()) {
                return a->update(index, [&val](const value_box&) { return value_box{std::move(val)}; });
            }
        }
        detail::log_index_error("Value::set", index, "cannot set on non-vector type");
        return *this;
    }

    [[nodiscard]] Value set_vivify(std::string_view key, Value val) const {
        if (auto* m = get_if<value_map>())
            return m->set(std::string{key}, value_box{std::move(val)});
        if (auto* t = get_if<value_table>())
            return t->insert(table_entry{std::string{key}, value_box{std::move(val)}});
        if (is_null())
            return value_map{}.set(std::string{key}, value_box{std::move(val)});
        detail::log_key_error("Value::set_vivify", key, "cannot set on non-map/non-null type");
        return *this;
    }

    /// Set value by const char* key with auto-vivification (disambiguation for string literals)
    [[nodiscard]] Value set_vivify(const char* key, Value val) const {
        return set_vivify(std::string_view{key}, std::move(val));
    }

    /// Set value by const string& key with auto-vivification (disambiguation for string lvalue)
    [[nodiscard]] Value set_vivify(const std::string& key, Value val) const {
        return set_vivify(std::string_view{key}, std::move(val));
    }

    /// Set value by rvalue string key with auto-vivification (zero-copy key transfer)
    [[nodiscard]] Value set_vivify(std::string&& key, Value val) const {
        if (auto* m = get_if<value_map>())
            return m->set(std::move(key), value_box{std::move(val)});
        if (auto* t = get_if<value_table>())
            return t->insert(table_entry{std::move(key), value_box{std::move(val)}});
        if (is_null())
            return value_map{}.set(std::move(key), value_box{std::move(val)});
        detail::log_key_error("Value::set_vivify", key, "cannot set on non-map/non-null type");
        return *this;
    }

    [[nodiscard]] Value set_vivify(std::size_t index, Value val) const {
        if (auto* v = get_if<value_vector>()) {
            if (index < v->size())
                return v->set(index, value_box{std::move(val)});
            auto trans = v->transient();
            while (trans.size() <= index)
                trans.push_back(value_box{});
            trans.set(index, value_box{std::move(val)});
            return trans.persistent();
        }
        if (auto* a = get_if<value_array>()) {
            if (index < a->size()) {
                return a->update(index, [&val](const value_box&) { return value_box{std::move(val)}; });
            }
            detail::log_index_error("Value::set_vivify", index, "array index out of range");
            return *this;
        }
        if (is_null()) {
            auto trans = value_vector{}.transient();
            for (std::size_t i = 0; i < index; ++i)
                trans.push_back(value_box{});
            trans.push_back(value_box{std::move(val)});
            return trans.persistent();
        }
        detail::log_index_error("Value::set_vivify", index, "cannot set on non-vector/non-null type");
        return *this;
    }

    /// Count occurrences of key (zero-allocation with transparent lookup)
    [[nodiscard]] std::size_t count(std::string_view key) const {
        if (auto* m = get_if<value_map>())
            return m->count(key);
        if (auto* t = get_if<value_table>())
            return t->count(key) ? 1 : 0;
        return 0;
    }

    [[nodiscard]] std::size_t size() const {
        if (auto* m = get_if<value_map>())
            return m->size();
        if (auto* v = get_if<value_vector>())
            return v->size();
        if (auto* a = get_if<value_array>())
            return a->size();
        if (auto* t = get_if<value_table>())
            return t->size();
        return 0;
    }

    using size_type = std::size_t;
};

// ============================================================
// Value comparison operators (C++20)
//
// Uses spaceship operator (<=>): compiler auto-generates ==, !=, <, >, <=, >=
// Note: std::variant supports <=> in C++20, enabling lexicographic comparison
// ============================================================

/// Equality comparison for Value
inline bool operator==(const Value& a, const Value& b) {
    return a.data == b.data;
}

/// Three-way comparison (spaceship operator) for Value
/// Enables all comparison operators: ==, !=, <, >, <=, >=
/// Returns std::partial_ordering because floating-point types may be NaN
inline std::partial_ordering operator<=>(const Value& a, const Value& b) {
    // Compare type indices first
    if (a.data.index() != b.data.index()) {
        return a.data.index() <=> b.data.index();
    }

    // Same type, compare values using std::visit
    return std::visit(
        [](const auto& lhs, const auto& rhs) -> std::partial_ordering {
            using T = std::decay_t<decltype(lhs)>;
            using U = std::decay_t<decltype(rhs)>;

            if constexpr (std::is_same_v<T, U>) {
                // Same type, compare directly
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return std::partial_ordering::equivalent;
                } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                    // Floating-point: use partial_ordering
                    return lhs <=> rhs;
                } else if constexpr (requires { lhs <=> rhs; }) {
                    // Type supports <=>
                    auto result = lhs <=> rhs;
                    if constexpr (std::is_same_v<decltype(result), std::strong_ordering>) {
                        return static_cast<std::partial_ordering>(result);
                    } else {
                        return result;
                    }
                } else if constexpr (requires {
                                         lhs < rhs;
                                         lhs == rhs;
                                     }) {
                    // Fall back to < and == operators
                    if (lhs == rhs)
                        return std::partial_ordering::equivalent;
                    if (lhs < rhs)
                        return std::partial_ordering::less;
                    return std::partial_ordering::greater;
                } else {
                    // Types without comparison (e.g., immer containers)
                    // Fall back to address comparison (arbitrary but consistent)
                    return std::partial_ordering::equivalent;
                }
            } else {
                // Different types shouldn't happen (same index), but handle anyway
                return std::partial_ordering::unordered;
            }
        },
        a.data, b.data);
}

// ============================================================
// Utility functions
// ============================================================

// Convert Value to human-readable string
[[nodiscard]] LAGER_EXT_API std::string value_to_string(const Value& val);

// Print Value with indentation
LAGER_EXT_API void print_value(const Value& val, const std::string& prefix = "", std::size_t depth = 0);

// Note: PathView::to_dot_notation() and PathView::to_string_path() are declared in path_types.h

// ============================================================
// Common test data factory
//
// Creates a sample data structure for demos:
// {
//   "users": [
//     { "name": "Alice", "age": 25 },
//     { "name": "Bob", "age": 30 }
//   ],
//   "config": { "version": 1, "theme": "dark" }
// }
// ============================================================
LAGER_EXT_API Value create_sample_data();

} // namespace lager_ext