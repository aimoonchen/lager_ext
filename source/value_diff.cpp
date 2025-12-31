// diff_collector.cpp - DiffCollector and diff demos

#include <lager_ext/value_diff.h>
#include <immer/algorithm.hpp>
#include <iostream>

namespace lager_ext {

// ============================================================
// DiffEntryCollector Implementation
// ============================================================

void DiffEntryCollector::diff(const Value& old_val, const Value& new_val, bool recursive)
{
    diffs_.clear();
    recursive_ = recursive;

    // Fast path: if both Values share the same variant storage, no changes
    // This handles the case where the same Value object is compared to itself
    if (&old_val.data == &new_val.data) {
        return;
    }

    // Pre-allocate to reduce reallocations during diff collection
    diffs_.reserve(32);

    Path root_path;
    root_path.reserve(16);  // Pre-allocate for typical nesting depth
    diff_value(old_val, new_val, root_path);

    // Shrink to fit if we over-allocated significantly
    if (diffs_.size() > 0 && diffs_.capacity() > diffs_.size() * 2) {
        diffs_.shrink_to_fit();
    }
}

const std::vector<DiffEntry>& DiffEntryCollector::get_diffs() const
{
    return diffs_;
}

void DiffEntryCollector::clear()
{
    diffs_.clear();
}

bool DiffEntryCollector::has_changes() const
{
    return !diffs_.empty();
}

void DiffEntryCollector::print_diffs() const
{
    if (diffs_.empty()) {
        std::cout << "  (no changes)\n";
        return;
    }
    for (const auto& d : diffs_) {
        std::string type_str;
        switch (d.type) {
            case DiffEntry::Type::Add:    type_str = "ADD   "; break;
            case DiffEntry::Type::Remove: type_str = "REMOVE"; break;
            case DiffEntry::Type::Change: type_str = "CHANGE"; break;
        }
        std::cout << "  " << type_str << " " << path_to_string(d.path);
        if (d.type == DiffEntry::Type::Change) {
            std::cout << ": " << value_to_string(d.old_value) << " -> " << value_to_string(d.new_value);
        } else if (d.type == DiffEntry::Type::Add) {
            std::cout << ": " << value_to_string(d.new_value);
        } else {
            std::cout << ": " << value_to_string(d.old_value);
        }
        std::cout << "\n";
    }
}

void DiffEntryCollector::diff_value(const Value& old_val, const Value& new_val, Path& current_path)
{
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();
    if (old_index != new_index) [[unlikely]] {
        diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
        return;
    }

    std::visit([&](const auto& old_arg) {
        using T = std::decay_t<decltype(old_arg)>;

        if constexpr (std::is_same_v<T, ValueMap>) {
            // We already verified indices match, so this is safe
            const auto& new_map = std::get<ValueMap>(new_val.data);
            // OPTIMIZATION: immer container identity check - O(1)
            // If the underlying immer::map shares the same root, skip entirely
            if (old_arg.impl().root == new_map.impl().root &&
                old_arg.impl().size == new_map.impl().size) [[likely]] {
                return;
            }
            // Shallow mode: report container change without recursing
            if (!recursive_) {
                diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
                return;
            }
            diff_map(old_arg, new_map, current_path);
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            const auto& new_vec = std::get<ValueVector>(new_val.data);
            // OPTIMIZATION: immer container identity check - O(1)
            // If the underlying immer::flex_vector shares the same root, skip entirely
            if (old_arg.impl().root == new_vec.impl().root &&
                old_arg.impl().tail == new_vec.impl().tail &&
                old_arg.impl().size == new_vec.impl().size) [[likely]] {
                return;
            }
            // Shallow mode: report container change without recursing
            if (!recursive_) {
                diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
                return;
            }
            diff_vector(old_arg, new_vec, current_path);
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            // Both null, no change
        }
        else {
            // Primitive types: direct comparison (already verified same type)
            const auto& new_arg = std::get<T>(new_val.data);
            if (old_arg != new_arg) {
                diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
            }
        }
    }, old_val.data);
}

void DiffEntryCollector::diff_map(const ValueMap& old_map, const ValueMap& new_map, Path& current_path)
{
    // OPTIMIZED: Use push_back/pop_back pattern to avoid Path copying
    auto map_differ = immer::make_differ(
        // added
        [&](const std::pair<const std::string, ValueBox>& added_kv) {
            current_path.push_back(added_kv.first);
            collect_added(*added_kv.second, current_path);
            current_path.pop_back();
        },
        // removed
        [&](const std::pair<const std::string, ValueBox>& removed_kv) {
            current_path.push_back(removed_kv.first);
            collect_removed(*removed_kv.second, current_path);
            current_path.pop_back();
        },
        // changed (retained key)
        [&](const std::pair<const std::string, ValueBox>& old_kv,
            const std::pair<const std::string, ValueBox>& new_kv) {
            // Optimization: pointer comparison - O(1)
            if (old_kv.second.get() == new_kv.second.get()) [[likely]] {
                return; // Same pointer, unchanged
            }
            current_path.push_back(old_kv.first);
            diff_value(*old_kv.second, *new_kv.second, current_path);
            current_path.pop_back();
        }
    );

    immer::diff(old_map, new_map, map_differ);
}

void DiffEntryCollector::diff_vector(const ValueVector& old_vec, const ValueVector& new_vec, Path& current_path)
{
    const size_t old_size = old_vec.size();
    const size_t new_size = new_vec.size();
    const size_t common_size = std::min(old_size, new_size);

    for (size_t i = 0; i < common_size; ++i) {
        const auto& old_box = old_vec[i];
        const auto& new_box = new_vec[i];

        // Optimization: immer::box pointer comparison - O(1)
        if (old_box.get() == new_box.get()) [[likely]] {
            continue;
        }

        current_path.push_back(i);
        diff_value(*old_box, *new_box, current_path);
        current_path.pop_back();
    }

    // Removed tail elements
    for (size_t i = common_size; i < old_size; ++i) {
        current_path.push_back(i);
        collect_removed(*old_vec[i], current_path);
        current_path.pop_back();
    }

    // Added tail elements
    for (size_t i = common_size; i < new_size; ++i) {
        current_path.push_back(i);
        collect_added(*new_vec[i], current_path);
        current_path.pop_back();
    }
}

// Helper: Collect entries for add/remove operations
// In recursive mode: recursively collect all nested entries
// In shallow mode: only report the container itself
// is_add: true for added entries, false for removed entries
//
// Note: For Add, we store (val, val) - old_value references new_value for convenience
//       For Remove, we store (val, val) - new_value references old_value for convenience
//       This avoids creating empty Value{} objects and allows direct reference to the actual value.
void DiffEntryCollector::collect_entries(const Value& val, Path& current_path, bool is_add)
{
    // In shallow mode, just record the value at current path (no recursion)
    if (!recursive_) {
        if (is_add) {
            // Add: new_value is the meaningful one, old_value references it too
            diffs_.emplace_back(DiffEntry::Type::Add, current_path, val, val);
        } else {
            // Remove: old_value is the meaningful one, new_value references it too
            diffs_.emplace_back(DiffEntry::Type::Remove, current_path, val, val);
        }
        return;
    }

    // Recursive mode: descend into containers
    std::visit([&](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, ValueMap>) {
            for (const auto& [k, v] : arg) {
                current_path.push_back(k);
                collect_entries(*v, current_path, is_add);
                current_path.pop_back();
            }
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            for (size_t i = 0; i < arg.size(); ++i) {
                current_path.push_back(i);
                collect_entries(*arg[i], current_path, is_add);
                current_path.pop_back();
            }
        }
        else if constexpr (!std::is_same_v<T, std::monostate>) {
            // Leaf value: record it - both fields reference the same value
            diffs_.emplace_back(is_add ? DiffEntry::Type::Add : DiffEntry::Type::Remove, 
                               current_path, val, val);
        }
    }, val.data);
}

void DiffEntryCollector::collect_removed(const Value& val, Path& current_path)
{
    collect_entries(val, current_path, false);
}

void DiffEntryCollector::collect_added(const Value& val, Path& current_path)
{
    collect_entries(val, current_path, true);
}

bool has_any_difference(const Value& old_val, const Value& new_val, bool recursive)
{
    // Fast path: same object
    if (&old_val.data == &new_val.data) {
        return false;
    }
    return detail::values_differ(old_val, new_val, recursive);
}

namespace detail {

bool values_differ(const Value& old_val, const Value& new_val, bool recursive)
{
    // Fast path: different types
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();
    if (old_index != new_index) [[unlikely]] {
        return true;
    }

    return std::visit([&](const auto& old_arg) -> bool {
        using T = std::decay_t<decltype(old_arg)>;

        if constexpr (std::is_same_v<T, ValueMap>) {
            const auto& new_map = std::get<ValueMap>(new_val.data);
            // O(1) identity check
            if (old_arg.impl().root == new_map.impl().root &&
                old_arg.impl().size == new_map.impl().size) [[likely]] {
                return false;
            }
            // Shallow mode: identity check failed, containers are different
            if (!recursive) {
                return true;
            }
            return maps_differ(old_arg, new_map, recursive);
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            const auto& new_vec = std::get<ValueVector>(new_val.data);
            // O(1) identity check
            if (old_arg.impl().root == new_vec.impl().root &&
                old_arg.impl().tail == new_vec.impl().tail &&
                old_arg.impl().size == new_vec.impl().size) [[likely]] {
                return false;
            }
            // Shallow mode: identity check failed, containers are different
            if (!recursive) {
                return true;
            }
            return vectors_differ(old_arg, new_vec, recursive);
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            return false;  // Both null
        }
        else {
            // Primitive types: direct comparison
            const auto& new_arg = std::get<T>(new_val.data);
            return old_arg != new_arg;
        }
    }, old_val.data);
}

bool maps_differ(const ValueMap& old_map, const ValueMap& new_map, bool recursive)
{
    // Quick size check
    if (old_map.size() != new_map.size()) {
        return true;
    }

    // Use immer::diff with early exit
    bool found_difference = false;

    auto differ = immer::make_differ(
        // added - any addition means difference
        [&](const std::pair<const std::string, ValueBox>&) {
            found_difference = true;
        },
        // removed - any removal means difference
        [&](const std::pair<const std::string, ValueBox>&) {
            found_difference = true;
        },
        // retained - check if values differ
        [&](const std::pair<const std::string, ValueBox>& old_kv,
            const std::pair<const std::string, ValueBox>& new_kv) {
            if (found_difference) return;  // Already found, skip

            // O(1) pointer check
            if (old_kv.second.get() == new_kv.second.get()) [[likely]] {
                return;
            }
            // Shallow mode: pointer differs, that's enough
            if (!recursive) {
                found_difference = true;
                return;
            }
            // Recurse
            if (values_differ(*old_kv.second, *new_kv.second, recursive)) {
                found_difference = true;
            }
        }
    );

    immer::diff(old_map, new_map, differ);
    return found_difference;
}

bool vectors_differ(const ValueVector& old_vec, const ValueVector& new_vec, bool recursive)
{
    const size_t old_size = old_vec.size();
    const size_t new_size = new_vec.size();

    // Size difference means there's a change
    if (old_size != new_size) {
        return true;
    }

    // Check each element (early exit on first difference)
    for (size_t i = 0; i < old_size; ++i) {
        const auto& old_box = old_vec[i];
        const auto& new_box = new_vec[i];

        // O(1) pointer check
        if (old_box.get() == new_box.get()) [[likely]] {
            continue;
        }

        // Shallow mode: pointer differs, that's enough
        if (!recursive) {
            return true;
        }

        // Recurse - early exit if different
        if (values_differ(*old_box, *new_box, recursive)) {
            return true;
        }
    }

    return false;
}

} // namespace detail

// ============================================================
// DiffValueCollector - Single-pass diff to Value tree
// ============================================================

void DiffValueCollector::diff(const Value& old_val, const Value& new_val, bool recursive)
{
    clear();
    recursive_ = recursive;
    
    // Fast path: if both Values share the same variant storage, no changes
    if (&old_val.data == &new_val.data) {
        return;
    }
    
    bool changed = false;
    result_ = diff_value_impl(old_val, new_val, changed);
    has_changes_ = changed;
}

void DiffValueCollector::clear()
{
    result_ = Value{};
    has_changes_ = false;
}

Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const ValueBox& val_box)
{
    auto builder = ValueMap{};
    
    // Set diff type as uint8_t
    uint8_t type_val = static_cast<uint8_t>(type);
    builder = builder.set(diff_keys::TYPE, ValueBox{Value{type_val}});
    
    // Store the value box directly - no copy, just reference count increment
    // Add: only _new (value being added)
    // Remove: only _old (value being removed)
    // Change: should not use this overload - use the two-value version
    if (type == DiffEntry::Type::Add) {
        builder = builder.set(diff_keys::NEW, val_box);
    } else if (type == DiffEntry::Type::Remove) {
        builder = builder.set(diff_keys::OLD, val_box);
    }
    
    return Value{builder};
}

Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const ValueBox& old_box, const ValueBox& new_box)
{
    // For Change type, we need both values
    if (type == DiffEntry::Type::Change) {
        auto builder = ValueMap{};
        builder = builder.set(diff_keys::TYPE, ValueBox{Value{static_cast<uint8_t>(type)}});
        builder = builder.set(diff_keys::OLD, old_box);
        builder = builder.set(diff_keys::NEW, new_box);
        return Value{builder};
    }
    
    // For Add/Remove, delegate to single-value version using the meaningful value
    return make_diff_node(type, (type == DiffEntry::Type::Add) ? new_box : old_box);
}

// Convenience overloads for Value& (wraps into ValueBox)
Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const Value& val)
{
    return make_diff_node(type, ValueBox{val});
}

Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const Value& old_val, const Value& new_val)
{
    return make_diff_node(type, ValueBox{old_val}, ValueBox{new_val});
}

Value DiffValueCollector::diff_value_impl(const Value& old_val, const Value& new_val, bool& changed)
{
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();
    
    // Different types - whole value changed
    if (old_index != new_index) [[unlikely]] {
        changed = true;
        return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
    }
    
    return std::visit([&](const auto& old_arg) -> Value {
        using T = std::decay_t<decltype(old_arg)>;
        
        if constexpr (std::is_same_v<T, ValueMap>) {
            const auto& new_map = std::get<ValueMap>(new_val.data);
            // O(1) identity check
            if (old_arg.impl().root == new_map.impl().root &&
                old_arg.impl().size == new_map.impl().size) [[likely]] {
                return Value{};  // No change
            }
            // Shallow mode: report container change without recursing
            if (!recursive_) {
                changed = true;
                return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
            }
            return diff_map_impl(old_arg, new_map, changed);
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            const auto& new_vec = std::get<ValueVector>(new_val.data);
            // O(1) identity check
            if (old_arg.impl().root == new_vec.impl().root &&
                old_arg.impl().tail == new_vec.impl().tail &&
                old_arg.impl().size == new_vec.impl().size) [[likely]] {
                return Value{};  // No change
            }
            // Shallow mode: report container change without recursing
            if (!recursive_) {
                changed = true;
                return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
            }
            return diff_vector_impl(old_arg, new_vec, changed);
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            return Value{};  // Both null, no change
        }
        else {
            // Primitive types: direct comparison
            const auto& new_arg = std::get<T>(new_val.data);
            if (old_arg != new_arg) {
                changed = true;
                return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
            }
            return Value{};  // No change
        }
    }, old_val.data);
}

// ValueBox version for zero-copy when we already have boxes from container traversal
Value DiffValueCollector::diff_value_impl_box(const ValueBox& old_box, const ValueBox& new_box, bool& changed)
{
    const Value& old_val = *old_box;
    const Value& new_val = *new_box;
    
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();
    
    // Different types - whole value changed, use the boxes directly
    if (old_index != new_index) [[unlikely]] {
        changed = true;
        return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
    }
    
    return std::visit([&](const auto& old_arg) -> Value {
        using T = std::decay_t<decltype(old_arg)>;
        
        if constexpr (std::is_same_v<T, ValueMap>) {
            const auto& new_map = std::get<ValueMap>(new_val.data);
            if (old_arg.impl().root == new_map.impl().root &&
                old_arg.impl().size == new_map.impl().size) [[likely]] {
                return Value{};
            }
            if (!recursive_) {
                changed = true;
                return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
            }
            return diff_map_impl(old_arg, new_map, changed);
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            const auto& new_vec = std::get<ValueVector>(new_val.data);
            if (old_arg.impl().root == new_vec.impl().root &&
                old_arg.impl().tail == new_vec.impl().tail &&
                old_arg.impl().size == new_vec.impl().size) [[likely]] {
                return Value{};
            }
            if (!recursive_) {
                changed = true;
                return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
            }
            return diff_vector_impl(old_arg, new_vec, changed);
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            return Value{};
        }
        else {
            const auto& new_arg = std::get<T>(new_val.data);
            if (old_arg != new_arg) {
                changed = true;
                return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
            }
            return Value{};
        }
    }, old_val.data);
}

Value DiffValueCollector::diff_map_impl(const ValueMap& old_map, const ValueMap& new_map, bool& changed)
{
    ValueMap result;
    bool has_any_change = false;
    
    auto map_differ = immer::make_differ(
        // added - use ValueBox directly for zero-copy
        [&](const std::pair<const std::string, ValueBox>& added_kv) {
            has_any_change = true;
            Value subtree = collect_entries_box(added_kv.second, DiffEntry::Type::Add);
            result = result.set(added_kv.first, ValueBox{subtree});
        },
        // removed - use ValueBox directly for zero-copy
        [&](const std::pair<const std::string, ValueBox>& removed_kv) {
            has_any_change = true;
            Value subtree = collect_entries_box(removed_kv.second, DiffEntry::Type::Remove);
            result = result.set(removed_kv.first, ValueBox{subtree});
        },
        // changed (retained key)
        [&](const std::pair<const std::string, ValueBox>& old_kv,
            const std::pair<const std::string, ValueBox>& new_kv) {
            // O(1) pointer check
            if (old_kv.second.get() == new_kv.second.get()) [[likely]] {
                return;
            }
            bool subtree_changed = false;
            Value subtree = diff_value_impl_box(old_kv.second, new_kv.second, subtree_changed);
            if (subtree_changed) {
                has_any_change = true;
                result = result.set(old_kv.first, ValueBox{subtree});
            }
        }
    );
    
    immer::diff(old_map, new_map, map_differ);
    
    if (has_any_change) {
        changed = true;
        return Value{result};
    }
    return Value{};
}

Value DiffValueCollector::diff_vector_impl(const ValueVector& old_vec, const ValueVector& new_vec, bool& changed)
{
    const size_t old_size = old_vec.size();
    const size_t new_size = new_vec.size();
    const size_t common_size = std::min(old_size, new_size);
    
    ValueMap result;
    bool has_any_change = false;
    
    // Compare common elements - use box version for zero-copy
    for (size_t i = 0; i < common_size; ++i) {
        const auto& old_box = old_vec[i];
        const auto& new_box = new_vec[i];
        
        // O(1) pointer check
        if (old_box.get() == new_box.get()) [[likely]] {
            continue;
        }
        
        bool subtree_changed = false;
        Value subtree = diff_value_impl_box(old_box, new_box, subtree_changed);
        if (subtree_changed) {
            has_any_change = true;
            result = result.set(std::to_string(i), ValueBox{subtree});
        }
    }
    
    // Removed tail elements - use box version for zero-copy
    for (size_t i = common_size; i < old_size; ++i) {
        has_any_change = true;
        Value subtree = collect_entries_box(old_vec[i], DiffEntry::Type::Remove);
        result = result.set(std::to_string(i), ValueBox{subtree});
    }
    
    // Added tail elements - use box version for zero-copy
    for (size_t i = common_size; i < new_size; ++i) {
        has_any_change = true;
        Value subtree = collect_entries_box(new_vec[i], DiffEntry::Type::Add);
        result = result.set(std::to_string(i), ValueBox{subtree});
    }
    
    if (has_any_change) {
        changed = true;
        return Value{result};
    }
    return Value{};
}

Value DiffValueCollector::collect_entries(const Value& val, DiffEntry::Type type)
{
    // In shallow mode, just create a diff node for the entire value
    // Use single-value make_diff_node which stores val in the appropriate field
    if (!recursive_) {
        return make_diff_node(type, val);
    }
    
    // Recursive mode: descend into containers
    return std::visit([&](const auto& arg) -> Value {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, ValueMap>) {
            ValueMap result;
            for (const auto& [k, v] : arg) {
                // Use box version for zero-copy
                Value subtree = collect_entries_box(v, type);
                result = result.set(k, ValueBox{subtree});
            }
            return Value{result};
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            ValueMap result;
            for (size_t i = 0; i < arg.size(); ++i) {
                // Use box version for zero-copy
                Value subtree = collect_entries_box(arg[i], type);
                result = result.set(std::to_string(i), ValueBox{subtree});
            }
            return Value{result};
        }
        else if constexpr (!std::is_same_v<T, std::monostate>) {
            // Leaf value: create diff node - use single-value version
            return make_diff_node(type, val);
        }
        else {
            return Value{};  // monostate - nothing to collect
        }
    }, val.data);
}

// ValueBox version for zero-copy - stores the box directly without unpacking/repacking
Value DiffValueCollector::collect_entries_box(const ValueBox& val_box, DiffEntry::Type type)
{
    const Value& val = *val_box;
    
    // In shallow mode, just create a diff node using the box directly
    if (!recursive_) {
        return make_diff_node(type, val_box);
    }
    
    // Recursive mode: descend into containers
    return std::visit([&](const auto& arg) -> Value {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, ValueMap>) {
            ValueMap result;
            for (const auto& [k, v] : arg) {
                Value subtree = collect_entries_box(v, type);
                result = result.set(k, ValueBox{subtree});
            }
            return Value{result};
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            ValueMap result;
            for (size_t i = 0; i < arg.size(); ++i) {
                Value subtree = collect_entries_box(arg[i], type);
                result = result.set(std::to_string(i), ValueBox{subtree});
            }
            return Value{result};
        }
        else if constexpr (!std::is_same_v<T, std::monostate>) {
            // Leaf value: create diff node using the box directly (zero-copy)
            return make_diff_node(type, val_box);
        }
        else {
            return Value{};  // monostate - nothing to collect
        }
    }, val.data);
}

bool DiffValueCollector::is_diff_node(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        return m->count(diff_keys::TYPE) > 0;
    }
    return false;
}

DiffEntry::Type DiffValueCollector::get_diff_type(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        if (auto* type_box = m->find(diff_keys::TYPE)) {
            if (auto* type_val = type_box->get().get_if<uint8_t>()) {
                if (*type_val <= static_cast<uint8_t>(DiffEntry::Type::Change)) {
                    return static_cast<DiffEntry::Type>(*type_val);
                }
            }
        }
    }
    return DiffEntry::Type::Add;
}

Value DiffValueCollector::get_old_value(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        if (auto* old_box = m->find(diff_keys::OLD)) {
            return old_box->get();
        }
    }
    return Value{};
}

Value DiffValueCollector::get_new_value(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        if (auto* new_box = m->find(diff_keys::NEW)) {
            return new_box->get();
        }
    }
    return Value{};
}

void DiffValueCollector::print() const
{
    std::cout << "DiffValueCollector tree:\n";
    if (!has_changes_) {
        std::cout << "  (no changes)\n";
        return;
    }
    print_value(result_, "  ");
}

// ============================================================
// DiffValue - Wrapper using DiffValueCollector
// ============================================================

void DiffValue::diff(const Value& old_val, const Value& new_val, bool recursive)
{
    // Delegate to DiffValueCollector for efficient single-pass tree construction
    DiffValueCollector collector;
    collector.diff(old_val, new_val, recursive);
    
    result_ = collector.get();
    has_changes_ = collector.has_changes();
}

void DiffValue::clear()
{
    result_ = Value{};
    has_changes_ = false;
}

bool DiffValue::is_diff_node(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        return m->count(diff_keys::TYPE) > 0;
    }
    return false;
}

DiffEntry::Type DiffValue::get_diff_type(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        if (auto* type_box = m->find(diff_keys::TYPE)) {
            if (auto* type_val = type_box->get().get_if<uint8_t>()) {
                // Validate the value is within enum range
                if (*type_val <= static_cast<uint8_t>(DiffEntry::Type::Change)) {
                    return static_cast<DiffEntry::Type>(*type_val);
                }
            }
        }
    }
    // Default fallback (should not happen if is_diff_node() was checked first)
    return DiffEntry::Type::Add;
}

Value DiffValue::get_old_value(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        if (auto* old_box = m->find(diff_keys::OLD)) {
            return old_box->get();
        }
    }
    return Value{};
}

Value DiffValue::get_new_value(const Value& val)
{
    if (auto* m = val.get_if<ValueMap>()) {
        if (auto* new_box = m->find(diff_keys::NEW)) {
            return new_box->get();
        }
    }
    return Value{};
}

void DiffValue::print() const
{
    std::cout << "DiffValue tree:\n";
    if (!has_changes_) {
        std::cout << "  (no changes)\n";
        return;
    }
    print_value(result_, "  ");
}

Value diff_as_value(const Value& old_val, const Value& new_val, bool recursive)
{
    // Use DiffValueCollector for better performance (single-pass)
    DiffValueCollector collector;
    collector.diff(old_val, new_val, recursive);
    return collector.get();
}

} // namespace lager_ext
