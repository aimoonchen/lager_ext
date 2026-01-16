// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file mutable_value.h
/// @brief Mutable dynamic value type for runtime type-erased data structures.
///
/// MutableValue provides a mutable, JSON-like data structure for scenarios where
/// immutability is not required. It supports:
/// - All common C++ numeric types (int8 to int64, uint8 to uint64, float, double)
/// - Math types: Vec2, Vec3, Vec4, Mat3, Mat4x3 (fixed-size float arrays)
/// - Strings, booleans, null
/// - Nested maps (robin_map for performance) and vectors
/// - Path-based access and modification (compatible with existing Path system)
///
/// ## Key Differences from Value (immutable)
/// - MutableValue allows in-place modification
/// - Uses direct value storage in containers (better cache locality, fewer allocations)
/// - Uses tsl::robin_map for faster map operations
/// - Designed for C++ reflection and serialization use cases
///
/// ## Performance Characteristics
/// - Container children stored directly (not via unique_ptr) for:
///   - Fewer heap allocations during construction
///   - Better cache locality during traversal
///   - Simpler memory management
/// - Containers themselves are boxed (unique_ptr) to break recursive type dependency
///
/// ## Usage Example
/// ```cpp
/// MutableValue root = MutableValue::map();
/// root.set_at_path({"user", "name"}, MutableValue{"John"});
/// root.set_at_path({"user", "age"}, MutableValue{30});
///
/// auto name = root.get_at_path({"user", "name"});
/// if (name && name->is<std::string>()) {
///     std::cout << name->as<std::string>() << std::endl;
/// }
/// ```

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/concepts.h> // for Vec2, Vec3, Vec4, Mat3, Mat4x3
#include <lager_ext/path.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tsl/robin_map.h>
#include <variant>
#include <vector>

namespace lager_ext {

// ============================================================
// Transparent Hash/Equal for robin_map heterogeneous lookup
// ============================================================

/// Transparent hash functor for string types
/// Supports: std::string, std::string_view, const char*
struct MutableValueStringHash {
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
struct MutableValueStringEqual {
    using is_transparent = void; // Enable heterogeneous lookup

    [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

// Forward declaration
struct MutableValue;

// ============================================================
// Boxed Container Types
// ============================================================
// To support direct value storage in containers while avoiding recursive
// type definition issues, we define the raw container types first, then
// box them in unique_ptr for use in the variant.
//
// This gives us:
// - Direct MutableValue storage inside containers (no per-element unique_ptr)
// - Containers boxed at variant level (breaks recursive dependency)
// - sizeof(MutableValue) reduced from ~64 bytes to ~40 bytes

/// Raw map type - stores MutableValue directly
using MutableValueMap = tsl::robin_map<std::string, MutableValue, MutableValueStringHash, MutableValueStringEqual>;

/// Raw vector type - stores MutableValue directly  
using MutableValueVector = std::vector<MutableValue>;

/// Boxed map type for use in variant (breaks recursive type dependency)
using MutableValueMapPtr = std::unique_ptr<MutableValueMap>;

/// Boxed vector type for use in variant
using MutableValueVectorPtr = std::unique_ptr<MutableValueVector>;

/// Legacy pointer type (kept for compatibility, but internal storage no longer uses this)
using MutableValuePtr = std::unique_ptr<MutableValue>;

// ============================================================
// Boxed Types for Variant Size Optimization
// ============================================================
// The variant size is determined by its largest member.
// By boxing Mat3 (36 bytes) and Mat4x3 (48 bytes) into unique_ptr (8 bytes),
// we reduce the variant size from ~64 bytes to ~40 bytes.
// This improves cache efficiency when storing many MutableValue objects.
//
// Trade-off: Extra indirection for matrix access, but matrices are typically
// accessed less frequently than basic types in JSON-like structures.

/// Boxed Mat3 type for MutableValue (36 bytes -> 8 bytes in variant)
/// Named with Mutable prefix to avoid collision with Value's BoxedMat3 (immer::box)
using MutableMat3Ptr = std::unique_ptr<Mat3>;

/// Boxed Mat4x3 type for MutableValue (48 bytes -> 8 bytes in variant)
/// Named with Mutable prefix to avoid collision with Value's BoxedMat4x3 (immer::box)
using MutableMat4x3Ptr = std::unique_ptr<Mat4x3>;

/// @brief Mutable dynamic value type supporting JSON-like structures
///
/// This type provides a mutable alternative to the immutable Value type.
/// It's designed for use cases where values need to be modified in-place,
/// such as receiving data from C++ reflection or building data structures
/// before converting to immutable Value.
struct LAGER_EXT_API MutableValue {
    /// Variant holding all possible value types
    /// Note: Mat3, Mat4x3, Map, and Vector are boxed (stored via unique_ptr)
    /// to reduce variant size to ~40 bytes and break recursive type dependency.
    using DataVariant = std::variant<std::monostate,     // null (1 byte)
                                     bool,               // 1 byte
                                     int8_t,             // 1 byte
                                     int16_t,            // 2 bytes
                                     int32_t,            // 4 bytes
                                     int64_t,            // 8 bytes
                                     uint8_t,            // 1 byte
                                     uint16_t,           // 2 bytes
                                     uint32_t,           // 4 bytes
                                     uint64_t,           // 8 bytes
                                     float,              // 4 bytes
                                     double,             // 8 bytes
                                     std::string,        // ~32 bytes (MSVC) - largest non-boxed type
                                     Vec2,               // 8 bytes
                                     Vec3,               // 12 bytes
                                     Vec4,               // 16 bytes
                                     MutableMat3Ptr,     // 8 bytes (pointer to 36-byte Mat3)
                                     MutableMat4x3Ptr,   // 8 bytes (pointer to 48-byte Mat4x3)
                                     MutableValueMapPtr, // 8 bytes (pointer to robin_map)
                                     MutableValueVectorPtr // 8 bytes (pointer to vector)
                                     >;
    // Total variant size ~ max(32) + 8 (discriminant + padding) ~ 40 bytes

    DataVariant data;

    // ============================================================
    // Constructors
    // ============================================================
    // Note: Constructors are NOT explicit to allow implicit conversions,
    // matching the API of Value class for convenient usage like:
    //   root.set("name", "John");  // const char* -> MutableValue
    //   root.set("age", 30);       // int -> MutableValue

    /// Default constructor - creates null value
    MutableValue() : data(std::monostate{}) {}

    /// Construct from bool
    MutableValue(bool v) noexcept : data(v) {}

    /// Construct from integer types
    MutableValue(int8_t v) noexcept : data(v) {}
    MutableValue(int16_t v) noexcept : data(v) {}
    MutableValue(int32_t v) noexcept : data(v) {}
    MutableValue(int64_t v) noexcept : data(v) {}
    MutableValue(uint8_t v) noexcept : data(v) {}
    MutableValue(uint16_t v) noexcept : data(v) {}
    MutableValue(uint32_t v) noexcept : data(v) {}
    MutableValue(uint64_t v) noexcept : data(v) {}

    /// Construct from floating point types
    MutableValue(float v) noexcept : data(v) {}
    MutableValue(double v) noexcept : data(v) {}

    /// Construct from string
    MutableValue(std::string v) : data(std::move(v)) {}
    MutableValue(const char* v) : data(std::string(v)) {}
    MutableValue(std::string_view v) : data(std::string(v)) {}

    /// Construct from map data (takes ownership, boxes it)
    MutableValue(MutableValueMap v) : data(std::make_unique<MutableValueMap>(std::move(v))) {}

    /// Construct from vector data (takes ownership, boxes it)
    MutableValue(MutableValueVector v) : data(std::make_unique<MutableValueVector>(std::move(v))) {}

    /// Construct from math types
    MutableValue(Vec2 v) noexcept : data(v) {}
    MutableValue(Vec3 v) noexcept : data(v) {}
    MutableValue(Vec4 v) noexcept : data(v) {}
    /// Mat3 and Mat4x3 are boxed for variant size optimization
    MutableValue(const Mat3& v) : data(std::make_unique<Mat3>(v)) {}
    MutableValue(const Mat4x3& v) : data(std::make_unique<Mat4x3>(v)) {}

    // ============================================================
    // Factory Methods (consistent with Value class naming)
    // ============================================================

    /// Create an empty map
    [[nodiscard]] static MutableValue map() { return MutableValue{MutableValueMap{}}; }

    /// Create an empty vector
    [[nodiscard]] static MutableValue vector() { return MutableValue{MutableValueVector{}}; }

    /// Create a Vec2 from individual components
    [[nodiscard]] static MutableValue vec2(float x, float y) { return MutableValue{Vec2{x, y}}; }

    /// Create a Vec3 from individual components
    [[nodiscard]] static MutableValue vec3(float x, float y, float z) { return MutableValue{Vec3{x, y, z}}; }

    /// Create a Vec4 from individual components
    [[nodiscard]] static MutableValue vec4(float x, float y, float z, float w) {
        return MutableValue{Vec4{x, y, z, w}};
    }

    /// Create a Vec2 from a float pointer (reads 2 floats)
    [[nodiscard]] static MutableValue vec2(const float* ptr) { return MutableValue{Vec2{ptr[0], ptr[1]}}; }

    /// Create a Vec3 from a float pointer (reads 3 floats)
    [[nodiscard]] static MutableValue vec3(const float* ptr) { return MutableValue{Vec3{ptr[0], ptr[1], ptr[2]}}; }

    /// Create a Vec4 from a float pointer (reads 4 floats)
    [[nodiscard]] static MutableValue vec4(const float* ptr) {
        return MutableValue{Vec4{ptr[0], ptr[1], ptr[2], ptr[3]}};
    }

    /// Create a Mat3 from a float pointer (reads 9 floats, row-major)
    [[nodiscard]] static MutableValue mat3(const float* ptr) {
        Mat3 m;
        std::copy(ptr, ptr + 9, m.begin());
        return MutableValue{m};
    }

    /// Create a Mat4x3 from a float pointer (reads 12 floats, row-major)
    [[nodiscard]] static MutableValue mat4x3(const float* ptr) {
        Mat4x3 m;
        std::copy(ptr, ptr + 12, m.begin());
        return MutableValue{m};
    }

    // ============================================================
    // Type Checking
    // ============================================================

    /// Check if value is null
    [[nodiscard]] bool is_null() const { return std::holds_alternative<std::monostate>(data); }

    /// Check if value is a specific type
    template <typename T>
    [[nodiscard]] bool is() const {
        return std::holds_alternative<T>(data);
    }

    /// Check if value is a map
    [[nodiscard]] bool is_map() const { return is<MutableValueMapPtr>(); }

    /// Check if value is a vector
    [[nodiscard]] bool is_vector() const { return is<MutableValueVectorPtr>(); }

    /// Check if value is a string
    [[nodiscard]] bool is_string() const { return is<std::string>(); }

    /// Check if value is a boolean
    [[nodiscard]] bool is_bool() const { return is<bool>(); }

    /// Check if value holds any numeric type
    [[nodiscard]] bool is_numeric() const {
        return is<int8_t>() || is<int16_t>() || is<int32_t>() || is<int64_t>() || is<uint8_t>() || is<uint16_t>() ||
               is<uint32_t>() || is<uint64_t>() || is<float>() || is<double>();
    }

    /// Check if value is a Vec2
    [[nodiscard]] bool is_vec2() const { return is<Vec2>(); }

    /// Check if value is a Vec3
    [[nodiscard]] bool is_vec3() const { return is<Vec3>(); }

    /// Check if value is a Vec4
    [[nodiscard]] bool is_vec4() const { return is<Vec4>(); }

    /// Check if value is a Mat3 (boxed)
    [[nodiscard]] bool is_mat3() const { return is<MutableMat3Ptr>(); }

    /// Check if value is a Mat4x3 (boxed)
    [[nodiscard]] bool is_mat4x3() const { return is<MutableMat4x3Ptr>(); }

    /// Check if value is any vector math type (Vec2, Vec3, Vec4)
    [[nodiscard]] bool is_vector_math() const { return is_vec2() || is_vec3() || is_vec4(); }

    /// Check if value is any matrix math type (Mat3, Mat4x3)
    [[nodiscard]] bool is_matrix_math() const { return is_mat3() || is_mat4x3(); }

    /// Check if value is any math type
    [[nodiscard]] bool is_math_type() const { return is_vector_math() || is_matrix_math(); }

    // ============================================================
    // Value Access
    // ============================================================

    /// Get value as specific type (throws if wrong type)
    template <typename T>
    [[nodiscard]] T& as() {
        return std::get<T>(data);
    }

    /// Get value as specific type (const, throws if wrong type)
    template <typename T>
    [[nodiscard]] const T& as() const {
        return std::get<T>(data);
    }

    /// Get value as specific type (returns nullptr if wrong type)
    template <typename T>
    [[nodiscard]] T* get_if() {
        return std::get_if<T>(&data);
    }

    /// Get value as specific type (const, returns nullptr if wrong type)
    template <typename T>
    [[nodiscard]] const T* get_if() const {
        return std::get_if<T>(&data);
    }

    /// Get value as specific type, or return default if wrong type
    template <typename T>
    [[nodiscard]] T get_or(T default_val = T{}) const {
        if (auto* p = std::get_if<T>(&data))
            return *p;
        return default_val;
    }

    // ============================================================
    // Special Accessor Functions (matching Value API)
    // ============================================================

    /// Get as string (copy), or return default - lvalue version
    [[nodiscard]] std::string as_string(std::string default_val = "") const& {
        if (auto* p = get_if<std::string>())
            return *p;
        return default_val;
    }

    /// Get as string (move), or return default - rvalue version
    /// Zero-copy when MutableValue is about to be destroyed
    [[nodiscard]] std::string as_string(std::string default_val = "") && {
        if (auto* p = std::get_if<std::string>(&data))
            return std::move(*p);
        return default_val;
    }

    /// Get string as string_view (zero-copy, no allocation)
    /// Returns empty string_view if not a string type
    [[nodiscard]] std::string_view as_string_view() const noexcept {
        if (auto* p = get_if<std::string>())
            return *p;
        return {};
    }

    /// Get any numeric type as double (with automatic type conversion)
    /// Matches Value::as_number() API for consistency
    [[nodiscard]] double as_number(double default_val = 0.0) const {
        if (auto* p = get_if<double>())
            return *p;
        if (auto* p = get_if<float>())
            return static_cast<double>(*p);
        if (auto* p = get_if<int64_t>())
            return static_cast<double>(*p);
        if (auto* p = get_if<int32_t>())
            return static_cast<double>(*p);
        return default_val;
    }

    /// Get as Mat3, or return default (unboxes the value)
    [[nodiscard]] Mat3 as_mat3(Mat3 default_val = {}) const {
        if (auto* p = get_if<MutableMat3Ptr>()) {
            return *p ? **p : default_val;
        }
        return default_val;
    }

    /// Get as Mat4x3, or return default (unboxes the value)
    [[nodiscard]] Mat4x3 as_mat4x3(Mat4x3 default_val = {}) const {
        if (auto* p = get_if<MutableMat4x3Ptr>()) {
            return *p ? **p : default_val;
        }
        return default_val;
    }

    // ============================================================
    // Map Operations
    // ============================================================

    /// Get map child by key (returns nullptr if not found or not a map)
    [[nodiscard]] MutableValue* get(std::string_view key);
    [[nodiscard]] const MutableValue* get(std::string_view key) const;

    /// Set map child by key (creates map if needed)
    /// Returns *this for chaining: v.set("a", 1).set("b", 2).set("c", 3);
    /// Overloads to avoid unnecessary string copies:
    /// - string_view: allocates string only if key doesn't exist
    /// - string&&: moves string if key doesn't exist (zero-copy)
    MutableValue& set(std::string_view key, MutableValue value);
    MutableValue& set(std::string&& key, MutableValue value);

    /// Convenience overloads that forward to string_view version
    MutableValue& set(const char* key, MutableValue value) { return set(std::string_view{key}, std::move(value)); }
    MutableValue& set(const std::string& key, MutableValue value) { return set(std::string_view{key}, std::move(value)); }

    /// Check if map contains key
    [[nodiscard]] bool contains(std::string_view key) const;

    /// Count occurrences of key (returns 0 or 1)
    /// Matches Value::count() API for consistency
    [[nodiscard]] std::size_t count(std::string_view key) const;

    /// Erase map key (returns true if key existed)
    bool erase(std::string_view key);

    // ============================================================
    // Vector Operations
    // ============================================================

    /// Get vector element by index (returns nullptr if out of bounds or not a vector)
    [[nodiscard]] MutableValue* get(std::size_t index);
    [[nodiscard]] const MutableValue* get(std::size_t index) const;

    /// Set vector element by index (extends vector with nulls if needed)
    void set(std::size_t index, MutableValue value);

    /// Push value to end of vector (creates vector if needed)
    void push_back(MutableValue value);

    /// Get vector size (0 if not a vector)
    [[nodiscard]] std::size_t size() const;

    // ============================================================
    // Path-based Access (compatible with Path system)
    // ============================================================

    /// Get value at path (returns nullptr if path doesn't exist)
    [[nodiscard]] MutableValue* get_at_path(PathView path);
    [[nodiscard]] const MutableValue* get_at_path(PathView path) const;

    /// Set value at path (creates intermediate maps/vectors as needed)
    void set_at_path(PathView path, MutableValue value);

    /// Erase value at path
    /// For maps: erases the key
    /// For vectors: sets to null (preserves indices)
    /// Returns true if something was erased
    bool erase_at_path(PathView path);

    /// Check if path exists and can be traversed
    [[nodiscard]] bool has_path(PathView path) const;

    // ============================================================
    // Comparison
    // ============================================================

    /// Deep equality comparison
    [[nodiscard]] bool operator==(const MutableValue& other) const;
    [[nodiscard]] bool operator!=(const MutableValue& other) const { return !(*this == other); }

    // ============================================================
    // Utility
    // ============================================================

    /// Create a deep copy
    [[nodiscard]] MutableValue clone() const;

    /// Convert to string representation (for debugging)
    [[nodiscard]] std::string to_string() const;

private:
    /// Get raw map pointer (internal helper)
    [[nodiscard]] MutableValueMap* get_map_data() {
        if (auto* p = get_if<MutableValueMapPtr>()) {
            return p->get();
        }
        return nullptr;
    }
    [[nodiscard]] const MutableValueMap* get_map_data() const {
        if (auto* p = get_if<MutableValueMapPtr>()) {
            return p->get();
        }
        return nullptr;
    }

    /// Get raw vector pointer (internal helper)
    [[nodiscard]] MutableValueVector* get_vector_data() {
        if (auto* p = get_if<MutableValueVectorPtr>()) {
            return p->get();
        }
        return nullptr;
    }
    [[nodiscard]] const MutableValueVector* get_vector_data() const {
        if (auto* p = get_if<MutableValueVectorPtr>()) {
            return p->get();
        }
        return nullptr;
    }

    /// Ensure this value is a map, creating one if needed
    MutableValueMap& ensure_map() {
        if (!is_map()) {
            data = std::make_unique<MutableValueMap>();
        }
        return *std::get<MutableValueMapPtr>(data);
    }

    /// Ensure this value is a vector, creating one if needed
    MutableValueVector& ensure_vector() {
        if (!is_vector()) {
            data = std::make_unique<MutableValueVector>();
        }
        return *std::get<MutableValueVectorPtr>(data);
    }
};

// ============================================================
// Helper Functions
// ============================================================

/// Create a MutableValuePtr from a value
[[nodiscard]] inline MutableValuePtr make_mutable_value_ptr(MutableValue value) {
    return std::make_unique<MutableValue>(std::move(value));
}

/// Deep clone a MutableValuePtr
[[nodiscard]] inline MutableValuePtr clone_mutable_value_ptr(const MutableValuePtr& ptr) {
    if (!ptr)
        return nullptr;
    return std::make_unique<MutableValue>(ptr->clone());
}

} // namespace lager_ext