// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file static_path.h
/// @brief Compile-time static path lens implementation (C++20).
///
/// This file provides a zero-overhead abstraction for path-based lens access
/// when the data structure is known at compile time (e.g., from C++ reflection).
///
/// Key features:
/// - Paths are constructed at compile time
/// - Lens composition is resolved at compile time
/// - Zero runtime overhead for path construction
/// - Type-safe path definitions
/// - JSON Pointer style path syntax support

#pragma once

#include <lager_ext/value.h>
#include <lager/lenses.hpp>
#include <zug/compose.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace lager_ext {
namespace static_path {

// ============================================================
// Fixed-size string for non-type template parameters (C++20 NTTP)
// ============================================================

template<std::size_t N>
struct FixedString {
    char data[N]{};

    constexpr FixedString() = default;

    constexpr FixedString(const char (&str)[N]) {
        std::copy_n(str, N, data);
    }

    constexpr std::size_t size() const noexcept { return N - 1; }
    constexpr const char* c_str() const noexcept { return data; }
    constexpr std::string_view view() const noexcept { return {data, N - 1}; }
    constexpr operator std::string_view() const noexcept { return view(); }
    std::string to_string() const { return std::string{view()}; }

    // Comparison operators for compile-time path comparison
    template<std::size_t M>
    constexpr bool operator==(const FixedString<M>& other) const noexcept {
        return view() == other.view();
    }

    constexpr bool operator==(std::string_view sv) const noexcept {
        return view() == sv;
    }
};

// Deduction guide
template<std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

// ============================================================
// Compile-time path segment types
// ============================================================

// Key segment - for map access
template<FixedString Key>
struct KeySeg {
    static constexpr auto key = Key;
    static constexpr bool is_key = true;
    static constexpr bool is_index = false;

    static std::string key_string() { return Key.to_string(); }
};

// Index segment - for vector access
template<std::size_t Index>
struct IndexSeg {
    static constexpr std::size_t index = Index;
    static constexpr bool is_key = false;
    static constexpr bool is_index = true;
};

// ============================================================
// Static Lens - Compile-time lens for a single path segment
// ============================================================

// Key lens - accesses a map by key at compile time
template<FixedString Key>
struct StaticKeyLens {
    static constexpr auto key = Key;

    Value get(const Value& whole) const {
        return whole.at(key.to_string());
    }

    Value set(Value whole, Value part) const {
        return whole.set(key.to_string(), std::move(part));
    }
};

// Index lens - accesses a vector by index at compile time
template<std::size_t Index>
struct StaticIndexLens {
    static constexpr std::size_t index = Index;

    Value get(const Value& whole) const {
        return whole.at(index);
    }

    Value set(Value whole, Value part) const {
        return whole.set(index, std::move(part));
    }
};

// ============================================================
// Composed Static Lens - Zero-overhead lens composition using fold expression
// ============================================================

/// @brief Composed lens using C++17 fold expressions for cleaner implementation
/// 
/// This implementation replaces the recursive template approach with fold expressions,
/// resulting in cleaner code and potentially better compile-time performance.
template<typename... Lenses>
struct ComposedLens {
    std::tuple<Lenses...> lenses;

    constexpr ComposedLens() = default;
    constexpr explicit ComposedLens(Lenses... ls) : lenses(std::move(ls)...) {}

    /// Get: Apply lenses left-to-right (lens0.get → lens1.get → ...)
    Value get(const Value& v) const {
        return get_impl(v, std::index_sequence_for<Lenses...>{});
    }

    /// Set: Apply lenses right-to-left for the setter
    Value set(Value v, const Value& x) const {
        return set_impl(std::move(v), x, std::index_sequence_for<Lenses...>{});
    }

private:
    template<std::size_t... Is>
    Value get_impl(const Value& v, std::index_sequence<Is...>) const {
        if constexpr (sizeof...(Is) == 0) {
            return v;
        } else {
            Value result = v;
            // Fold expression: apply each lens's get in sequence
            ((result = std::get<Is>(lenses).get(result)), ...);
            return result;
        }
    }

    template<std::size_t... Is>
    Value set_impl(Value v, const Value& x, std::index_sequence<Is...>) const {
        if constexpr (sizeof...(Is) == 0) {
            return x;
        } else {
            return set_recursive<0>(std::move(v), x, std::index_sequence<Is...>{});
        }
    }

    // Recursive set: need to get intermediate values, then set back
    template<std::size_t Current, std::size_t First, std::size_t... Rest>
    Value set_recursive(Value v, const Value& x, std::index_sequence<First, Rest...>) const {
        if constexpr (sizeof...(Rest) == 0) {
            // Base case: last lens
            return std::get<Current>(lenses).set(std::move(v), x);
        } else {
            // Get the intermediate value
            auto inner = std::get<Current>(lenses).get(v);
            // Recursively set in the inner value
            auto new_inner = set_recursive<Current + 1>(std::move(inner), x, std::index_sequence<Rest...>{});
            // Set back to the current level
            return std::get<Current>(lenses).set(std::move(v), new_inner);
        }
    }
};

// Empty case (identity lens) - specialization for zero lenses
template<>
struct ComposedLens<> {
    std::tuple<> lenses;

    constexpr ComposedLens() = default;

    Value get(const Value& v) const {
        return v;
    }

    Value set(Value, const Value& x) const {
        return x;
    }
};

// ============================================================
// Static Path - Compile-time path definition
// ============================================================

template<typename... Segments>
struct StaticPath {
    static constexpr std::size_t depth = sizeof...(Segments);

    // Convert to composed lens at compile time
    static auto to_lens() {
        return make_lens_impl(std::index_sequence_for<Segments...>{});
    }

    // Get value using this path
    static Value get(const Value& v) {
        return to_lens().get(v);
    }

    // Set value using this path
    static Value set(Value v, const Value& x) {
        return to_lens().set(std::move(v), x);
    }

    // Convert to runtime Path for compatibility
    static Path to_runtime_path() {
        Path result;
        (add_segment<Segments>(result), ...);
        return result;
    }

private:
    template<typename Seg>
    static void add_segment(Path& path) {
        if constexpr (Seg::is_key) {
            path.push_back(Seg::key_string());
        } else {
            path.push_back(Seg::index);
        }
    }

    template<std::size_t... Is>
    static auto make_lens_impl(std::index_sequence<Is...>) {
        return ComposedLens<decltype(segment_to_lens<Segments>())...>{
            segment_to_lens<Segments>()...
        };
    }

    template<typename Seg>
    static auto segment_to_lens() {
        if constexpr (Seg::is_key) {
            return StaticKeyLens<Seg::key>{};
        } else {
            return StaticIndexLens<Seg::index>{};
        }
    }
};

// Empty path specialization (identity lens)
template<>
struct StaticPath<> {
    static constexpr std::size_t depth = 0;

    static Value get(const Value& v) { return v; }
    static Value set(Value, const Value& x) { return x; }
    static Path to_runtime_path() { return {}; }
};

// ============================================================
// Convenience type aliases
// ============================================================

/// @brief Key segment shorthand for map access
/// @example K<"name">, K<"users">
template<FixedString S>
using K = KeySeg<S>;

/// @brief Index segment shorthand for vector/array access  
/// @example I<0>, I<1>
template<std::size_t N>
using I = IndexSeg<N>;

// ============================================================
// Path concatenation - combine two static paths
// ============================================================

template<typename Path1, typename Path2>
struct ConcatPath;

template<typename... Segs1, typename... Segs2>
struct ConcatPath<StaticPath<Segs1...>, StaticPath<Segs2...>> {
    using type = StaticPath<Segs1..., Segs2...>;
};

template<typename Path1, typename Path2>
using ConcatPathT = typename ConcatPath<Path1, Path2>::type;

// ============================================================
// Path extension - add a segment to an existing path
// ============================================================

template<typename BasePath, typename Segment>
struct ExtendPath;

template<typename... Segs, typename Segment>
struct ExtendPath<StaticPath<Segs...>, Segment> {
    using type = StaticPath<Segs..., Segment>;
};

template<typename BasePath, typename Segment>
using ExtendPathT = typename ExtendPath<BasePath, Segment>::type;

// ============================================================
// Macro helpers for defining paths
// ============================================================

// Define a key segment
#define STATIC_KEY(name) \
    lager_ext::static_path::KeySeg<lager_ext::static_path::FixedString{name}>

// Define an index segment
#define STATIC_IDX(n) \
    lager_ext::static_path::IndexSeg<n>

// Define a complete path
#define STATIC_PATH(...) \
    lager_ext::static_path::StaticPath<__VA_ARGS__>

// ============================================================
// PathRegistry - For organizing paths by schema
// ============================================================

template<typename Schema>
struct PathRegistry {
    // Override this in derived types to define paths
};

// ============================================================
// String Literal Static Path
//
// Allows defining paths using string literal syntax:
//   StaticPath<"/users/0/name">
//
// This is equivalent to:
//   StaticPath<K<"users">, I<0>, K<"name">>
// ============================================================

namespace detail {

// Helper: check if string_view contains only digits
constexpr bool is_number(std::string_view sv) noexcept {
    if (sv.empty()) return false;
    for (char c : sv) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// Helper: convert string_view to number
constexpr std::size_t to_number(std::string_view sv) noexcept {
    std::size_t result = 0;
    for (char c : sv) {
        result = result * 10 + (c - '0');
    }
    return result;
}

// Count segments in a JSON Pointer
template<FixedString Ptr>
constexpr std::size_t count_segments() {
    std::string_view sv = Ptr.view();
    if (sv.empty()) return 0;

    std::size_t start = (sv[0] == '/') ? 1 : 0;
    if (start >= sv.size()) return 0;

    std::size_t count = 0;
    while (start < sv.size()) {
        ++count;
        auto next = sv.find('/', start);
        if (next == std::string_view::npos) break;
        start = next + 1;
    }
    return count;
}

// Extract segment at index as string_view
template<FixedString Ptr, std::size_t Index>
constexpr std::string_view get_segment() {
    std::string_view sv = Ptr.view();
    std::size_t start = (sv[0] == '/') ? 1 : 0;

    std::size_t current = 0;
    while (current < Index) {
        auto next = sv.find('/', start);
        start = next + 1;
        ++current;
    }

    auto end = sv.find('/', start);
    if (end == std::string_view::npos) end = sv.size();

    return sv.substr(start, end - start);
}

// Check if segment is a number
template<FixedString Ptr, std::size_t Index>
constexpr bool is_index_segment() {
    return is_number(get_segment<Ptr, Index>());
}

// Get segment as number
template<FixedString Ptr, std::size_t Index>
constexpr std::size_t get_index_value() {
    return to_number(get_segment<Ptr, Index>());
}

// Get segment length at compile time
template<FixedString Ptr, std::size_t Index>
constexpr std::size_t get_segment_length() {
    return get_segment<Ptr, Index>().size();
}

// Build FixedString from segment
template<FixedString Ptr, std::size_t Index, std::size_t Len>
struct SegmentString {
    static constexpr auto make() {
        constexpr auto seg = get_segment<Ptr, Index>();
        FixedString<Len + 1> result{};
        for (std::size_t i = 0; i < Len; ++i) {
            result.data[i] = seg[i];
        }
        result.data[Len] = '\0';
        return result;
    }
    static constexpr auto value = make();
};

template<FixedString Ptr, std::size_t Index>
constexpr auto make_segment_string() {
    constexpr std::size_t len = get_segment_length<Ptr, Index>();
    return SegmentString<Ptr, Index, len>::value;
}

// Build segment type at index
template<FixedString Ptr, std::size_t Index>
struct SegmentTypeAt {
    static constexpr bool is_idx = is_index_segment<Ptr, Index>();

    using type = std::conditional_t<
        is_idx,
        IndexSeg<get_index_value<Ptr, Index>()>,
        KeySeg<make_segment_string<Ptr, Index>()>
    >;
};

// Build StaticPath from segments
template<FixedString Ptr, typename Indices>
struct BuildPath;

template<FixedString Ptr, std::size_t... Is>
struct BuildPath<Ptr, std::index_sequence<Is...>> {
    using type = StaticPath<typename SegmentTypeAt<Ptr, Is>::type...>;
};

} // namespace detail

/// @brief Convert string literal to StaticPath at compile time
/// @tparam Ptr JSON Pointer style path string (e.g., "/users/0/name")
/// 
/// Usage: LiteralPath<"/users/0/name">
/// Equivalent to: StaticPath<K<"users">, I<0>, K<"name">>
/// 
/// @example
/// using UserNamePath = LiteralPath<"/users/0/name">;
/// Value name = UserNamePath::get(root);
template<FixedString Ptr>
using LiteralPath = typename detail::BuildPath<
    Ptr,
    std::make_index_sequence<detail::count_segments<Ptr>()>
>::type;

} // namespace static_path

// ============================================================
// Promote commonly used types to lager_ext namespace
// Users can use lager_ext::LiteralPath<"/path"> directly
// ============================================================

using static_path::FixedString;
using static_path::LiteralPath;
using static_path::StaticPath;
using static_path::K;
using static_path::I;
using static_path::ExtendPathT;
using static_path::ConcatPathT;

} // namespace lager_ext
