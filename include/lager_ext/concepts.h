// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file concepts.h
/// @brief C++20 Concepts for lager_ext type constraints.
///
/// This file defines concept constraints for ImmerValue types, enabling
/// compile-time type checking and better error messages.

#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace lager_ext {

// ============================================================
// Math Type Aliases (Row-major matrices)
// ============================================================

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Mat3 = std::array<float, 9>;
using Mat4x3 = std::array<float, 12>;
using Mat4 = std::array<float, 16>;

// ============================================================
// Primitive Type Concepts
// ============================================================

/// @brief Concept for primitive numeric and boolean types supported by ImmerValue
template <typename T>
concept PrimitiveType = std::is_same_v<std::decay_t<T>, int8_t> || std::is_same_v<std::decay_t<T>, int16_t> ||
                        std::is_same_v<std::decay_t<T>, int32_t> || std::is_same_v<std::decay_t<T>, int64_t> ||
                        std::is_same_v<std::decay_t<T>, uint8_t> || std::is_same_v<std::decay_t<T>, uint16_t> ||
                        std::is_same_v<std::decay_t<T>, uint32_t> || std::is_same_v<std::decay_t<T>, uint64_t> ||
                        std::is_same_v<std::decay_t<T>, float> || std::is_same_v<std::decay_t<T>, double> ||
                        std::is_same_v<std::decay_t<T>, bool>;

/// @brief Concept for string-like types
template <typename T>
concept StringLike = std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, const char*> ||
                     std::is_convertible_v<T, std::string_view>;

template <typename T>
concept VectorMathType = std::is_same_v<std::decay_t<T>, Vec2> || std::is_same_v<std::decay_t<T>, Vec3> ||
                         std::is_same_v<std::decay_t<T>, Vec4>;

template <typename T>
concept MatrixMathType = std::is_same_v<std::decay_t<T>, Mat3> || std::is_same_v<std::decay_t<T>, Mat4x3> ||
                         std::is_same_v<std::decay_t<T>, Mat4>;

template <typename T>
concept MathType = VectorMathType<T> || MatrixMathType<T>;

template <typename T>
concept ValueConstructible = PrimitiveType<T> || StringLike<T> || MathType<T>;

template <typename T>
concept KeyType = std::is_same_v<std::decay_t<T>, std::string> || std::is_convertible_v<T, std::string>;

template <typename T>
concept IndexType = std::is_integral_v<std::decay_t<T>> && !std::is_same_v<std::decay_t<T>, bool>;

template <typename T>
concept PathElementType = KeyType<T> || IndexType<T>;

template <typename Fn, typename ValueType>
concept ValueTransformer =
    std::invocable<Fn, ValueType> && std::convertible_to<std::invoke_result_t<Fn, ValueType>, ValueType>;

template <typename Fn, typename ValueType>
concept ValuePredicate =
    std::invocable<Fn, const ValueType&> && std::convertible_to<std::invoke_result_t<Fn, const ValueType&>, bool>;

template <typename T>
concept SizedContainer = requires(const T& t) {
    { t.size() } -> std::convertible_to<std::size_t>;
};

template <typename T, typename Key = std::string>
concept MapLike = SizedContainer<T> && requires(const T& t, const Key& k) {
    { t.count(k) } -> std::convertible_to<std::size_t>;
    { t.find(k) };
};

template <typename T>
concept SequenceLike = SizedContainer<T> && requires(const T& t, std::size_t i) {
    { t[i] };
};

template <typename T>
concept MemoryPolicyLike = requires {
    typename T::heap;
    typename T::refcount;
};

template <typename T>
concept ByteSerializable = std::is_trivially_copyable_v<T>;

template <typename T>
concept ByteBufferLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const uint8_t*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

} // namespace lager_ext
