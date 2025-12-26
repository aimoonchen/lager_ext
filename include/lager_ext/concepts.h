// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file concepts.h
/// @brief C++20 Concepts for type constraints in lager_ext.
///
/// This file provides concepts for compile-time type checking, enabling:
/// - Better error messages when template constraints are violated
/// - IDE auto-completion improvements
/// - Documentation of type requirements
///
/// @note Requires C++20 or later.

#pragma once

#include <array>
#include <concepts>
#include <string>
#include <type_traits>
#include <variant>

namespace lager_ext {

// ============================================================
// Primitive Type Concepts
// ============================================================

/// Concept for types that can be directly stored as primitive values
/// Integer types: int32_t, int64_t (signed), uint32_t, uint64_t (unsigned)
/// Floating-point: float, double
/// Boolean: bool
template<typename T>
concept PrimitiveType = std::is_same_v<std::decay_t<T>, int32_t> ||
                        std::is_same_v<std::decay_t<T>, int64_t> ||
                        std::is_same_v<std::decay_t<T>, uint32_t> ||
                        std::is_same_v<std::decay_t<T>, uint64_t> ||
                        std::is_same_v<std::decay_t<T>, float> ||
                        std::is_same_v<std::decay_t<T>, double> ||
                        std::is_same_v<std::decay_t<T>, bool>;

/// Concept for string-like types that can be converted to std::string
template<typename T>
concept StringLike = std::is_same_v<std::decay_t<T>, std::string> ||
                     std::is_same_v<std::decay_t<T>, const char*> ||
                     std::is_convertible_v<T, std::string_view>;

// ============================================================
// Math Type Concepts
// ============================================================

// Forward declarations for math types
using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Mat3 = std::array<float, 9>;
using Mat4x3 = std::array<float, 12>;

/// Concept for vector math types (Vec2, Vec3, Vec4)
template<typename T>
concept VectorMathType = std::is_same_v<std::decay_t<T>, Vec2> ||
                         std::is_same_v<std::decay_t<T>, Vec3> ||
                         std::is_same_v<std::decay_t<T>, Vec4>;

/// Concept for matrix math types (Mat3, Mat4x3)
template<typename T>
concept MatrixMathType = std::is_same_v<std::decay_t<T>, Mat3> ||
                         std::is_same_v<std::decay_t<T>, Mat4x3>;

/// Concept for all math types (vectors and matrices)
template<typename T>
concept MathType = VectorMathType<T> || MatrixMathType<T>;

/// Concept for small math types that fit in registers (pass by value)
template<typename T>
concept SmallMathType = VectorMathType<T>;

/// Concept for large math types that should be passed by const reference
template<typename T>
concept LargeMathType = MatrixMathType<T>;

// ============================================================
// Value Construction Concepts
// ============================================================

/// Concept for types that can be used to construct a BasicValue
/// This includes all primitive types, string types, and math types
template<typename T>
concept ValueConstructible = PrimitiveType<T> || StringLike<T> || MathType<T>;

/// Concept for types that can be used as container keys
template<typename T>
concept KeyType = std::is_same_v<std::decay_t<T>, std::string> ||
                  std::is_convertible_v<T, std::string>;

/// Concept for types that can be used as container indices
template<typename T>
concept IndexType = std::is_integral_v<std::decay_t<T>> &&
                    !std::is_same_v<std::decay_t<T>, bool>;

/// Concept for path element types (either key or index)
template<typename T>
concept PathElementType = KeyType<T> || IndexType<T>;

// ============================================================
// Callable Concepts
// ============================================================

/// Concept for update functions that transform a value
template<typename Fn, typename ValueType>
concept ValueTransformer = std::invocable<Fn, ValueType> &&
                           std::convertible_to<std::invoke_result_t<Fn, ValueType>, ValueType>;

/// Concept for predicate functions on values
template<typename Fn, typename ValueType>
concept ValuePredicate = std::invocable<Fn, const ValueType&> &&
                         std::convertible_to<std::invoke_result_t<Fn, const ValueType&>, bool>;

// ============================================================
// Container Concepts
// ============================================================

/// Concept for immer-like containers with size() method
template<typename T>
concept SizedContainer = requires(const T& t) {
    { t.size() } -> std::convertible_to<std::size_t>;
};

/// Concept for map-like containers with key lookup
template<typename T, typename Key = std::string>
concept MapLike = SizedContainer<T> && requires(const T& t, const Key& k) {
    { t.count(k) } -> std::convertible_to<std::size_t>;
    { t.find(k) };
};

/// Concept for sequence-like containers with index access
template<typename T>
concept SequenceLike = SizedContainer<T> && requires(const T& t, std::size_t i) {
    { t[i] };
};

// ============================================================
// Memory Policy Concepts
// ============================================================

/// Concept for types that look like immer memory policies
/// (This is a simplified check; immer doesn't expose a formal concept)
template<typename T>
concept MemoryPolicyLike = requires {
    typename T::heap;
    typename T::refcount;
};

// ============================================================
// Serialization Concepts
// ============================================================

/// Concept for types that can be serialized to bytes
template<typename T>
concept ByteSerializable = std::is_trivially_copyable_v<T>;

/// Concept for buffer-like types for deserialization
/// @note Named ByteBufferLike to avoid conflict with ByteBuffer type alias
template<typename T>
concept ByteBufferLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const uint8_t*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

} // namespace lager_ext
