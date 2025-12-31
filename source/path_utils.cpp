// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_utils.cpp
/// @brief Implementation of complex path traversal utilities.

#include <lager_ext/path_utils.h>

namespace lager_ext {

// ============================================================
// Direct Path Traversal Implementation
// ============================================================

Value set_at_path_recursive(
    const Value& root,
    const Path& path,
    std::size_t path_index,
    Value new_val)
{
    if (path_index >= path.size()) {
        return new_val;  // Base case: replace current node
    }

    const auto& elem = path[path_index];
    Value current_child = get_at_path_element(root, elem);
    Value new_child = set_at_path_recursive(current_child, path, path_index + 1, std::move(new_val));
    return set_at_path_element(root, elem, std::move(new_child));
}

Value set_at_path_direct(const Value& root, const Path& path, Value new_val)
{
    if (path.empty()) {
        return new_val;
    }
    return set_at_path_recursive(root, path, 0, std::move(new_val));
}

// ============================================================
// Auto-Vivification Path Traversal Implementation
// ============================================================

Value set_at_path_element_vivify(const Value& current, const PathElement& elem, Value new_val)
{
    // Optimized: uses if-else instead of std::visit to avoid indirect call overhead
    if (auto* key = std::get_if<std::string>(&elem)) {
        // For string keys: auto-create map if null
        if (auto* m = current.get_if<ValueMap>()) {
            return m->set(*key, immer::box<Value>{std::move(new_val)});
        }
        if (current.is_null()) {
            // Auto-vivification: create new map
            return ValueMap{}.set(*key, immer::box<Value>{std::move(new_val)});
        }
        // Not a map and not null - cannot vivify
        return current;
    } else {
        // For index: auto-extend vector if needed
        auto idx = std::get<std::size_t>(elem);
        // OPTIMIZATION: Use transient mode for O(N) batch push_back
        // (immutable push_back in loop would be O(N log N))
        if (auto* v = current.get_if<ValueVector>()) {
            if (idx < v->size()) {
                // Index within bounds, just set
                return v->set(idx, immer::box<Value>{std::move(new_val)});
            }
            // Need to extend: use transient for batch operations
            auto trans = v->transient();
            while (trans.size() <= idx) {
                trans.push_back(immer::box<Value>{});
            }
            trans.set(idx, immer::box<Value>{std::move(new_val)});
            return trans.persistent();
        }
        if (current.is_null()) {
            // Auto-vivification: create new vector with enough space
            // Use transient for O(N) construction
            auto trans = ValueVector{}.transient();
            for (std::size_t i = 0; i < idx; ++i) {
                trans.push_back(immer::box<Value>{});
            }
            trans.push_back(immer::box<Value>{std::move(new_val)});
            return trans.persistent();
        }
        return current;
    }
}

Value set_at_path_recursive_vivify(
    const Value& root,
    const Path& path,
    std::size_t path_index,
    Value new_val)
{
    if (path_index >= path.size()) {
        return new_val;  // Base case: replace current node
    }

    const auto& elem = path[path_index];
    
    // Get current child, or create appropriate container if null
    Value current_child = get_at_path_element(root, elem);
    
    // If child is null and we have more path elements, prepare for vivification
    // The actual container creation happens in set_at_path_element_vivify
    if (current_child.is_null() && path_index + 1 < path.size()) {
        // Check if next path element is string (need map) or index (need vector)
        const auto& next_elem = path[path_index + 1];
        if (std::holds_alternative<std::string>(next_elem)) {
            current_child = Value{ValueMap{}};
        } else {
            current_child = Value{ValueVector{}};
        }
    }
    
    Value new_child = set_at_path_recursive_vivify(current_child, path, path_index + 1, std::move(new_val));
    return set_at_path_element_vivify(root, elem, std::move(new_child));
}

Value set_at_path_vivify(const Value& root, const Path& path, Value new_val)
{
    if (path.empty()) {
        return new_val;
    }
    return set_at_path_recursive_vivify(root, path, 0, std::move(new_val));
}

// ============================================================
// Path Erasure Implementation
// ============================================================

Value erase_at_path_direct(const Value& root, const Path& path)
{
    if (path.empty()) {
        return Value{};  // Erase entire root
    }

    // If last element is a string key, we can erase from map
    if (path.size() == 1) {
        if (auto* key = std::get_if<std::string>(&path[0])) {
            return erase_key_from_map(root, *key);
        }
        // For index, set to null (cannot truly remove without reindexing)
        return set_at_path_direct(root, path, Value{});
    }

    // Navigate to parent and erase from there
    Path parent_path;
    parent_path.reserve(path.size() - 1);
    for (std::size_t i = 0; i < path.size() - 1; ++i) {
        parent_path.push_back(path[i]);
    }
    const auto& last = path.back();

    if (auto* key = std::get_if<std::string>(&last)) {
        // Get parent, erase key from it, set parent back
        Value parent = get_at_path_direct(root, parent_path);
        Value new_parent = erase_key_from_map(parent, *key);
        return set_at_path_direct(root, parent_path, new_parent);
    }

    // For index removal, set to null
    return set_at_path_direct(root, path, Value{});
}

// ============================================================
// Path Validation Implementation
// ============================================================

bool is_valid_path(const Value& root, const Path& path)
{
    Value current = root;
    for (const auto& elem : path) {
        if (!can_access(current, elem)) {
            return false;
        }
        current = get_at_path_element(current, elem);
    }
    return true;
}

std::size_t valid_path_depth(const Value& root, const Path& path)
{
    Value current = root;
    std::size_t depth = 0;
    for (const auto& elem : path) {
        if (!can_access(current, elem)) {
            break;
        }
        current = get_at_path_element(current, elem);
        ++depth;
    }
    return depth;
}

} // namespace lager_ext
