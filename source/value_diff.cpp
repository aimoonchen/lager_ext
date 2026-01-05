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

    // OPTIMIZATION: Use path stack instead of Path object for better performance
    path_stack_.clear();
    path_stack_.reserve(16);  // Pre-allocate for typical nesting depth
    diff_value_optimized(old_val, new_val, 0);

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
        std::cout << "  " << type_str << " " << d.path.to_dot_notation();
        if (d.type == DiffEntry::Type::Change) {
            std::cout << ": " << value_to_string(d.get_old()) << " -> " << value_to_string(d.get_new());
        } else if (d.type == DiffEntry::Type::Add) {
            std::cout << ": " << value_to_string(d.get_new());
        } else {
            std::cout << ": " << value_to_string(d.get_old());
        }
        std::cout << "\n";
    }
}

// ============================================================
// Optimized Path Stack Implementation
// These methods use the path_stack_ member for zero-allocation path building
// ============================================================

void DiffEntryCollector::diff_value_optimized(const Value& old_val, const Value& new_val, std::size_t path_depth)
{
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();
    if (old_index != new_index) [[unlikely]] {
        Path current_path(current_path_view(path_depth));
        diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
        return;
    }

    std::visit([&](const auto& old_arg) {
        using T = std::decay_t<decltype(old_arg)>;

        if constexpr (std::is_same_v<T, ValueMap>) {
            const auto& new_map = std::get<ValueMap>(new_val.data);
            // OPTIMIZATION: immer container identity check - O(1)
            if (old_arg.impl().root == new_map.impl().root &&
                old_arg.impl().size == new_map.impl().size) [[likely]] {
                return;
            }
            // Shallow mode: report container change without recursing
            if (!recursive_) {
                Path current_path(current_path_view(path_depth));
                diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
                return;
            }
            diff_map_optimized(old_arg, new_map, path_depth);
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            const auto& new_vec = std::get<ValueVector>(new_val.data);
            // OPTIMIZATION: immer container identity check - O(1)
            if (old_arg.impl().root == new_vec.impl().root &&
                old_arg.impl().tail == new_vec.impl().tail &&
                old_arg.impl().size == new_vec.impl().size) [[likely]] {
                return;
            }
            // Shallow mode: report container change without recursing
            if (!recursive_) {
                Path current_path(current_path_view(path_depth));
                diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
                return;
            }
            diff_vector_optimized(old_arg, new_vec, path_depth);
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            // Both null, no change
        }
        else {
            // Primitive types: direct comparison (already verified same type)
            const auto& new_arg = std::get<T>(new_val.data);
            if (old_arg != new_arg) {
                Path current_path(current_path_view(path_depth));
                diffs_.emplace_back(DiffEntry::Type::Change, current_path, old_val, new_val);
            }
        }
    }, old_val.data);
}

void DiffEntryCollector::diff_map_optimized(const ValueMap& old_map, const ValueMap& new_map, std::size_t path_depth)
{
    // Ensure path stack has enough capacity
    if (path_stack_.size() <= path_depth) {
        path_stack_.resize(path_depth + 1);
    }
    
    auto map_differ = immer::make_differ(
        // added
        [&](const std::pair<const std::string, ValueBox>& added_kv) {
            path_stack_[path_depth] = PathElement{added_kv.first};
            collect_added_optimized(*added_kv.second, path_depth + 1);
        },
        // removed
        [&](const std::pair<const std::string, ValueBox>& removed_kv) {
            path_stack_[path_depth] = PathElement{removed_kv.first};
            collect_removed_optimized(*removed_kv.second, path_depth + 1);
        },
        // changed (retained key)
        [&](const std::pair<const std::string, ValueBox>& old_kv,
            const std::pair<const std::string, ValueBox>& new_kv) {
            // Optimization: pointer comparison - O(1)
            if (old_kv.second.get() == new_kv.second.get()) [[likely]] {
                return; // Same pointer, unchanged
            }
            path_stack_[path_depth] = PathElement{old_kv.first};
            diff_value_optimized(*old_kv.second, *new_kv.second, path_depth + 1);
        }
    );

    immer::diff(old_map, new_map, map_differ);
}

void DiffEntryCollector::diff_vector_optimized(const ValueVector& old_vec, const ValueVector& new_vec, std::size_t path_depth)
{
    const size_t old_size = old_vec.size();
    const size_t new_size = new_vec.size();
    const size_t common_size = std::min(old_size, new_size);
    
    // Ensure path stack has enough capacity
    if (path_stack_.size() <= path_depth) {
        path_stack_.resize(path_depth + 1);
    }

    for (size_t i = 0; i < common_size; ++i) {
        const auto& old_box = old_vec[i];
        const auto& new_box = new_vec[i];

        // Optimization: immer::box pointer comparison - O(1)
        if (old_box.get() == new_box.get()) [[likely]] {
            continue;
        }

        path_stack_[path_depth] = PathElement{i};
        diff_value_optimized(*old_box, *new_box, path_depth + 1);
    }

    // Removed tail elements
    for (size_t i = common_size; i < old_size; ++i) {
        path_stack_[path_depth] = PathElement{i};
        collect_removed_optimized(*old_vec[i], path_depth + 1);
    }

    // Added tail elements
    for (size_t i = common_size; i < new_size; ++i) {
        path_stack_[path_depth] = PathElement{i};
        collect_added_optimized(*new_vec[i], path_depth + 1);
    }
}

void DiffEntryCollector::collect_entries_optimized(const Value& val, std::size_t path_depth, bool is_add)
{
    // In shallow mode, just record the value at current path (no recursion)
    if (!recursive_) {
        Path current_path(current_path_view(path_depth));
        if (is_add) {
            diffs_.emplace_back(DiffEntry::Type::Add, current_path, val, val);
        } else {
            diffs_.emplace_back(DiffEntry::Type::Remove, current_path, val, val);
        }
        return;
    }

    // Recursive mode: descend into containers
    std::visit([&](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, ValueMap>) {
            // Ensure path stack has enough capacity for next level
            if (path_stack_.size() <= path_depth) {
                path_stack_.resize(path_depth + 1);
            }
            for (const auto& [k, v] : arg) {
                path_stack_[path_depth] = PathElement{k};
                collect_entries_optimized(*v, path_depth + 1, is_add);
            }
        }
        else if constexpr (std::is_same_v<T, ValueVector>) {
            // Ensure path stack has enough capacity for next level
            if (path_stack_.size() <= path_depth) {
                path_stack_.resize(path_depth + 1);
            }
            for (size_t i = 0; i < arg.size(); ++i) {
                path_stack_[path_depth] = PathElement{i};
                collect_entries_optimized(*arg[i], path_depth + 1, is_add);
            }
        }
        else if constexpr (!std::is_same_v<T, std::monostate>) {
            // Leaf value: record it - both fields reference the same value
            Path current_path(current_path_view(path_depth));
            diffs_.emplace_back(is_add ? DiffEntry::Type::Add : DiffEntry::Type::Remove, 
                               current_path, val, val);
        }
    }, val.data);
}

void DiffEntryCollector::collect_removed_optimized(const Value& val, std::size_t path_depth)
{
    collect_entries_optimized(val, path_depth, false);
}

void DiffEntryCollector::collect_added_optimized(const Value& val, std::size_t path_depth)
{
    collect_entries_optimized(val, path_depth, true);
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

namespace {
    // Pre-cached diff type ValueBoxes to avoid repeated allocation
    // These are created once and shared across all diff operations
    inline const ValueBox& get_type_box(DiffEntry::Type type) {
        static const ValueBox add_box{Value{static_cast<uint8_t>(DiffEntry::Type::Add)}};
        static const ValueBox remove_box{Value{static_cast<uint8_t>(DiffEntry::Type::Remove)}};
        static const ValueBox change_box{Value{static_cast<uint8_t>(DiffEntry::Type::Change)}};
        
        switch (type) {
            case DiffEntry::Type::Add:    return add_box;
            case DiffEntry::Type::Remove: return remove_box;
            case DiffEntry::Type::Change: return change_box;
        }
        return add_box;  // fallback (should never reach)
    }
    
    // Pre-cached index strings for common vector indices (0-99)
    // Avoids std::to_string allocation for small indices
    inline const std::string& get_index_string(size_t i) {
        static const std::array<std::string, 100> cache = []() {
            std::array<std::string, 100> arr;
            for (size_t j = 0; j < 100; ++j) {
                arr[j] = std::to_string(j);
            }
            return arr;
        }();
        
        if (i < 100) [[likely]] {
            return cache[i];
        }
        // For large indices, use static buffer (single-threaded context)
        // This avoids heap allocation on each call
        static std::string large_index;
        large_index = std::to_string(i);
        return large_index;
    }
}

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
    
    // Use cached type box to avoid repeated allocation
    builder = builder.set(diff_keys::TYPE, get_type_box(type));
    
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
        // Use cached type box to avoid repeated allocation
        builder = builder.set(diff_keys::TYPE, get_type_box(type));
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
            result = result.set(get_index_string(i), ValueBox{subtree});
        }
    }
    
    // Removed tail elements - use box version for zero-copy
    for (size_t i = common_size; i < old_size; ++i) {
        has_any_change = true;
        Value subtree = collect_entries_box(old_vec[i], DiffEntry::Type::Remove);
        result = result.set(get_index_string(i), ValueBox{subtree});
    }
    
    // Added tail elements - use box version for zero-copy
    for (size_t i = common_size; i < new_size; ++i) {
        has_any_change = true;
        Value subtree = collect_entries_box(new_vec[i], DiffEntry::Type::Add);
        result = result.set(get_index_string(i), ValueBox{subtree});
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
                // Use box version for zero-copy, and cached index string
                Value subtree = collect_entries_box(arg[i], type);
                result = result.set(get_index_string(i), ValueBox{subtree});
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
                // Use cached index string to avoid allocation
                result = result.set(get_index_string(i), ValueBox{subtree});
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

Value diff_as_value(const Value& old_val, const Value& new_val, bool recursive)
{
    // Use DiffValueCollector for better performance (single-pass)
    DiffValueCollector collector;
    collector.diff(old_val, new_val, recursive);
    return collector.get();
}

// ============================================================
// Apply Diff Implementation
// ============================================================

namespace {
    /// Recursively apply diff tree to a value, creating new Value with changes applied
    /// @param root Current value being modified
    /// @param diff_tree Current diff subtree to apply
    /// @param path Current path (for error reporting)
    /// @return New Value with diff applied
    Value apply_diff_recursive(const Value& root, const Value& diff_tree, const Path& path = {}) {
        // Check if this is a diff leaf node (contains _diff_type)
        if (DiffValueCollector::is_diff_node(diff_tree)) {
            // This is a leaf diff node - extract the operation and value
            DiffEntry::Type type = DiffValueCollector::get_diff_type(diff_tree);
            
            switch (type) {
                case DiffEntry::Type::Add: {
                    // For Add: return the new value from the diff node
                    Value new_val = DiffValueCollector::get_new_value(diff_tree);
                    if (new_val.is_null()) {
                        throw std::runtime_error("apply_diff: Add operation missing _new value at path: " + path.to_dot_notation());
                    }
                    return new_val;
                }
                case DiffEntry::Type::Remove: {
                    // For Remove: return a null Value (removal)
                    return Value{};
                }
                case DiffEntry::Type::Change: {
                    // For Change: return the new value from the diff node
                    Value new_val = DiffValueCollector::get_new_value(diff_tree);
                    if (new_val.is_null()) {
                        throw std::runtime_error("apply_diff: Change operation missing _new value at path: " + path.to_dot_notation());
                    }
                    return new_val;
                }
            }
        }
        
        // This is an intermediate node - recurse into its children
        if (auto* diff_map = diff_tree.get_if<ValueMap>()) {
            // The diff tree is a map - apply changes to the corresponding structure in root
            if (auto* root_map = root.get_if<ValueMap>()) {
                // Root is also a map - merge changes
                ValueMap result = *root_map;
                
                for (const auto& [key, diff_child_box] : *diff_map) {
                    const Value& diff_child = *diff_child_box;
                    Path child_path = path;
                    child_path.push_back(key);
                    
                    // Get the current value at this key (or null if not present)
                    Value current_val;
                    if (auto* existing_box = root_map->find(key)) {
                        current_val = existing_box->get();
                    } else {
                        current_val = Value{};  // null for missing keys
                    }
                    
                    // Recursively apply diff to this child
                    Value new_child = apply_diff_recursive(current_val, diff_child, child_path);
                    
                    // Update or remove the key based on the result
                    if (new_child.is_null()) {
                        // Remove the key if the result is null
                        result = result.erase(key);
                    } else {
                        // Set the new value
                        result = result.set(key, ValueBox{new_child});
                    }
                }
                
                return Value{result};
            }
            else if (root.is_null()) {
                // Root is null but we have changes to apply - create a new map
                ValueMap result;
                
                for (const auto& [key, diff_child_box] : *diff_map) {
                    const Value& diff_child = *diff_child_box;
                    Path child_path = path;
                    child_path.push_back(key);
                    
                    // Apply diff to null value (effectively adding new keys)
                    Value new_child = apply_diff_recursive(Value{}, diff_child, child_path);
                    
                    if (!new_child.is_null()) {
                        result = result.set(key, ValueBox{new_child});
                    }
                }
                
                return Value{result};
            }
            else {
                throw std::runtime_error("apply_diff: Type mismatch - diff expects map but root is not a map at path: " + path.to_dot_notation());
            }
        }
        else if (auto* diff_vector_map = diff_tree.get_if<ValueMap>()) {
            // Check if this looks like a vector diff (all keys are numeric strings)
            bool all_numeric = true;
            for (const auto& [key, _] : *diff_vector_map) {
                try {
                    [[maybe_unused]] size_t parsed = std::stoul(key);  // Try to parse as unsigned integer
                } catch (...) {
                    all_numeric = false;
                    break;
                }
            }
            
            if (all_numeric) {
                // This is a vector diff represented as a map with numeric string keys
                if (auto* root_vec = root.get_if<ValueVector>()) {
                    // Root is a vector - apply indexed changes
                    ValueVector result = *root_vec;
                    
                    for (const auto& [index_str, diff_child_box] : *diff_vector_map) {
                        const Value& diff_child = *diff_child_box;
                        size_t index = std::stoul(index_str);
                        Path child_path = path;
                        child_path.push_back(index);
                        
                        // Get the current value at this index (or null if out of bounds)
                        Value current_val;
                        if (index < result.size()) {
                            current_val = *result[index];
                        } else {
                            current_val = Value{};  // null for out-of-bounds indices
                        }
                        
                        // Recursively apply diff to this child
                        Value new_child = apply_diff_recursive(current_val, diff_child, child_path);
                        
                        // Update the vector
                        if (!new_child.is_null()) {
                            // Extend vector if necessary
                            while (result.size() <= index) {
                                result = result.push_back(ValueBox{Value{}});
                            }
                            result = result.set(index, ValueBox{new_child});
                        } else if (index < result.size()) {
                            // For removal, we could either remove the element or set it to null
                            // For now, let's set it to null to preserve indices
                            result = result.set(index, ValueBox{Value{}});
                        }
                    }
                    
                    return Value{result};
                }
                else if (root.is_null()) {
                    // Root is null but we have vector changes to apply - create a new vector
                    ValueVector result;
                    
                    // Collect all indices and sort them
                    std::vector<std::pair<size_t, const Value*>> indexed_diffs;
                    for (const auto& [index_str, diff_child_box] : *diff_vector_map) {
                        size_t index = std::stoul(index_str);
                        indexed_diffs.emplace_back(index, &(*diff_child_box));
                    }
                    std::sort(indexed_diffs.begin(), indexed_diffs.end());
                    
                    // Apply changes in order
                    for (const auto& [index, diff_child] : indexed_diffs) {
                        Path child_path = path;
                        child_path.push_back(index);
                        
                        Value new_child = apply_diff_recursive(Value{}, *diff_child, child_path);
                        
                        if (!new_child.is_null()) {
                            // Extend vector if necessary
                            while (result.size() <= index) {
                                result = result.push_back(ValueBox{Value{}});
                            }
                            result = result.set(index, ValueBox{new_child});
                        }
                    }
                    
                    return Value{result};
                }
                else {
                    throw std::runtime_error("apply_diff: Type mismatch - diff expects vector but root is not a vector at path: " + path.to_dot_notation());
                }
            }
        }
        
        // If we reach here, the diff_tree structure doesn't match expected patterns
        throw std::runtime_error("apply_diff: Unexpected diff tree structure at path: " + path.to_dot_notation());
    }
}

Value apply_diff(const Value& root, const Value& diff_tree)
{
    // Handle empty diff
    if (diff_tree.is_null()) {
        return root;  // No changes to apply
    }
    
    // Check if diff is empty (no changes recorded)
    if (auto* diff_map = diff_tree.get_if<ValueMap>()) {
        if (diff_map->empty()) {
            return root;  // Empty diff, no changes
        }
    }
    
    try {
        return apply_diff_recursive(root, diff_tree);
    } catch (const std::exception& e) {
        // Re-throw with additional context
        throw std::runtime_error(std::string("apply_diff failed: ") + e.what());
    }
}

} // namespace lager_ext
