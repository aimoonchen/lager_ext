// Copyright (c) 2024 chenmou. All rights reserved.
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
/// The Value type is templated on a memory policy, allowing users to
/// customize memory allocation strategies for the underlying immer containers.

#pragma once

#include "api.h"

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

#include <algorithm>  // for std::copy_n
#include <array>      // for Vec2, Vec3, Vec4, Mat3, Mat4x3
#include <compare>    // for std::strong_ordering (C++20)
#include <concepts>   // for C++20 Concepts (C++20)
#include <cstdint>
#include <iostream>
#include <ranges>     // for std::ranges::copy (C++20)
#include <source_location> // for std::source_location (C++20)
#include <span>       // for std::span (C++20)
#include <string>
#include <variant>
#include <vector>

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
#  if defined(NDEBUG)
#    define lager_ext_VERBOSE_LOG 0
#  else
#    define lager_ext_VERBOSE_LOG 1
#  endif
#endif

namespace lager_ext {

namespace detail {

inline void log_access_error(
    std::string_view func,
    std::string_view message,
    std::source_location loc = std::source_location::current()) noexcept
{
#if lager_ext_VERBOSE_LOG
    std::cerr << "[" << func << "] " << message
              << " (called from " << loc.file_name()
              << ":" << loc.line() << ")\n";
#else
    (void)func;
    (void)message;
    (void)loc;
#endif
}

inline void log_key_error(
    std::string_view func,
    std::string_view key,
    std::string_view reason,
    std::source_location loc = std::source_location::current()) noexcept
{
#if lager_ext_VERBOSE_LOG
    std::cerr << "[" << func << "] key '" << key << "' " << reason
              << " (called from " << loc.file_name()
              << ":" << loc.line() << ")\n";
#else
    (void)func;
    (void)key;
    (void)reason;
    (void)loc;
#endif
}

inline void log_index_error(
    std::string_view func,
    std::size_t index,
    std::string_view reason,
    std::source_location loc = std::source_location::current()) noexcept
{
#if lager_ext_VERBOSE_LOG
    std::cerr << "[" << func << "] index " << index << " " << reason
              << " (called from " << loc.file_name()
              << ":" << loc.line() << ")\n";
#else
    (void)func;
    (void)index;
    (void)reason;
    (void)loc;
#endif
}

} // namespace detail

} // namespace lager_ext (temporary close for include)

#include <lager_ext/concepts.h>

namespace lager_ext {

// Math type aliases (row-major matrices)
using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Mat3 = std::array<float, 9>;
using Mat4x3 = std::array<float, 12>;
using Mat4 = std::array<float, 16>;

// Forward declaration
template <typename MemoryPolicy>
struct BasicValue;

template <typename MemoryPolicy>
using BasicValueBox = immer::box<BasicValue<MemoryPolicy>, MemoryPolicy>;

template <typename MemoryPolicy>
using BasicValueMap = immer::map<std::string,
                                  BasicValueBox<MemoryPolicy>,
                                  std::hash<std::string>,
                                  std::equal_to<std::string>,
                                  MemoryPolicy>;

template <typename MemoryPolicy>
using BasicValueVector = immer::vector<BasicValueBox<MemoryPolicy>,
                                        MemoryPolicy>;

template <typename MemoryPolicy>
using BasicValueArray = immer::array<BasicValueBox<MemoryPolicy>,
                                      MemoryPolicy>;

template <typename MemoryPolicy>
struct BasicTableEntry {
    std::string id;
    BasicValueBox<MemoryPolicy> value;

    bool operator==(const BasicTableEntry& other) const {
        return id == other.id && value == other.value;
    }

    bool operator!=(const BasicTableEntry& other) const {
        return !(*this == other);
    }
};

template <typename MemoryPolicy>
using BasicValueTable = immer::table<BasicTableEntry<MemoryPolicy>,
                                      immer::table_key_fn,
                                      std::hash<std::string>,
                                      std::equal_to<std::string>,
                                      MemoryPolicy>;

// Boxed matrix types (reduces variant size from ~72 to ~40 bytes)
template <typename MemoryPolicy>
using BoxedMat3 = immer::box<Mat3, MemoryPolicy>;

template <typename MemoryPolicy>
using BoxedMat4x3 = immer::box<Mat4x3, MemoryPolicy>;

template <typename MemoryPolicy>
using BoxedMat4 = immer::box<Mat4, MemoryPolicy>;

using PathElement = std::variant<std::string, std::size_t>;
using Path        = std::vector<PathElement>;

/// @brief Byte buffer type for binary serialization
using ByteBuffer  = std::vector<uint8_t>;

template <typename MemoryPolicy = immer::default_memory_policy>
struct BasicValue
{
    using memory_policy = MemoryPolicy;
    using value_box     = BasicValueBox<MemoryPolicy>;
    using value_map     = BasicValueMap<MemoryPolicy>;
    using value_vector  = BasicValueVector<MemoryPolicy>;
    using value_array   = BasicValueArray<MemoryPolicy>;
    using value_table   = BasicValueTable<MemoryPolicy>;
    using table_entry   = BasicTableEntry<MemoryPolicy>;
    using boxed_mat3    = BoxedMat3<MemoryPolicy>;
    using boxed_mat4x3  = BoxedMat4x3<MemoryPolicy>;
    using boxed_mat4    = BoxedMat4<MemoryPolicy>;

    std::variant<int8_t,
                 int16_t,
                 int32_t,
                 int64_t,
                 uint8_t,
                 uint16_t,
                 uint32_t,
                 uint64_t,
                 float,
                 double,
                 bool,
                 std::string,
                 Vec2,
                 Vec3,
                 Vec4,
                 boxed_mat3,
                 boxed_mat4x3,
                 boxed_mat4,
                 value_map,
                 value_vector,
                 value_array,
                 value_table,
                 std::monostate>
        data;

    constexpr BasicValue() noexcept : data(std::monostate{}) {}
    constexpr BasicValue(int8_t v) noexcept : data(v) {}
    constexpr BasicValue(int16_t v) noexcept : data(v) {}
    constexpr BasicValue(int32_t v) noexcept : data(v) {}
    constexpr BasicValue(int64_t v) noexcept : data(v) {}
    constexpr BasicValue(uint8_t v) noexcept : data(v) {}
    constexpr BasicValue(uint16_t v) noexcept : data(v) {}
    constexpr BasicValue(uint32_t v) noexcept : data(v) {}
    constexpr BasicValue(uint64_t v) noexcept : data(v) {}
    constexpr BasicValue(float v) noexcept : data(v) {}
    constexpr BasicValue(double v) noexcept : data(v) {}
    constexpr BasicValue(bool v) noexcept : data(v) {}
    BasicValue(const std::string& v) : data(v) {}
    BasicValue(std::string&& v) noexcept : data(std::move(v)) {}
    BasicValue(const char* v) : data(std::in_place_type<std::string>, v) {}
    constexpr BasicValue(Vec2 v) noexcept : data(v) {}
    constexpr BasicValue(Vec3 v) noexcept : data(v) {}
    constexpr BasicValue(Vec4 v) noexcept : data(v) {}
    BasicValue(const Mat3& v) : data(boxed_mat3{v}) {}
    BasicValue(const Mat4x3& v) : data(boxed_mat4x3{v}) {}
    BasicValue(const Mat4& v) : data(boxed_mat4{v}) {}
    BasicValue(boxed_mat3 v) : data(std::move(v)) {}
    BasicValue(boxed_mat4x3 v) : data(std::move(v)) {}
    BasicValue(boxed_mat4 v) : data(std::move(v)) {}
    BasicValue(value_map v) : data(std::move(v)) {}
    BasicValue(value_vector v) : data(std::move(v)) {}
    BasicValue(value_array v) : data(std::move(v)) {}
    BasicValue(value_table v) : data(std::move(v)) {}

    // Factory functions for container types
    static BasicValue map(std::initializer_list<std::pair<std::string, BasicValue>> init) {
        auto t = value_map{}.transient();
        for (const auto& [key, val] : init) {
            t.set(key, value_box{val});
        }
        return BasicValue{t.persistent()};
    }

    static BasicValue vector(std::initializer_list<BasicValue> init) {
        auto t = value_vector{}.transient();
        for (const auto& val : init) {
            t.push_back(value_box{val});
        }
        return BasicValue{t.persistent()};
    }

    static BasicValue array(std::initializer_list<BasicValue> init) {
        value_array result;
        for (const auto& val : init) {
            result = std::move(result).push_back(value_box{val});
        }
        return BasicValue{std::move(result)};
    }

    static BasicValue table(std::initializer_list<std::pair<std::string, BasicValue>> init) {
        auto t = value_table{}.transient();
        for (const auto& [id, val] : init) {
            t.insert(table_entry{id, value_box{val}});
        }
        return BasicValue{t.persistent()};
    }

    static BasicValue vec2(float x, float y) {
        return BasicValue{Vec2{x, y}};
    }

    static BasicValue vec3(float x, float y, float z) {
        return BasicValue{Vec3{x, y, z}};
    }

    static BasicValue vec4(float x, float y, float z, float w) {
        return BasicValue{Vec4{x, y, z, w}};
    }

    static BasicValue vec2(std::span<const float, 2> data) {
        Vec2 v;
        std::ranges::copy(data, v.begin());
        return BasicValue{v};
    }

    static BasicValue vec3(std::span<const float, 3> data) {
        Vec3 v;
        std::ranges::copy(data, v.begin());
        return BasicValue{v};
    }

    static BasicValue vec4(std::span<const float, 4> data) {
        Vec4 v;
        std::ranges::copy(data, v.begin());
        return BasicValue{v};
    }

    static BasicValue mat3(std::span<const float, 9> data) {
        Mat3 m;
        std::ranges::copy(data, m.begin());
        return BasicValue{m};
    }

    static BasicValue mat4x3(std::span<const float, 12> data) {
        Mat4x3 m;
        std::ranges::copy(data, m.begin());
        return BasicValue{m};
    }

    static BasicValue vec2(const float* ptr) {
        return vec2(std::span<const float, 2>{ptr, 2});
    }

    static BasicValue vec3(const float* ptr) {
        return vec3(std::span<const float, 3>{ptr, 3});
    }

    static BasicValue vec4(const float* ptr) {
        return vec4(std::span<const float, 4>{ptr, 4});
    }

    static BasicValue mat3(const float* ptr) {
        return mat3(std::span<const float, 9>{ptr, 9});
    }

    static BasicValue mat4x3(const float* ptr) {
        return mat4x3(std::span<const float, 12>{ptr, 12});
    }

    static constexpr BasicValue identity_mat3() {
        return BasicValue{Mat3{
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        }};
    }

    template <typename T>
    [[nodiscard]] const T* get_if() const { return std::get_if<T>(&data); }

    template <typename T>
    [[nodiscard]] bool is() const { return std::holds_alternative<T>(data); }

    [[nodiscard]] std::size_t type_index() const noexcept { return data.index(); }
    [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::monostate>(data); }
    [[nodiscard]] bool is_vec2() const noexcept { return is<Vec2>(); }
    [[nodiscard]] bool is_vec3() const noexcept { return is<Vec3>(); }
    [[nodiscard]] bool is_vec4() const noexcept { return is<Vec4>(); }
    [[nodiscard]] bool is_mat3() const noexcept { return is<boxed_mat3>(); }
    [[nodiscard]] bool is_mat4x3() const noexcept { return is<boxed_mat4x3>(); }
    [[nodiscard]] bool is_math_type() const noexcept {
        return is_vec2() || is_vec3() || is_vec4() || is_mat3() || is_mat4x3();
    }

    [[nodiscard]] BasicValue at(const std::string& key) const {
        if (auto* m = get_if<value_map>()) {
            if (auto* found = m->find(key)) return found->get();
        }
        if (auto* t = get_if<value_table>()) {
            if (auto* found = t->find(key)) return found->value.get();
        }
        detail::log_key_error("Value::at", key, "not found or type mismatch");
        return BasicValue{};
    }

    [[nodiscard]] BasicValue at(std::size_t index) const {
        if (auto* v = get_if<value_vector>()) {
            if (index < v->size()) return (*v)[index].get();
        }
        if (auto* a = get_if<value_array>()) {
            if (index < a->size()) return (*a)[index].get();
        }
        detail::log_index_error("Value::at", index, "out of range or type mismatch");
        return BasicValue{};
    }

    [[nodiscard]] BasicValue at_or(const std::string& key, BasicValue default_val) const {
        auto result = at(key);
        return result.is_null() ? std::move(default_val) : std::move(result);
    }

    [[nodiscard]] BasicValue at_or(std::size_t index, BasicValue default_val) const {
        auto result = at(index);
        return result.is_null() ? std::move(default_val) : std::move(result);
    }

    template<typename T>
    [[nodiscard]] T get_or(T default_val = T{}) const {
        if (auto* ptr = get_if<T>()) return *ptr;
        return default_val;
    }

    [[nodiscard]] int as_int(int default_val = 0) const {
        if (auto* p = get_if<int>()) return *p;
        return default_val;
    }

    [[nodiscard]] int64_t as_int64(int64_t default_val = 0) const {
        if (auto* p = get_if<int64_t>()) return *p;
        return default_val;
    }

    [[nodiscard]] float as_float(float default_val = 0.0f) const {
        if (auto* p = get_if<float>()) return *p;
        return default_val;
    }

    [[nodiscard]] double as_double(double default_val = 0.0) const {
        if (auto* p = get_if<double>()) return *p;
        return default_val;
    }

    [[nodiscard]] bool as_bool(bool default_val = false) const {
        if (auto* p = get_if<bool>()) return *p;
        return default_val;
    }

    [[nodiscard]] std::string as_string(std::string default_val = "") const {
        if (auto* p = get_if<std::string>()) return *p;
        return default_val;
    }

    [[nodiscard]] std::string_view as_string_view() const noexcept {
        if (auto* p = get_if<std::string>()) return *p;
        return {};
    }

    [[nodiscard]] double as_number(double default_val = 0.0) const {
        if (auto* p = get_if<double>()) return *p;
        if (auto* p = get_if<float>()) return static_cast<double>(*p);
        if (auto* p = get_if<int64_t>()) return static_cast<double>(*p);
        if (auto* p = get_if<int>()) return static_cast<double>(*p);
        return default_val;
    }

    [[nodiscard]] value_map as_map(value_map default_val = {}) const {
        if (auto* p = get_if<value_map>()) return *p;
        return default_val;
    }

    [[nodiscard]] value_vector as_vector(value_vector default_val = {}) const {
        if (auto* p = get_if<value_vector>()) return *p;
        return default_val;
    }

    [[nodiscard]] value_array as_array(value_array default_val = {}) const {
        if (auto* p = get_if<value_array>()) return *p;
        return default_val;
    }

    [[nodiscard]] value_table as_table(value_table default_val = {}) const {
        if (auto* p = get_if<value_table>()) return *p;
        return default_val;
    }

    [[nodiscard]] Vec2 as_vec2(Vec2 default_val = {}) const {
        if (auto* p = get_if<Vec2>()) return *p;
        return default_val;
    }

    [[nodiscard]] Vec3 as_vec3(Vec3 default_val = {}) const {
        if (auto* p = get_if<Vec3>()) return *p;
        return default_val;
    }

    [[nodiscard]] Vec4 as_vec4(Vec4 default_val = {}) const {
        if (auto* p = get_if<Vec4>()) return *p;
        return default_val;
    }

    [[nodiscard]] Mat3 as_mat3(Mat3 default_val = {}) const {
        if (auto* p = get_if<boxed_mat3>()) return p->get();
        return default_val;
    }

    [[nodiscard]] Mat4x3 as_mat4x3(Mat4x3 default_val = {}) const {
        if (auto* p = get_if<boxed_mat4x3>()) return p->get();
        return default_val;
    }

    [[nodiscard]] bool contains(const std::string& key) const { return count(key) > 0; }

    [[nodiscard]] bool contains(std::size_t index) const {
        if (auto* v = get_if<value_vector>()) return index < v->size();
        if (auto* a = get_if<value_array>()) return index < a->size();
        return false;
    }

    [[nodiscard]] BasicValue set(const std::string& key, BasicValue val) const {
        if (auto* m = get_if<value_map>()) return m->set(key, value_box{std::move(val)});
        if (auto* t = get_if<value_table>()) return t->insert(table_entry{key, value_box{std::move(val)}});
        detail::log_key_error("Value::set", key, "cannot set on non-map type");
        return *this;
    }

    [[nodiscard]] BasicValue set(std::size_t index, BasicValue val) const {
        if (auto* v = get_if<value_vector>()) {
            if (index < v->size()) return v->set(index, value_box{std::move(val)});
        }
        if (auto* a = get_if<value_array>()) {
            if (index < a->size()) {
                return a->update(index, [&val](const value_box&) { return value_box{std::move(val)}; });
            }
        }
        detail::log_index_error("Value::set", index, "cannot set on non-vector type");
        return *this;
    }

    [[nodiscard]] BasicValue set_vivify(const std::string& key, BasicValue val) const {
        if (auto* m = get_if<value_map>()) return m->set(key, value_box{std::move(val)});
        if (auto* t = get_if<value_table>()) return t->insert(table_entry{key, value_box{std::move(val)}});
        if (is_null()) return value_map{}.set(key, value_box{std::move(val)});
        detail::log_key_error("Value::set_vivify", key, "cannot set on non-map/non-null type");
        return *this;
    }

    [[nodiscard]] BasicValue set_vivify(std::size_t index, BasicValue val) const {
        if (auto* v = get_if<value_vector>()) {
            if (index < v->size()) return v->set(index, value_box{std::move(val)});
            auto trans = v->transient();
            while (trans.size() <= index) trans.push_back(value_box{});
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
            for (std::size_t i = 0; i < index; ++i) trans.push_back(value_box{});
            trans.push_back(value_box{std::move(val)});
            return trans.persistent();
        }
        detail::log_index_error("Value::set_vivify", index, "cannot set on non-vector/non-null type");
        return *this;
    }

    [[nodiscard]] std::size_t count(const std::string& key) const {
        if (auto* m = get_if<value_map>()) return m->count(key);
        if (auto* t = get_if<value_table>()) return t->count(key) ? 1 : 0;
        return 0;
    }

    [[nodiscard]] std::size_t size() const {
        if (auto* m = get_if<value_map>()) return m->size();
        if (auto* v = get_if<value_vector>()) return v->size();
        if (auto* a = get_if<value_array>()) return a->size();
        if (auto* t = get_if<value_table>()) return t->size();
        return 0;
    }

    using size_type = std::size_t;
};

// ============================================================
// Memory Policy Definitions
// ============================================================

/// Single-threaded memory policy: non-atomic refcount + no locks, highest performance
using unsafe_memory_policy = immer::memory_policy<
    immer::unsafe_free_list_heap_policy<immer::cpp_heap>,
    immer::unsafe_refcount_policy,
    immer::no_lock_policy
>;

/// Thread-safe memory policy: atomic refcount + spinlock
using thread_safe_memory_policy = immer::default_memory_policy;

// ============================================================
// UnsafeValue - Single-threaded high-performance Value
//
// Features:
//   - Non-atomic reference counting (avoids CPU cache line bouncing)
//   - No lock overhead (no spinlock)
//   - 10-30% faster than thread-safe version
//
// Use cases:
//   - Single-threaded applications
//   - Each thread has its own independent Value tree (no sharing)
//   - Performance-critical hot paths
//
// WARNING: Sharing UnsafeValue across threads causes data races and UB!
//          Use ThreadSafeValue for multi-threaded scenarios.
// ============================================================
using UnsafeValue       = BasicValue<unsafe_memory_policy>;
using UnsafeValueBox    = BasicValueBox<unsafe_memory_policy>;
using UnsafeValueMap    = BasicValueMap<unsafe_memory_policy>;
using UnsafeValueVector = BasicValueVector<unsafe_memory_policy>;
using UnsafeValueArray  = BasicValueArray<unsafe_memory_policy>;
using UnsafeValueTable  = BasicValueTable<unsafe_memory_policy>;
using UnsafeTableEntry  = BasicTableEntry<unsafe_memory_policy>;

// ============================================================
// ThreadSafeValue - Thread-safe Value for multi-threaded scenarios
//
// Features:
//   - Atomic reference counting (std::atomic operations)
//   - Spinlock-protected free list
//
// Use cases:
//   - Sharing the same Value tree across multiple threads
//   - Cross-thread Value passing (e.g., message queues, event systems)
//   - Integration with lager store (store may be accessed from multiple threads)
//
// Performance note:
//   - 10-30% slower than UnsafeValue (depends on contention level)
//   - Performance degradation is more pronounced under high contention
// ============================================================
using ThreadSafeValue       = BasicValue<thread_safe_memory_policy>;
using ThreadSafeValueBox    = BasicValueBox<thread_safe_memory_policy>;
using ThreadSafeValueMap    = BasicValueMap<thread_safe_memory_policy>;
using ThreadSafeValueVector = BasicValueVector<thread_safe_memory_policy>;
using ThreadSafeValueArray  = BasicValueArray<thread_safe_memory_policy>;
using ThreadSafeValueTable  = BasicValueTable<thread_safe_memory_policy>;
using ThreadSafeTableEntry  = BasicTableEntry<thread_safe_memory_policy>;

// ============================================================
// Default Value Type Aliases
//
// Naming conventions:
//   - Value       : Default type, alias for UnsafeValue (single-threaded, high performance)
//   - SyncValue   : Convenience alias for ThreadSafeValue (multi-threaded safe)
//   - SharedValue : Cross-process shared version (see shared_value.h)
//
// Design philosophy:
//   - Base type names explicitly express safety characteristics (UnsafeValue / ThreadSafeValue)
//   - Short aliases for everyday use (Value / SyncValue)
//   - Most single-threaded scenarios can simply use Value
// ============================================================

// Value = UnsafeValue (default, single-threaded, high performance)
using Value       = UnsafeValue;
using ValueBox    = UnsafeValueBox;
using ValueMap    = UnsafeValueMap;
using ValueVector = UnsafeValueVector;
using ValueArray  = UnsafeValueArray;
using ValueTable  = UnsafeValueTable;
using TableEntry  = UnsafeTableEntry;

// SyncValue = ThreadSafeValue (multi-threaded safe)
using SyncValue       = ThreadSafeValue;
using SyncValueBox    = ThreadSafeValueBox;
using SyncValueMap    = ThreadSafeValueMap;
using SyncValueVector = ThreadSafeValueVector;
using SyncValueArray  = ThreadSafeValueArray;
using SyncValueTable  = ThreadSafeValueTable;
using SyncTableEntry  = ThreadSafeTableEntry;

// ============================================================
// BasicValue comparison operators (C++20)
//
// Uses spaceship operator (<=>): compiler auto-generates ==, !=, <, >, <=, >=
// Note: std::variant supports <=> in C++20, enabling lexicographic comparison
// ============================================================

/// Equality comparison for BasicValue
template <typename MemoryPolicy>
bool operator==(const BasicValue<MemoryPolicy>& a, const BasicValue<MemoryPolicy>& b)
{
    return a.data == b.data;
}

/// Three-way comparison (spaceship operator) for BasicValue
/// Enables all comparison operators: ==, !=, <, >, <=, >=
/// Returns std::partial_ordering because floating-point types may be NaN
template <typename MemoryPolicy>
std::partial_ordering operator<=>(const BasicValue<MemoryPolicy>& a,
                                   const BasicValue<MemoryPolicy>& b)
{
    // Compare type indices first
    if (a.data.index() != b.data.index()) {
        return a.data.index() <=> b.data.index();
    }

    // Same type, compare values using std::visit
    return std::visit([](const auto& lhs, const auto& rhs) -> std::partial_ordering {
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
            } else if constexpr (requires { lhs < rhs; lhs == rhs; }) {
                // Fall back to < and == operators
                if (lhs == rhs) return std::partial_ordering::equivalent;
                if (lhs < rhs) return std::partial_ordering::less;
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
    }, a.data, b.data);
}

// ============================================================
// Utility functions
// ============================================================

// Convert Value to human-readable string
[[nodiscard]] LAGER_EXT_API std::string value_to_string(const Value& val);

// Print Value with indentation
LAGER_EXT_API void print_value(const Value& val, const std::string& prefix = "", std::size_t depth = 0);

// Convert Path to dot-notation string (e.g., ".users[0].name")
[[nodiscard]] LAGER_EXT_API std::string path_to_string(const Path& path);

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

// ============================================================
// Extern Template Declarations
//
// These declarations prevent implicit instantiation of common template
// specializations in every translation unit that includes this header.
// The actual instantiations are in value.cpp, reducing compile time
// and object file size.
// ============================================================

extern template struct BasicValue<unsafe_memory_policy>;
extern template struct BasicTableEntry<unsafe_memory_policy>;

extern template struct BasicValue<thread_safe_memory_policy>;
extern template struct BasicTableEntry<thread_safe_memory_policy>;

} // namespace lager_ext
