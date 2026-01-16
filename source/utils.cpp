// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

#include <lager_ext/utils.h>

#include <immer/map_transient.hpp>
#include <immer/vector_transient.hpp>

namespace lager_ext {

// ============================================================
// MutableValue -> Value Conversion
// ============================================================

Value to_value(const MutableValue& mv) {
    return std::visit(
        [](const auto& val) -> Value {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                return Value{};
            } else if constexpr (std::is_same_v<T, MutableValueMapPtr>) {
                if (!val) return Value{ValueMap{}};
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueMap{}.transient();
                for (const auto& [key, child] : *val) {
                    transient.set(key, ValueBox{to_value(child)});
                }
                return Value{transient.persistent()};
            } else if constexpr (std::is_same_v<T, MutableValueVectorPtr>) {
                if (!val) return Value{ValueVector{}};
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueVector{}.transient();
                for (const auto& child : *val) {
                    transient.push_back(ValueBox{to_value(child)});
                }
                return Value{transient.persistent()};
            } else if constexpr (std::is_same_v<T, MutableMat3Ptr>) {
                // Unbox and convert to Value's boxed type
                if (val) {
                    return Value{*val};
                }
                return Value{};
            } else if constexpr (std::is_same_v<T, MutableMat4x3Ptr>) {
                // Unbox and convert to Value's boxed type
                if (val) {
                    return Value{*val};
                }
                return Value{};
            } else {
                // All other types (primitives, string, Vec2/3/4) can be used directly
                return Value{val};
            }
        },
        mv.data);
}

Value to_value(MutableValue&& mv) {
    return std::visit(
        [](auto&& val) -> Value {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                return Value{};
            } else if constexpr (std::is_same_v<T, std::string>) {
                // Move the string to avoid copy
                return Value{std::move(val)};
            } else if constexpr (std::is_same_v<T, MutableValueMapPtr>) {
                if (!val) return Value{ValueMap{}};
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueMap{}.transient();
                for (auto& [key, child] : *val) {
                    transient.set(key, ValueBox{to_value(std::move(child))});
                }
                return Value{transient.persistent()};
            } else if constexpr (std::is_same_v<T, MutableValueVectorPtr>) {
                if (!val) return Value{ValueVector{}};
                // Use transient for O(n) batch construction instead of O(n log n)
                auto transient = ValueVector{}.transient();
                for (auto& child : *val) {
                    transient.push_back(ValueBox{to_value(std::move(child))});
                }
                return Value{transient.persistent()};
            } else if constexpr (std::is_same_v<T, MutableMat3Ptr>) {
                if (val) {
                    return Value{*val};
                }
                return Value{};
            } else if constexpr (std::is_same_v<T, MutableMat4x3Ptr>) {
                if (val) {
                    return Value{*val};
                }
                return Value{};
            } else {
                return Value{val};
            }
        },
        std::move(mv.data));
}

// ============================================================
// Value -> MutableValue Conversion
// ============================================================

MutableValue to_mutable_value(const Value& v) {
    return std::visit(
        [](const auto& val) -> MutableValue {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                return MutableValue{};
            } else if constexpr (std::is_same_v<T, ValueMap>) {
                // Convert immer::map to robin_map
                // Note: robin_map doesn't have transient, but reserve() helps
                MutableValueMap result;
                result.reserve(val.size());
                for (const auto& [key, child_box] : val) {
                    result.emplace(key, to_mutable_value(*child_box));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, ValueVector>) {
                // Convert immer::vector to std::vector
                MutableValueVector result;
                result.reserve(val.size());
                for (const auto& child_box : val) {
                    result.push_back(to_mutable_value(*child_box));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, ValueArray>) {
                // Convert immer::array to std::vector
                MutableValueVector result;
                result.reserve(val.size());
                for (const auto& child_box : val) {
                    result.push_back(to_mutable_value(*child_box));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, ValueTable>) {
                // Convert immer::table to robin_map using the entry's id as key
                // Note: Table entries have (id, value) structure
                MutableValueMap result;
                result.reserve(val.size());
                for (const auto& entry : val) {
                    // entry is BasicTableEntry with .id and .value members
                    result.emplace(entry.id, to_mutable_value(*entry.value));
                }
                return MutableValue{std::move(result)};
            } else if constexpr (std::is_same_v<T, Value::boxed_mat3>) {
                // Unbox immer::box and create MutableValue with unique_ptr boxing
                return MutableValue{*val};
            } else if constexpr (std::is_same_v<T, Value::boxed_mat4x3>) {
                return MutableValue{*val};
            } else if constexpr (std::is_same_v<T, Value::boxed_mat4>) {
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