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

    BasicValue() noexcept : data(std::monostate{}) {}
    BasicValue(int8_t v) noexcept : data(v) {}
    BasicValue(int16_t v) noexcept : data(v) {}
    BasicValue(int32_t v) noexcept : data(v) {}
    BasicValue(int64_t v) noexcept : data(v) {}
    BasicValue(uint8_t v) noexcept : data(v) {}
    BasicValue(uint16_t v) noexcept : data(v) {}
    BasicValue(uint32_t v) noexcept : data(v) {}
    BasicValue(uint64_t v) noexcept : data(v) {}
    BasicValue(float v) noexcept : data(v) {}
    BasicValue(double v) noexcept : data(v) {}
    BasicValue(bool v) noexcept : data(v) {}
    BasicValue(const std::string& v) : data(v) {}
    BasicValue(std::string&& v) noexcept : data(std::move(v)) {}
    BasicValue(const char* v) : data(std::in_place_type<std::string>, v) {}
    BasicValue(Vec2 v) noexcept : data(v) {}
    BasicValue(Vec3 v) noexcept : data(v) {}
    BasicValue(Vec4 v) noexcept : data(v) {}
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

// Builder classes for O(n) construction using immer's transient API
template <typename MemoryPolicy>
class BasicMapBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_map = BasicValueMap<MemoryPolicy>;
    using value_vector = BasicValueVector<MemoryPolicy>;
    using transient_type = typename value_map::transient_type;

    BasicMapBuilder() : transient_(value_map{}.transient()) {}
    explicit BasicMapBuilder(const value_map& existing) : transient_(existing.transient()) {}

    ///   auto result = MapBuilder(config)
    ///       .set("updated", true)
    ///       .finish();
    explicit BasicMapBuilder(const value_type& existing) 
        : transient_(existing.template is<value_map>() 
            ? existing.template get_if<value_map>()->transient()
            : value_map{}.transient()) {}

    // Move operations (allowed)
    BasicMapBuilder(BasicMapBuilder&&) noexcept = default;
    BasicMapBuilder& operator=(BasicMapBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    BasicMapBuilder(const BasicMapBuilder&) = delete;
    BasicMapBuilder& operator=(const BasicMapBuilder&) = delete;

    /// Set a key-value pair
    /// @param key The key
    /// @param val The value (any type convertible to BasicValue)
    /// @return Reference to this builder for chaining
    template <typename T>
    BasicMapBuilder& set(const std::string& key, T&& val) {
        transient_.set(key, value_box{value_type{std::forward<T>(val)}});
        return *this;
    }

    /// Set a key with an already constructed BasicValue
    BasicMapBuilder& set(const std::string& key, value_type val) {
        transient_.set(key, value_box{std::move(val)});
        return *this;
    }

    /// Check if the builder contains a key
    [[nodiscard]] bool contains(const std::string& key) const {
        return transient_.count(key) > 0;
    }

    /// Get current size
    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    // ============================================================
    // Access and Update Methods (for modifying previously set values)
    // ============================================================

    /// Get a previously set value by key
    /// @param key The key to look up
    /// @param default_val Value to return if key doesn't exist (default: null)
    /// @return The value, or default_val if not found
    [[nodiscard]] value_type get(const std::string& key, value_type default_val = value_type{}) const {
        if (auto* found = transient_.find(key)) {
            return found->get();
        }
        return default_val;
    }

    /// Update a previously set value by key using a function
    /// @param key The key to update
    /// @param fn Function taking value_type and returning value_type
    /// @return Reference to this builder for chaining
    /// @note If key doesn't exist, no change is made
    /// @example
    ///   builder.set("items", Value::vector({1, 2}))
    ///          .update_at("items", [](Value v) {
    ///              return v.set_vivify(v.size(), Value{3});  // append 3
    ///          });
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicMapBuilder& update_at(const std::string& key, Fn&& fn) {
        if (auto* found = transient_.find(key)) {
            auto new_val = std::forward<Fn>(fn)(found->get());
            transient_.set(key, value_box{std::move(new_val)});
        }
        return *this;
    }

    /// Update or insert: fn receives current value (null if key doesn't exist)
    /// @param key The key to upsert
    /// @param fn Function taking value_type (may be null) and returning value_type
    /// @return Reference to this builder for chaining
    /// @note Unlike update_at, this always calls fn and sets the result
    /// @example
    ///   // Append to existing vector or create new one
    ///   builder.upsert("items", [](Value current) {
    ///       if (current.is_null()) {
    ///           return Value::vector({"first_item"});
    ///       }
    ///       return current.set_vivify(current.size(), Value{"new_item"});
    ///   });
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicMapBuilder& upsert(const std::string& key, Fn&& fn) {
        value_type current{};
        if (auto* found = transient_.find(key)) {
            current = found->get();
        }
        auto new_val = std::forward<Fn>(fn)(std::move(current));
        transient_.set(key, value_box{std::move(new_val)});
        return *this;
    }

    /// Set value at a nested path with auto-vivification
    /// Creates intermediate maps/vectors as needed
    /// @param path The path (e.g., {"users", 0, "name"})
    /// @param val The value to set
    /// @return Reference to this builder for chaining
    /// @example
    ///   builder.set_in({"config", "display", "width"}, 1920);
    template <typename T>
    BasicMapBuilder& set_in(const Path& path, T&& val) {
        if (path.empty()) return *this;
        
        // Get first key (must be string for map)
        auto* first_key = std::get_if<std::string>(&path[0]);
        if (!first_key) return *this;
        
        if (path.size() == 1) {
            // Single element path, just set directly
            return set(*first_key, std::forward<T>(val));
        }
        
        // Get or create the root value for this key
        value_type root_val = get(*first_key);
        
        // Build sub-path (skip first element)
        Path sub_path(path.begin() + 1, path.end());
        
        // Use recursive vivify to set the nested value
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, value_type{std::forward<T>(val)});
        transient_.set(*first_key, value_box{std::move(new_root)});
        return *this;
    }

    /// Update value at a nested path using a function
    /// @param path The path to the value
    /// @param fn Function taking value_type and returning value_type
    /// @return Reference to this builder for chaining
    /// @example
    ///   builder.update_in({"users", 0, "age"}, [](Value v) {
    ///       return Value{v.get_or<int>(0) + 1};  // increment age
    ///   });
    template<typename Fn>
    BasicMapBuilder& update_in(const Path& path, Fn&& fn) {
        if (path.empty()) return *this;
        
        auto* first_key = std::get_if<std::string>(&path[0]);
        if (!first_key) return *this;
        
        if (path.size() == 1) {
            return update_at(*first_key, std::forward<Fn>(fn));
        }
        
        value_type root_val = get(*first_key);
        Path sub_path(path.begin() + 1, path.end());
        
        // Get current value at path
        value_type current = get_at_path_impl(root_val, sub_path, 0);
        // Apply function
        value_type new_val = std::forward<Fn>(fn)(std::move(current));
        // Set back
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, std::move(new_val));
        transient_.set(*first_key, value_box{std::move(new_root)});
        return *this;
    }

    /// Finish building and return the immutable Value
    /// Note: After calling finish(), the builder is in an undefined state
    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    /// Finish and return just the map (not wrapped in Value)
    [[nodiscard]] value_map finish_map() {
        return transient_.persistent();
    }

private:
    transient_type transient_;

    // Helper: get value at path
    static value_type get_at_path_impl(const value_type& root, const Path& path, std::size_t idx) {
        if (idx >= path.size()) return root;
        
        value_type child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, path[idx]);
        
        if (child.is_null()) return child;
        return get_at_path_impl(child, path, idx + 1);
    }

    // Helper: set value at path with vivification
    static value_type set_at_path_vivify_impl(
        const value_type& root,
        const Path& path,
        std::size_t idx,
        value_type new_val)
    {
        if (idx >= path.size()) return new_val;
        
        const auto& elem = path[idx];
        value_type current_child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, elem);
        
        // Prepare child for next level if needed
        if (current_child.is_null() && idx + 1 < path.size()) {
            const auto& next = path[idx + 1];
            if (std::holds_alternative<std::string>(next)) {
                current_child = value_type{value_map{}};
            } else {
                current_child = value_type{value_vector{}};
            }
        }
        
        value_type new_child = set_at_path_vivify_impl(current_child, path, idx + 1, std::move(new_val));
        
        // Set child back to parent
        return std::visit([&root, &new_child](const auto& key_or_idx) -> value_type {
            using T = std::decay_t<decltype(key_or_idx)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return root.set_vivify(key_or_idx, std::move(new_child));
            } else {
                return root.set_vivify(key_or_idx, std::move(new_child));
            }
        }, elem);
    }
};

/// Builder for constructing value_vector efficiently - O(n) complexity
template <typename MemoryPolicy>
class BasicVectorBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_map = BasicValueMap<MemoryPolicy>;
    using value_vector = BasicValueVector<MemoryPolicy>;
    using transient_type = typename value_vector::transient_type;

    /// Create an empty vector builder
    BasicVectorBuilder() : transient_(value_vector{}.transient()) {}

    /// Create a builder from an existing vector (for incremental modification)
    /// @param existing The existing vector to start from
    /// @note This enables efficient batch appends/modifications on existing data
    /// @example
    ///   ValueVector existing = get_items();
    ///   auto result = VectorBuilder(existing)
    ///       .push_back("new_item")
    ///       .push_back("another")
    ///       .finish();
    explicit BasicVectorBuilder(const value_vector& existing) 
        : transient_(existing.transient()) {}

    /// Create a builder from a Value containing a vector
    /// @param existing The Value (must contain value_vector, otherwise starts empty)
    /// @example
    ///   Value items = data.at("items");
    ///   auto result = VectorBuilder(items)
    ///       .push_back("appended")
    ///       .finish();
    explicit BasicVectorBuilder(const value_type& existing) 
        : transient_(existing.template is<value_vector>() 
            ? existing.template get_if<value_vector>()->transient()
            : value_vector{}.transient()) {}

    // Move operations (allowed)
    BasicVectorBuilder(BasicVectorBuilder&&) noexcept = default;
    BasicVectorBuilder& operator=(BasicVectorBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    BasicVectorBuilder(const BasicVectorBuilder&) = delete;
    BasicVectorBuilder& operator=(const BasicVectorBuilder&) = delete;

    /// Append a value to the end
    /// @param val The value (any type convertible to BasicValue)
    /// @return Reference to this builder for chaining
    template <typename T>
    BasicVectorBuilder& push_back(T&& val) {
        transient_.push_back(value_box{value_type{std::forward<T>(val)}});
        return *this;
    }

    /// Append an already constructed BasicValue
    BasicVectorBuilder& push_back(value_type val) {
        transient_.push_back(value_box{std::move(val)});
        return *this;
    }

    /// Set value at index (must be within current size)
    template <typename T>
    BasicVectorBuilder& set(std::size_t index, T&& val) {
        if (index < transient_.size()) {
            transient_.set(index, value_box{value_type{std::forward<T>(val)}});
        }
        return *this;
    }

    /// Get current size
    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    // ============================================================
    // Access and Update Methods (for modifying previously set values)
    // ============================================================

    /// Get a previously set value by index
    /// @param index The index to look up
    /// @param default_val Value to return if index is out of range (default: null)
    /// @return The value, or default_val if out of range
    [[nodiscard]] value_type get(std::size_t index, value_type default_val = value_type{}) const {
        if (index < transient_.size()) {
            return transient_[index].get();
        }
        return default_val;
    }

    /// Update a previously set value by index using a function
    /// @param index The index to update
    /// @param fn Function taking value_type and returning value_type
    /// @return Reference to this builder for chaining
    /// @example
    ///   builder.push_back(Value::map({{"count", 0}}))
    ///          .update_at(0, [](Value v) {
    ///              return v.set("count", Value{v.at("count").get_or<int>(0) + 1});
    ///          });
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicVectorBuilder& update_at(std::size_t index, Fn&& fn) {
        if (index < transient_.size()) {
            auto new_val = std::forward<Fn>(fn)(transient_[index].get());
            transient_.set(index, value_box{std::move(new_val)});
        }
        return *this;
    }

    /// Set value at a nested path with auto-vivification
    /// First element must be an index into the vector
    /// @param path The path (e.g., {0, "name"} for vec[0]["name"])
    /// @param val The value to set
    /// @return Reference to this builder for chaining
    /// @example
    ///   builder.push_back(Value::map({}))
    ///          .set_in({0, "user", "name"}, "Alice");
    template <typename T>
    BasicVectorBuilder& set_in(const Path& path, T&& val) {
        if (path.empty()) return *this;
        
        // Get first index
        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size()) return *this;
        
        if (path.size() == 1) {
            // Single element path, just set directly
            return set(*first_idx, std::forward<T>(val));
        }
        
        // Get the root value at this index
        value_type root_val = transient_[*first_idx].get();
        
        // Build sub-path (skip first element)
        Path sub_path(path.begin() + 1, path.end());
        
        // Use recursive vivify to set the nested value
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, value_type{std::forward<T>(val)});
        transient_.set(*first_idx, value_box{std::move(new_root)});
        return *this;
    }

    /// Update value at a nested path using a function
    /// @param path The path to the value
    /// @param fn Function taking value_type and returning value_type
    /// @return Reference to this builder for chaining
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicVectorBuilder& update_in(const Path& path, Fn&& fn) {
        if (path.empty()) return *this;
        
        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size()) return *this;
        
        if (path.size() == 1) {
            return update_at(*first_idx, std::forward<Fn>(fn));
        }
        
        value_type root_val = transient_[*first_idx].get();
        Path sub_path(path.begin() + 1, path.end());
        
        // Get current value at path
        value_type current = get_at_path_impl(root_val, sub_path, 0);
        // Apply function
        value_type new_val = std::forward<Fn>(fn)(std::move(current));
        // Set back
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, std::move(new_val));
        transient_.set(*first_idx, value_box{std::move(new_root)});
        return *this;
    }

    /// Finish building and return the immutable Value
    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    /// Finish and return just the vector (not wrapped in Value)
    [[nodiscard]] value_vector finish_vector() {
        return transient_.persistent();
    }

private:
    transient_type transient_;

    // Helper: get value at path
    static value_type get_at_path_impl(const value_type& root, const Path& path, std::size_t idx) {
        if (idx >= path.size()) return root;
        
        value_type child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, path[idx]);
        
        if (child.is_null()) return child;
        return get_at_path_impl(child, path, idx + 1);
    }

    // Helper: set value at path with vivification
    static value_type set_at_path_vivify_impl(
        const value_type& root,
        const Path& path,
        std::size_t idx,
        value_type new_val)
    {
        if (idx >= path.size()) return new_val;
        
        const auto& elem = path[idx];
        value_type current_child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, elem);
        
        // Prepare child for next level if needed
        if (current_child.is_null() && idx + 1 < path.size()) {
            const auto& next = path[idx + 1];
            if (std::holds_alternative<std::string>(next)) {
                current_child = value_type{value_map{}};
            } else {
                current_child = value_type{value_vector{}};
            }
        }
        
        value_type new_child = set_at_path_vivify_impl(current_child, path, idx + 1, std::move(new_val));
        
        // Set child back to parent
        return std::visit([&root, &new_child](const auto& key_or_idx) -> value_type {
            return root.set_vivify(key_or_idx, std::move(new_child));
        }, elem);
    }
};

/// Builder for constructing value_array efficiently - O(n) complexity
template <typename MemoryPolicy>
class BasicArrayBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_array = BasicValueArray<MemoryPolicy>;
    using transient_type = typename value_array::transient_type;

    /// Create an empty array builder
    BasicArrayBuilder() : transient_(value_array{}.transient()) {}

    /// Create a builder from an existing array (for appending)
    /// @param existing The existing array to start from
    /// @example
    ///   ValueArray existing = get_coords();
    ///   auto result = ArrayBuilder(existing)
    ///       .push_back(new_coord)
    ///       .finish();
    explicit BasicArrayBuilder(const value_array& existing) 
        : transient_(existing.transient()) {}

    /// Create a builder from a Value containing an array
    /// @param existing The Value (must contain value_array, otherwise starts empty)
    explicit BasicArrayBuilder(const value_type& existing) 
        : transient_(existing.template is<value_array>() 
            ? existing.template get_if<value_array>()->transient()
            : value_array{}.transient()) {}

    // Move operations (allowed)
    BasicArrayBuilder(BasicArrayBuilder&&) noexcept = default;
    BasicArrayBuilder& operator=(BasicArrayBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    BasicArrayBuilder(const BasicArrayBuilder&) = delete;
    BasicArrayBuilder& operator=(const BasicArrayBuilder&) = delete;

    /// Append a value to the end
    template <typename T>
    BasicArrayBuilder& push_back(T&& val) {
        transient_.push_back(value_box{value_type{std::forward<T>(val)}});
        return *this;
    }

    /// Append an already constructed BasicValue
    BasicArrayBuilder& push_back(value_type val) {
        transient_.push_back(value_box{std::move(val)});
        return *this;
    }

    /// Get current size
    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    /// Finish building and return the immutable Value
    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    /// Finish and return just the array (not wrapped in Value)
    [[nodiscard]] value_array finish_array() {
        return transient_.persistent();
    }

private:
    transient_type transient_;
};

/// Builder for constructing value_table efficiently - O(n) complexity
template <typename MemoryPolicy>
class BasicTableBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_table = BasicValueTable<MemoryPolicy>;
    using table_entry = BasicTableEntry<MemoryPolicy>;
    using transient_type = typename value_table::transient_type;

    /// Create an empty table builder
    BasicTableBuilder() : transient_(value_table{}.transient()) {}

    /// Create a builder from an existing table (for incremental modification)
    /// @param existing The existing table to start from
    /// @note This enables efficient batch modifications on existing data
    /// @example
    ///   ValueTable existing = get_users();
    ///   auto result = TableBuilder(existing)
    ///       .insert("new_user", user_data)
    ///       .update("existing_user", update_fn)
    ///       .finish();
    explicit BasicTableBuilder(const value_table& existing) 
        : transient_(existing.transient()) {}

    /// Create a builder from a Value containing a table
    /// @param existing The Value (must contain value_table, otherwise starts empty)
    /// @example
    ///   Value users = data.at("users");
    ///   auto result = TableBuilder(users)
    ///       .insert("new_id", new_entry)
    ///       .finish();
    explicit BasicTableBuilder(const value_type& existing) 
        : transient_(existing.template is<value_table>() 
            ? existing.template get_if<value_table>()->transient()
            : value_table{}.transient()) {}

    // Move operations (allowed)
    BasicTableBuilder(BasicTableBuilder&&) noexcept = default;
    BasicTableBuilder& operator=(BasicTableBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    BasicTableBuilder(const BasicTableBuilder&) = delete;
    BasicTableBuilder& operator=(const BasicTableBuilder&) = delete;

    /// Insert or update an entry by id
    /// @param id The unique identifier
    /// @param val The value (any type convertible to BasicValue)
    /// @return Reference to this builder for chaining
    template <typename T>
    BasicTableBuilder& insert(const std::string& id, T&& val) {
        transient_.insert(table_entry{id, value_box{value_type{std::forward<T>(val)}}});
        return *this;
    }

    /// Insert with an already constructed BasicValue
    BasicTableBuilder& insert(const std::string& id, value_type val) {
        transient_.insert(table_entry{id, value_box{std::move(val)}});
        return *this;
    }

    /// Check if the builder contains an id
    [[nodiscard]] bool contains(const std::string& id) const {
        return transient_.count(id) > 0;
    }

    /// Get a value by id
    /// @param id The unique identifier
    /// @param default_val Value to return if id doesn't exist (default: null)
    /// @return The value if found, or default_val if not found
    [[nodiscard]] value_type get(const std::string& id, value_type default_val = value_type{}) const {
        const auto* ptr = transient_.find(id);
        if (ptr) {
            return ptr->value.get();
        }
        return default_val;
    }

    /// Update an existing entry by id using a function
    /// @param id The unique identifier
    /// @param fn Function: Value -> Value (or compatible type)
    /// @return Reference to this builder for chaining
    /// @note If id doesn't exist, no change is made
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicTableBuilder& update(const std::string& id, Fn&& fn) {
        const auto* ptr = transient_.find(id);
        if (ptr) {
            auto new_val = value_type{std::forward<Fn>(fn)(ptr->value.get())};
            transient_.insert(table_entry{id, value_box{std::move(new_val)}});
        }
        return *this;
    }

    /// Update or insert: fn receives current value (null if id doesn't exist)
    /// @param id The unique identifier
    /// @param fn Function taking value_type (may be null) and returning value_type
    /// @return Reference to this builder for chaining
    /// @note Unlike update, this always calls fn and inserts the result
    /// @example
    ///   // Increment counter or initialize to 1
    ///   builder.upsert("user_001", [](Value current) {
    ///       if (current.is_null()) {
    ///           return Value::map({{"visits", 1}});
    ///       }
    ///       int visits = current.at("visits").get_or<int>(0);
    ///       return current.set("visits", Value{visits + 1});
    ///   });
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicTableBuilder& upsert(const std::string& id, Fn&& fn) {
        value_type current{};
        const auto* ptr = transient_.find(id);
        if (ptr) {
            current = ptr->value.get();
        }
        auto new_val = std::forward<Fn>(fn)(std::move(current));
        transient_.insert(table_entry{id, value_box{std::move(new_val)}});
        return *this;
    }

    /// Get current size
    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    /// Finish building and return the immutable Value
    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    /// Finish and return just the table (not wrapped in Value)
    [[nodiscard]] value_table finish_table() {
        return transient_.persistent();
    }

private:
    transient_type transient_;
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
// Builder type aliases
// ============================================================

// Unsafe (single-threaded) builders - use with Value
using MapBuilder    = BasicMapBuilder<unsafe_memory_policy>;
using VectorBuilder = BasicVectorBuilder<unsafe_memory_policy>;
using ArrayBuilder  = BasicArrayBuilder<unsafe_memory_policy>;
using TableBuilder  = BasicTableBuilder<unsafe_memory_policy>;

// Thread-safe builders - use with SyncValue
using SyncMapBuilder    = BasicMapBuilder<thread_safe_memory_policy>;
using SyncVectorBuilder = BasicVectorBuilder<thread_safe_memory_policy>;
using SyncArrayBuilder  = BasicArrayBuilder<thread_safe_memory_policy>;
using SyncTableBuilder  = BasicTableBuilder<thread_safe_memory_policy>;

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
// Serialization / Deserialization
//
// Binary format for efficient memory storage and transfer.
// The format is compact and supports all Value types.
//
// Type tags (1 byte):
//   0x00 = null (monostate)
//   0x01 = int (4 bytes, little-endian)
//   0x02 = float (4 bytes, IEEE 754)
//   0x03 = double (8 bytes, IEEE 754)
//   0x04 = bool (1 byte: 0x00=false, 0x01=true)
//   0x05 = string (4-byte length + UTF-8 data)
//   0x06 = map (4-byte count + entries)
//   0x07 = vector (4-byte count + elements)
//   0x08 = array (4-byte count + elements)
//   0x09 = table (4-byte count + entries)
//   0x0A = int64 (8 bytes, little-endian)
//   0x10 = Vec2 (8 bytes, 2 floats)
//   0x11 = Vec3 (12 bytes, 3 floats)
//   0x12 = Vec4 (16 bytes, 4 floats)
//   0x13 = Mat3 (36 bytes, 9 floats)
//   0x14 = Mat4x3 (48 bytes, 12 floats)
//
// All multi-byte integers are stored in little-endian format.
// ============================================================

// Byte buffer type for serialization
using ByteBuffer = std::vector<uint8_t>;

// Serialize Value to binary buffer
// Returns: byte buffer containing serialized data
LAGER_EXT_API ByteBuffer serialize(const Value& val);

// Deserialize Value from binary buffer
// Returns: reconstructed Value, or null Value on error
// Note: throws std::runtime_error on invalid data format
LAGER_EXT_API Value deserialize(const ByteBuffer& buffer);

// Deserialize from raw pointer and size
// Useful for memory-mapped data or network buffers
LAGER_EXT_API Value deserialize(const uint8_t* data, std::size_t size);

// ============================================================
// Serialization utilities
// ============================================================

// Get serialized size without actually serializing
// Useful for pre-allocating buffers
LAGER_EXT_API std::size_t serialized_size(const Value& val);

// Serialize to pre-allocated buffer
// Returns: number of bytes written
// Note: buffer must have at least serialized_size(val) bytes
LAGER_EXT_API std::size_t serialize_to(const Value& val, uint8_t* buffer, std::size_t buffer_size);

// ============================================================
// JSON Serialization / Deserialization
//
// Provides human-readable JSON format for:
// - Configuration files
// - Network APIs
// - Debugging and logging
// - Interoperability with other systems
//
// Special handling for math types:
// - Vec2, Vec3, Vec4: JSON arrays [x, y, ...]
// - Mat3, Mat4x3: JSON arrays of floats (row-major)
//
// Note: JSON has limitations:
// - Numbers are always double precision (int64 may lose precision)
// - Binary data must be base64 encoded
// - Null, true, false are reserved keywords
// ============================================================

// Convert Value to JSON string
// compact: if false, adds indentation and newlines for readability
LAGER_EXT_API std::string to_json(const Value& val, bool compact = false);

// Parse JSON string to Value
// Returns: parsed Value, or null Value on parse error
// error_out: if provided, receives error message on failure
LAGER_EXT_API Value from_json(const std::string& json_str, std::string* error_out = nullptr);

// ============================================================
// Extern Template Declarations
//
// These declarations prevent implicit instantiation of common template
// specializations in every translation unit that includes this header.
// The actual instantiations are in value.cpp, reducing compile time
// and object file size.
//
// Covered types:
//   - BasicValue<unsafe_memory_policy>      (Value, UnsafeValue)
//   - BasicValue<thread_safe_memory_policy> (SyncValue, ThreadSafeValue)
//   - All Builder classes for both policies
//   - BasicTableEntry for both policies
// ============================================================

// Extern template for unsafe_memory_policy (single-threaded, high performance)
extern template struct BasicValue<unsafe_memory_policy>;
extern template struct BasicTableEntry<unsafe_memory_policy>;
extern template class BasicMapBuilder<unsafe_memory_policy>;
extern template class BasicVectorBuilder<unsafe_memory_policy>;
extern template class BasicArrayBuilder<unsafe_memory_policy>;
extern template class BasicTableBuilder<unsafe_memory_policy>;

// Extern template for thread_safe_memory_policy (multi-threaded safe)
extern template struct BasicValue<thread_safe_memory_policy>;
extern template struct BasicTableEntry<thread_safe_memory_policy>;
extern template class BasicMapBuilder<thread_safe_memory_policy>;
extern template class BasicVectorBuilder<thread_safe_memory_policy>;
extern template class BasicArrayBuilder<thread_safe_memory_policy>;
extern template class BasicTableBuilder<thread_safe_memory_policy>;

} // namespace lager_ext
