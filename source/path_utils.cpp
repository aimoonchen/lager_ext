// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_utils.cpp
/// @brief Implementation of path traversal engine.

#include <lager_ext/path_utils.h>

namespace lager_ext {

// ============================================================
// Anonymous namespace - Internal implementation details
// ============================================================

namespace {

/// Recursive helper for set_at_path
ImmerValue set_at_path_recursive(const ImmerValue& root, PathView path, std::size_t path_index, ImmerValue new_val) {
    if (path_index >= path.size()) {
        return new_val; // Base case: replace current node
    }

    const auto& elem = path[path_index];
    ImmerValue current_child = detail::get_at_path_element(root, elem);
    ImmerValue new_child = set_at_path_recursive(current_child, path, path_index + 1, std::move(new_val));
    return detail::set_at_path_element(root, elem, std::move(new_child));
}

} // anonymous namespace

// ============================================================
// Public API Implementation
// ============================================================

ImmerValue get_at_path(const ImmerValue& root, PathView path) {
    ImmerValue current = root;
    for (const auto& elem : path) {
        current = detail::get_at_path_element(current, elem);
        if (current.is_null()) [[unlikely]] {
            break; // Early exit on null (path errors are uncommon)
        }
    }
    return current;
}

ImmerValue set_at_path(const ImmerValue& root, PathView path, ImmerValue new_val) {
    if (path.empty()) {
        return new_val;
    }
    return set_at_path_recursive(root, path, 0, std::move(new_val));
}

// ============================================================
// Auto-Vivification Implementation (internal helpers in anonymous namespace)
// ============================================================

namespace {

/// Set value at a single path element with auto-vivification
ImmerValue set_at_path_element_vivify(const ImmerValue& current, const PathElement& elem, ImmerValue new_val) {
    if (auto* key = std::get_if<std::string_view>(&elem)) {
        // For string keys: auto-create map if null
        std::string key_str{*key};
        // Container Boxing: use BoxedValueMap
        if (auto* boxed_map = current.get_if<BoxedValueMap>()) {
            auto new_map = boxed_map->get().set(key_str, std::move(new_val));
            return ImmerValue{BoxedValueMap{std::move(new_map)}};
        }
        if (current.is_null()) {
            // Auto-vivification: create new map
            auto new_map = ValueMap{}.set(key_str, std::move(new_val));
            return ImmerValue{BoxedValueMap{std::move(new_map)}};
        }
        // Not a map and not null - cannot vivify
        return current;
    } else {
        // For index: auto-extend vector if needed
        auto idx = std::get<std::size_t>(elem);
        // Container Boxing: use BoxedValueVector
        // Use transient mode for O(N) batch push_back
        if (auto* boxed_vec = current.get_if<BoxedValueVector>()) {
            const auto& v = boxed_vec->get();
            if (idx < v.size()) {
                auto new_vec = v.set(idx, std::move(new_val));
                return ImmerValue{BoxedValueVector{std::move(new_vec)}};
            }
            auto trans = v.transient();
            while (trans.size() <= idx) {
                trans.push_back(ImmerValue{});
            }
            trans.set(idx, std::move(new_val));
            return ImmerValue{BoxedValueVector{trans.persistent()}};
        }
        if (current.is_null()) {
            auto trans = ValueVector{}.transient();
            for (std::size_t i = 0; i < idx; ++i) {
                trans.push_back(ImmerValue{});
            }
            trans.push_back(std::move(new_val));
            return ImmerValue{BoxedValueVector{trans.persistent()}};
        }
        return current;
    }
}

/// Recursive helper for set_at_path_vivify
ImmerValue set_at_path_recursive_vivify(const ImmerValue& root, PathView path, std::size_t path_index, ImmerValue new_val) {
    if (path_index >= path.size()) {
        return new_val;
    }

    const auto& elem = path[path_index];
    ImmerValue current_child = detail::get_at_path_element(root, elem);

    // If child is null and we have more path elements, prepare for vivification
    // Container Boxing: wrap in immer::box
    if (current_child.is_null() && path_index + 1 < path.size()) {
        const auto& next_elem = path[path_index + 1];
        if (std::holds_alternative<std::string_view>(next_elem)) {
            current_child = ImmerValue{BoxedValueMap{ValueMap{}}};
        } else {
            current_child = ImmerValue{BoxedValueVector{ValueVector{}}};
        }
    }

    ImmerValue new_child = set_at_path_recursive_vivify(current_child, path, path_index + 1, std::move(new_val));
    return set_at_path_element_vivify(root, elem, std::move(new_child));
}

} // anonymous namespace

ImmerValue set_at_path_vivify(const ImmerValue& root, PathView path, ImmerValue new_val) {
    if (path.empty()) {
        return new_val;
    }
    return set_at_path_recursive_vivify(root, path, 0, std::move(new_val));
}

// ============================================================
// Path Erasure Implementation
// ============================================================

ImmerValue erase_at_path(const ImmerValue& root, PathView path) {
    if (path.empty()) {
        return ImmerValue{}; // Erase entire root
    }

    // If last element is a string key, we can erase from map
    if (path.size() == 1) {
        if (auto* key = std::get_if<std::string_view>(&path[0])) {
            return detail::erase_key_from_map(root, *key);
        }
        // For index, set to null (cannot truly remove without reindexing)
        return set_at_path(root, path, ImmerValue{});
    }

    // Navigate to parent and erase from there
    Path parent_path;
    parent_path.reserve(path.size() - 1);
    for (std::size_t i = 0; i < path.size() - 1; ++i) {
        parent_path.push_back(path[i]);
    }
    const auto& last = path.back();

    if (auto* key = std::get_if<std::string_view>(&last)) {
        ImmerValue parent = get_at_path(root, parent_path);
        ImmerValue new_parent = detail::erase_key_from_map(parent, *key);
        return set_at_path(root, parent_path, new_parent);
    }

    // For index removal, set to null
    return set_at_path(root, path, ImmerValue{});
}

// ============================================================
// Path Validation Implementation
// ============================================================

bool is_valid_path(const ImmerValue& root, PathView path) {
    ImmerValue current = root;
    for (const auto& elem : path) {
        if (!detail::can_access_element(current, elem)) {
            return false;
        }
        current = detail::get_at_path_element(current, elem);
    }
    return true;
}

std::size_t valid_path_depth(const ImmerValue& root, PathView path) {
    ImmerValue current = root;
    std::size_t depth = 0;
    for (const auto& elem : path) {
        if (!detail::can_access_element(current, elem)) {
            break;
        }
        current = detail::get_at_path_element(current, elem);
        ++depth;
    }
    return depth;
}

} // namespace lager_ext