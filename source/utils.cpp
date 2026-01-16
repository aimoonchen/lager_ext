// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

#include <lager_ext/utils.h>

#include <immer/map_transient.hpp>
#include <immer/vector_transient.hpp>

namespace lager_ext {

// ============================================================
// MutableValue -> ImmerValue Conversion
// ============================================================

ImmerValue to_value(const MutableValue& mv) {
    return std::visit(
        [](const auto& val) -> ImmerValue {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                return ImmerValue{};
            } else if constexpr (std::is_same_v<T, MutableValueMapPtr>) {
                if (!val) return ImmerValue{BoxedValueMap{ValueMap{}}};
                // Container Boxing: wrap in BoxedValueMap
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueMap{}.transient();
                for (const auto& [key, child] : *val) {
                    transient.set(key, to_value(child));
                }
                return ImmerValue{BoxedValueMap{transient.persistent()}};
            } else if constexpr (std::is_same_v<T, MutableValueVectorPtr>) {
                if (!val) return ImmerValue{BoxedValueVector{ValueVector{}}};
                // Container Boxing: wrap in BoxedValueVector
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueVector{}.transient();
                for (const auto& child : *val) {
                    transient.push_back(to_value(child));
                }
                return ImmerValue{BoxedValueVector{transient.persistent()}};
            } else if constexpr (std::is_same_v<T, MutableMat3Ptr>) {
                // Unbox and convert to ImmerValue's boxed type
                if (val) {
                    return ImmerValue{*val};
                }
                return ImmerValue{};
            } else if constexpr (std::is_same_v<T, MutableMat4x3Ptr>) {
                // Unbox and convert to ImmerValue's boxed type
                if (val) {
                    return ImmerValue{*val};
                }
                return ImmerValue{};
            } else {
                // All other types (primitives, string, Vec2/3/4) can be used directly
                return ImmerValue{val};
            }
        },
        mv.data);
}

ImmerValue to_value(MutableValue&& mv) {
    return std::visit(
        [](auto&& val) -> ImmerValue {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                return ImmerValue{};
            } else if constexpr (std::is_same_v<T, std::string>) {
                // Move the string to avoid copy
                return ImmerValue{std::move(val)};
            } else if constexpr (std::is_same_v<T, MutableValueMapPtr>) {
                if (!val) return ImmerValue{BoxedValueMap{ValueMap{}}};
                // Container Boxing: wrap in BoxedValueMap
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueMap{}.transient();
                for (auto& [key, child] : *val) {
                    transient.set(key, to_value(std::move(child)));
                }
                return ImmerValue{BoxedValueMap{transient.persistent()}};
            } else if constexpr (std::is_same_v<T, MutableValueVectorPtr>) {
                if (!val) return ImmerValue{BoxedValueVector{ValueVector{}}};
                // Container Boxing: wrap in BoxedValueVector
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueVector{}.transient();
                for (auto& child : *val) {
                    transient.push_back(to_value(std::move(child)));
                }
                return ImmerValue{BoxedValueVector{transient.persistent()}};
            } else if constexpr (std::is_same_v<T, MutableMat3Ptr>) {
                if (val) {
                    return ImmerValue{*val};
                }
                return ImmerValue{};
            } else if constexpr (std::is_same_v<T, MutableMat4x3Ptr>) {
                if (val) {
                    return ImmerValue{*val};
                }
                return ImmerValue{};
            } else {
                return ImmerValue{val};
            }
        },
        std::move(mv.data));
}

// ============================================================
// ImmerValue -> MutableValue Conversion
// ============================================================

MutableValue to_mutable_value(const ImmerValue& v) {
    return std::visit(
        [](const auto& val) -> MutableValue {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                return MutableValue{};
            } else if constexpr (std::is_same_v<T, BoxedValueMap>) {
                // Container Boxing: unbox first, then convert immer::map to robin_map
                const auto& map = val.get();
                MutableValueMap result;
                result.reserve(map.size());
                // Container Boxing: map now stores ImmerValue directly, not ValueBox
                for (const auto& [key, child] : map) {
                    result.emplace(key, to_mutable_value(child));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, BoxedValueVector>) {
                // Container Boxing: unbox first, then convert immer::vector to std::vector
                const auto& vec = val.get();
                MutableValueVector result;
                result.reserve(vec.size());
                // Container Boxing: vector now stores ImmerValue directly, not ValueBox
                for (const auto& child : vec) {
                    result.push_back(to_mutable_value(child));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, BoxedValueArray>) {
                // Container Boxing: unbox first, then convert immer::array to std::vector
                const auto& arr = val.get();
                MutableValueVector result;
                result.reserve(arr.size());
                // Container Boxing: array now stores ImmerValue directly, not ValueBox
                for (const auto& child : arr) {
                    result.push_back(to_mutable_value(child));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, BoxedValueTable>) {
                // Container Boxing: unbox first, then convert immer::table to robin_map
                const auto& table = val.get();
                MutableValueMap result;
                result.reserve(table.size());
                for (const auto& entry : table) {
                    // entry is BasicTableEntry with .id and .value members
                    // Container Boxing: entry.value is now ImmerValue directly, not ValueBox
                    result.emplace(entry.id, to_mutable_value(entry.value));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, ImmerValue::boxed_mat3>) {
                // Unbox immer::box and create MutableValue with unique_ptr boxing
                return MutableValue{*val};
            } else if constexpr (std::is_same_v<T, ImmerValue::boxed_mat4x3>) {
                return MutableValue{*val};
            } else if constexpr (std::is_same_v<T, ImmerValue::boxed_mat4>) {
                // MutableValue doesn't support Mat4, convert to null
                // TODO: Add Mat4 support to MutableValue if needed
                return MutableValue{};
            } else {
                // All other types (primitives, string, Vec2/3/4) can be used directly
                return MutableValue{val};
            }
        },
        v.data);
}

} // namespace lager_ext