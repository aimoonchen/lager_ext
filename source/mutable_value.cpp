// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

#include <lager_ext/mutable_value.h>

#include <sstream>
#include <stdexcept>

namespace lager_ext {

// ============================================================
// Map Operations
// ============================================================

MutableValue* MutableValue::get(std::string_view key)
{
    auto* map = get_if<MutableValueMap>();
    if (!map) return nullptr;

    // Heterogeneous lookup via transparent hash - no allocation
    auto it = map->find(key);
    if (it == map->end()) return nullptr;

    return it->second.get();
}

const MutableValue* MutableValue::get(std::string_view key) const
{
    const auto* map = get_if<MutableValueMap>();
    if (!map) return nullptr;

    // Heterogeneous lookup via transparent hash - no allocation
    auto it = map->find(key);
    if (it == map->end()) return nullptr;

    return it->second.get();
}

void MutableValue::set(std::string_view key, MutableValue value)
{
    // Ensure we're a map
    if (!is_map()) {
        data = MutableValueMap{};
    }

    auto& map = as<MutableValueMap>();
    // Heterogeneous lookup - no allocation if key exists
    auto it = map.find(key);
    if (it != map.end()) {
        // Key exists, just update value - no string allocation needed
        it.value() = std::make_unique<MutableValue>(std::move(value));
    } else {
        // Key doesn't exist, must allocate string for new key
        map.emplace(std::string{key}, std::make_unique<MutableValue>(std::move(value)));
    }
}

void MutableValue::set(std::string&& key, MutableValue value)
{
    // Ensure we're a map
    if (!is_map()) {
        data = MutableValueMap{};
    }

    auto& map = as<MutableValueMap>();
    // Heterogeneous lookup - no allocation if key exists
    auto it = map.find(key);
    if (it != map.end()) {
        // Key exists, just update value - no string allocation needed
        it.value() = std::make_unique<MutableValue>(std::move(value));
    } else {
        // Key doesn't exist, move the caller's string directly (zero-copy)
        map.emplace(std::move(key), std::make_unique<MutableValue>(std::move(value)));
    }
}

bool MutableValue::contains(std::string_view key) const
{
    const auto* map = get_if<MutableValueMap>();
    if (!map) return false;

    // Heterogeneous lookup - no allocation
    return map->find(key) != map->end();
}

bool MutableValue::erase(std::string_view key)
{
    auto* map = get_if<MutableValueMap>();
    if (!map) return false;

    // Heterogeneous lookup - no allocation for find
    auto it = map->find(key);
    if (it == map->end()) return false;

    map->erase(it);
    return true;
}

// ============================================================
// Vector Operations
// ============================================================

MutableValue* MutableValue::get(std::size_t index)
{
    auto* vec = get_if<MutableValueVector>();
    if (!vec || index >= vec->size()) return nullptr;

    return (*vec)[index].get();
}

const MutableValue* MutableValue::get(std::size_t index) const
{
    const auto* vec = get_if<MutableValueVector>();
    if (!vec || index >= vec->size()) return nullptr;

    return (*vec)[index].get();
}

void MutableValue::set(std::size_t index, MutableValue value)
{
    // Ensure we're a vector
    if (!is_vector()) {
        data = MutableValueVector{};
    }

    auto& vec = as<MutableValueVector>();

    // Extend vector with nulls if needed
    while (vec.size() <= index) {
        vec.push_back(std::make_unique<MutableValue>());
    }

    vec[index] = std::make_unique<MutableValue>(std::move(value));
}

void MutableValue::push_back(MutableValue value)
{
    // Ensure we're a vector
    if (!is_vector()) {
        data = MutableValueVector{};
    }

    auto& vec = as<MutableValueVector>();
    vec.push_back(std::make_unique<MutableValue>(std::move(value)));
}

std::size_t MutableValue::size() const
{
    if (const auto* vec = get_if<MutableValueVector>()) {
        return vec->size();
    }
    if (const auto* map = get_if<MutableValueMap>()) {
        return map->size();
    }
    return 0;
}

// ============================================================
// Path-based Access
// ============================================================

MutableValue* MutableValue::get_at_path(PathView path)
{
    if (path.empty()) return this;

    MutableValue* current = this;

    for (const auto& elem : path) {
        if (!current) return nullptr;

        if (auto* key = std::get_if<std::string_view>(&elem)) {
            current = current->get(*key);
        } else {
            current = current->get(std::get<std::size_t>(elem));
        }
    }

    return current;
}

const MutableValue* MutableValue::get_at_path(PathView path) const
{
    if (path.empty()) return this;

    const MutableValue* current = this;

    for (const auto& elem : path) {
        if (!current) return nullptr;

        if (auto* key = std::get_if<std::string_view>(&elem)) {
            current = current->get(*key);
        } else {
            current = current->get(std::get<std::size_t>(elem));
        }
    }

    return current;
}

void MutableValue::set_at_path(PathView path, MutableValue value)
{
    if (path.empty()) {
        *this = std::move(value);
        return;
    }

    // Navigate to parent, creating intermediate nodes as needed
    MutableValue* current = this;

    for (std::size_t i = 0; i < path.size() - 1; ++i) {
        const auto& elem = path[i];
        const auto& next_elem = path[i + 1];

        if (auto* key = std::get_if<std::string_view>(&elem)) {
            // Need a map at this level
            if (!current->is_map()) {
                current->data = MutableValueMap{};
            }

            auto& map = current->as<MutableValueMap>();
            // Heterogeneous lookup - no allocation for find
            auto it = map.find(*key);

            if (it == map.end()) {
                // Only allocate std::string when actually inserting
                std::string key_str{*key};
                // Create intermediate node based on next element type
                if (std::holds_alternative<std::string_view>(next_elem)) {
                    auto [new_it, _] = map.emplace(std::move(key_str),
                        std::make_unique<MutableValue>(MutableValueMap{}));
                    current = new_it->second.get();
                } else {
                    auto [new_it, _] = map.emplace(std::move(key_str),
                        std::make_unique<MutableValue>(MutableValueVector{}));
                    current = new_it->second.get();
                }
            } else {
                current = it->second.get();
            }
        } else {
            // Need a vector at this level
            auto idx = std::get<std::size_t>(elem);

            if (!current->is_vector()) {
                current->data = MutableValueVector{};
            }

            auto& vec = current->as<MutableValueVector>();

            // Extend with nulls if needed
            while (vec.size() <= idx) {
                vec.push_back(std::make_unique<MutableValue>());
            }

            // Initialize element based on next element type
            if (!vec[idx] || vec[idx]->is_null()) {
                if (std::holds_alternative<std::string_view>(next_elem)) {
                    vec[idx] = std::make_unique<MutableValue>(MutableValueMap{});
                } else {
                    vec[idx] = std::make_unique<MutableValue>(MutableValueVector{});
                }
            }

            current = vec[idx].get();
        }
    }

    // Set the final value
    const auto& last_elem = path.back();

    if (auto* key = std::get_if<std::string_view>(&last_elem)) {
        current->set(*key, std::move(value));
    } else {
        current->set(std::get<std::size_t>(last_elem), std::move(value));
    }
}

bool MutableValue::erase_at_path(PathView path)
{
    if (path.empty()) {
        *this = MutableValue{};
        return true;
    }

    // Navigate to parent using subpath
    PathView parent_path = path.subpath(0, path.size() - 1);
    MutableValue* parent = get_at_path(parent_path);

    if (!parent) return false;

    const auto& last_elem = path.back();

    if (auto* key = std::get_if<std::string_view>(&last_elem)) {
        return parent->erase(*key);
    } else {
        auto idx = std::get<std::size_t>(last_elem);
        if (auto* vec = parent->get_if<MutableValueVector>()) {
            if (idx < vec->size()) {
                (*vec)[idx] = std::make_unique<MutableValue>();  // Set to null
                return true;
            }
        }
        return false;
    }
}

bool MutableValue::has_path(PathView path) const
{
    return get_at_path(path) != nullptr;
}

// ============================================================
// Comparison
// ============================================================

bool MutableValue::operator==(const MutableValue& other) const
{
    if (data.index() != other.data.index()) return false;

    return std::visit([&other](const auto& val) -> bool {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, MutableValueMap>) {
            const auto& other_map = std::get<MutableValueMap>(other.data);
            if (val.size() != other_map.size()) return false;

            for (const auto& [k, v] : val) {
                auto it = other_map.find(k);
                if (it == other_map.end()) return false;
                if (!v && !it->second) continue;
                if (!v || !it->second) return false;
                if (*v != *it->second) return false;
            }
            return true;
        }
        else if constexpr (std::is_same_v<T, MutableValueVector>) {
            const auto& other_vec = std::get<MutableValueVector>(other.data);
            if (val.size() != other_vec.size()) return false;

            for (std::size_t i = 0; i < val.size(); ++i) {
                if (!val[i] && !other_vec[i]) continue;
                if (!val[i] || !other_vec[i]) return false;
                if (*val[i] != *other_vec[i]) return false;
            }
            return true;
        }
        else if constexpr (std::is_same_v<T, MutableBoxedMat3>) {
            const auto& other_box = std::get<MutableBoxedMat3>(other.data);
            if (!val && !other_box) return true;
            if (!val || !other_box) return false;
            return *val == *other_box;
        }
        else if constexpr (std::is_same_v<T, MutableBoxedMat4x3>) {
            const auto& other_box = std::get<MutableBoxedMat4x3>(other.data);
            if (!val && !other_box) return true;
            if (!val || !other_box) return false;
            return *val == *other_box;
        }
        else {
            return val == std::get<T>(other.data);
        }
    }, data);
}

// ============================================================
// Utility
// ============================================================

MutableValue MutableValue::clone() const
{
    return std::visit([](const auto& val) -> MutableValue {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, MutableValueMap>) {
            MutableValueMap new_map;
            for (const auto& [k, v] : val) {
                if (v) {
                    new_map[k] = std::make_unique<MutableValue>(v->clone());
                } else {
                    new_map[k] = nullptr;
                }
            }
            return MutableValue{std::move(new_map)};
        }
        else if constexpr (std::is_same_v<T, MutableValueVector>) {
            MutableValueVector new_vec;
            new_vec.reserve(val.size());
            for (const auto& v : val) {
                if (v) {
                    new_vec.push_back(std::make_unique<MutableValue>(v->clone()));
                } else {
                    new_vec.push_back(nullptr);
                }
            }
            return MutableValue{std::move(new_vec)};
        }
        else if constexpr (std::is_same_v<T, MutableBoxedMat3>) {
            // Clone boxed Mat3 - dereference and re-box
            if (val) {
                return MutableValue{*val};
            }
            // Edge case: null boxed value (shouldn't happen, but handle gracefully)
            MutableValue result;
            result.data = MutableBoxedMat3{nullptr};
            return result;
        }
        else if constexpr (std::is_same_v<T, MutableBoxedMat4x3>) {
            // Clone boxed Mat4x3 - dereference and re-box
            if (val) {
                return MutableValue{*val};
            }
            // Edge case: null boxed value (shouldn't happen, but handle gracefully)
            MutableValue result;
            result.data = MutableBoxedMat4x3{nullptr};
            return result;
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            return MutableValue{};
        }
        else {
            return MutableValue{val};
        }
    }, data);
}

std::string MutableValue::to_string() const
{
    return std::visit([](const auto& val) -> std::string {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return "null";
        }
        else if constexpr (std::is_same_v<T, bool>) {
            return val ? "true" : "false";
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + val + "\"";
        }
        else if constexpr (std::is_same_v<T, Vec2>) {
            std::ostringstream oss;
            oss << "[" << val[0] << ", " << val[1] << "]";
            return oss.str();
        }
        else if constexpr (std::is_same_v<T, Vec3>) {
            std::ostringstream oss;
            oss << "[" << val[0] << ", " << val[1] << ", " << val[2] << "]";
            return oss.str();
        }
        else if constexpr (std::is_same_v<T, Vec4>) {
            std::ostringstream oss;
            oss << "[" << val[0] << ", " << val[1] << ", " << val[2] << ", " << val[3] << "]";
            return oss.str();
        }
        else if constexpr (std::is_same_v<T, MutableBoxedMat3>) {
            if (!val) return "null";
            std::ostringstream oss;
            oss << "[";
            for (std::size_t i = 0; i < 9; ++i) {
                if (i > 0) oss << ", ";
                oss << (*val)[i];
            }
            oss << "]";
            return oss.str();
        }
        else if constexpr (std::is_same_v<T, MutableBoxedMat4x3>) {
            if (!val) return "null";
            std::ostringstream oss;
            oss << "[";
            for (std::size_t i = 0; i < 12; ++i) {
                if (i > 0) oss << ", ";
                oss << (*val)[i];
            }
            oss << "]";
            return oss.str();
        }
        else if constexpr (std::is_same_v<T, MutableValueMap>) {
            std::ostringstream oss;
            oss << "{";
            bool first = true;
            for (const auto& [k, v] : val) {
                if (!first) oss << ", ";
                first = false;
                oss << "\"" << k << "\": ";
                if (v) {
                    oss << v->to_string();
                } else {
                    oss << "null";
                }
            }
            oss << "}";
            return oss.str();
        }
        else if constexpr (std::is_same_v<T, MutableValueVector>) {
            std::ostringstream oss;
            oss << "[";
            bool first = true;
            for (const auto& v : val) {
                if (!first) oss << ", ";
                first = false;
                if (v) {
                    oss << v->to_string();
                } else {
                    oss << "null";
                }
            }
            oss << "]";
            return oss.str();
        }
        else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(val);
        }
        else {
            return "<unknown>";
        }
    }, data);
}

} // namespace lager_ext
