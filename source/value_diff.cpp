// diff_collector.cpp - DiffCollector and diff demos

#include <lager_ext/path_utils.h>
#include <lager_ext/value_diff.h>

#include <immer/algorithm.hpp>

#include <iostream>

namespace lager_ext {

// ============================================================
// DiffEntryCollector Implementation
// ============================================================

void DiffEntryCollector::diff(const Value& old_val, const Value& new_val, bool recursive) {
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
    path_stack_.reserve(16); // Pre-allocate for typical nesting depth
    diff_value_optimized(old_val, new_val, 0);

    // Shrink to fit if we over-allocated significantly
    if (diffs_.size() > 0 && diffs_.capacity() > diffs_.size() * 2) {
        diffs_.shrink_to_fit();
    }
}

const std::vector<DiffEntry>& DiffEntryCollector::get_diffs() const {
    return diffs_;
}

void DiffEntryCollector::clear() {
    diffs_.clear();
}

bool DiffEntryCollector::has_changes() const {
    return !diffs_.empty();
}

void DiffEntryCollector::print_diffs() const {
    if (diffs_.empty()) {
        std::cout << "  (no changes)\n";
        return;
    }
    for (const auto& d : diffs_) {
        std::string type_str;
        switch (d.type) {
        case DiffEntry::Type::Add:
            type_str = "ADD   ";
            break;
        case DiffEntry::Type::Remove:
            type_str = "REMOVE";
            break;
        case DiffEntry::Type::Change:
            type_str = "CHANGE";
            break;
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

void DiffEntryCollector::diff_value_optimized(const Value& old_val, const Value& new_val, std::size_t path_depth) {
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();
    if (old_index != new_index) [[unlikely]] {
        Path current_path(current_path_view(path_depth));
        diffs_.emplace_back(DiffEntry::Type::Change, std::move(current_path), old_val, new_val);
        return;
    }

    std::visit(
        [&](const auto& old_arg) {
            using T = std::decay_t<decltype(old_arg)>;

            // Container Boxing: variant now holds BoxedValueMap, BoxedValueVector, etc.
            if constexpr (std::is_same_v<T, BoxedValueMap>) {
                const auto& new_boxed = std::get<BoxedValueMap>(new_val.data);
                // OPTIMIZATION: box identity check - O(1)
                if (old_arg.get().impl().root == new_boxed.get().impl().root &&
                    old_arg.get().impl().size == new_boxed.get().impl().size) [[likely]] {
                    return;
                }
                // Shallow mode: report container change without recursing
                if (!recursive_) {
                    Path current_path(current_path_view(path_depth));
                    diffs_.emplace_back(DiffEntry::Type::Change, std::move(current_path), old_val, new_val);
                    return;
                }
                diff_map_optimized(old_arg.get(), new_boxed.get(), path_depth);
            } else if constexpr (std::is_same_v<T, BoxedValueVector>) {
                const auto& new_boxed = std::get<BoxedValueVector>(new_val.data);
                // OPTIMIZATION: box identity check - O(1)
                if (old_arg.get().impl().root == new_boxed.get().impl().root &&
                    old_arg.get().impl().tail == new_boxed.get().impl().tail &&
                    old_arg.get().impl().size == new_boxed.get().impl().size) [[likely]] {
                    return;
                }
                // Shallow mode: report container change without recursing
                if (!recursive_) {
                    Path current_path(current_path_view(path_depth));
                    diffs_.emplace_back(DiffEntry::Type::Change, std::move(current_path), old_val, new_val);
                    return;
                }
                diff_vector_optimized(old_arg.get(), new_boxed.get(), path_depth);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                // Both null, no change
            } else {
                // Primitive types and BoxedString: direct comparison (already verified same type)
                const auto& new_arg = std::get<T>(new_val.data);
                if (old_arg != new_arg) {
                    Path current_path(current_path_view(path_depth));
                    diffs_.emplace_back(DiffEntry::Type::Change, std::move(current_path), old_val, new_val);
                }
            }
        },
        old_val.data);
}

void DiffEntryCollector::diff_map_optimized(const ValueMap& old_map, const ValueMap& new_map, std::size_t path_depth) {
    // Ensure path stack has enough capacity
    if (path_stack_.size() <= path_depth) {
        path_stack_.resize(path_depth + 1);
    }

    // Container Boxing: ValueMap now stores Value directly, not ValueBox
    auto map_differ = immer::make_differ(
        // added
        [&](const std::pair<const std::string, Value>& added_kv) {
            path_stack_[path_depth] = PathElement{added_kv.first};
            collect_added_optimized(added_kv.second, path_depth + 1);
        },
        // removed
        [&](const std::pair<const std::string, Value>& removed_kv) {
            path_stack_[path_depth] = PathElement{removed_kv.first};
            collect_removed_optimized(removed_kv.second, path_depth + 1);
        },
        // changed (retained key)
        [&](const std::pair<const std::string, Value>& old_kv,
            const std::pair<const std::string, Value>& new_kv) {
            path_stack_[path_depth] = PathElement{old_kv.first};
            diff_value_optimized(old_kv.second, new_kv.second, path_depth + 1);
        });

    immer::diff(old_map, new_map, map_differ);
}

void DiffEntryCollector::diff_vector_optimized(const ValueVector& old_vec, const ValueVector& new_vec,
                                               std::size_t path_depth) {
    const size_t old_size = old_vec.size();
    const size_t new_size = new_vec.size();
    const size_t common_size = std::min(old_size, new_size);

    // Ensure path stack has enough capacity
    if (path_stack_.size() <= path_depth) {
        path_stack_.resize(path_depth + 1);
    }

    // Container Boxing: ValueVector now stores Value directly, not ValueBox
    for (size_t i = 0; i < common_size; ++i) {
        const Value& old_val = old_vec[i];
        const Value& new_val = new_vec[i];

        path_stack_[path_depth] = PathElement{i};
        diff_value_optimized(old_val, new_val, path_depth + 1);
    }

    // Removed tail elements
    for (size_t i = common_size; i < old_size; ++i) {
        path_stack_[path_depth] = PathElement{i};
        collect_removed_optimized(old_vec[i], path_depth + 1);
    }

    // Added tail elements
    for (size_t i = common_size; i < new_size; ++i) {
        path_stack_[path_depth] = PathElement{i};
        collect_added_optimized(new_vec[i], path_depth + 1);
    }
}

void DiffEntryCollector::collect_entries_optimized(const Value& val, std::size_t path_depth, bool is_add) {
    // In shallow mode, just record the value at current path (no recursion)
    if (!recursive_) {
        Path current_path(current_path_view(path_depth));
        if (is_add) {
            diffs_.emplace_back(DiffEntry::Type::Add, std::move(current_path), val, val);
        } else {
            diffs_.emplace_back(DiffEntry::Type::Remove, std::move(current_path), val, val);
        }
        return;
    }

    // Recursive mode: descend into containers
    // Container Boxing: variant now holds BoxedValueMap, BoxedValueVector, etc.
    std::visit(
        [&](const auto& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BoxedValueMap>) {
                // Ensure path stack has enough capacity for next level
                if (path_stack_.size() <= path_depth) {
                    path_stack_.resize(path_depth + 1);
                }
                // Container Boxing: map now stores Value directly
                for (const auto& [k, v] : arg.get()) {
                    path_stack_[path_depth] = PathElement{k};
                    collect_entries_optimized(v, path_depth + 1, is_add);
                }
            } else if constexpr (std::is_same_v<T, BoxedValueVector>) {
                // Ensure path stack has enough capacity for next level
                if (path_stack_.size() <= path_depth) {
                    path_stack_.resize(path_depth + 1);
                }
                // Container Boxing: vector now stores Value directly
                const auto& vec = arg.get();
                for (size_t i = 0; i < vec.size(); ++i) {
                    path_stack_[path_depth] = PathElement{i};
                    collect_entries_optimized(vec[i], path_depth + 1, is_add);
                }
            } else if constexpr (!std::is_same_v<T, std::monostate>) {
                // Leaf value: record it - both fields reference the same value
                Path current_path(current_path_view(path_depth));
                diffs_.emplace_back(is_add ? DiffEntry::Type::Add : DiffEntry::Type::Remove, std::move(current_path),
                                    val, val);
            }
        },
        val.data);
}

void DiffEntryCollector::collect_removed_optimized(const Value& val, std::size_t path_depth) {
    collect_entries_optimized(val, path_depth, false);
}

void DiffEntryCollector::collect_added_optimized(const Value& val, std::size_t path_depth) {
    collect_entries_optimized(val, path_depth, true);
}

Value DiffEntryCollector::as_value_tree() const {
    if (diffs_.empty()) {
        return Value{}; // Empty tree for no changes
    }

    // Build tree by inserting each diff entry's path
    // Leaf nodes store pointer to DiffEntry as uint64_t
    Value tree = Value{ValueMap{}};

    for (const auto& entry : diffs_) {
        // Encode pointer as uint64_t
        const uint64_t ptr_val = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&entry));
        Value leaf{ptr_val};

        // Insert at path
        if (entry.path.empty()) {
            // Root-level change
            tree = leaf;
        } else {
            // Use set_at_path_vivify to build the tree structure with auto-vivification
            tree = set_at_path_vivify(tree, entry.path, std::move(leaf));
        }
    }

    return tree;
}

bool has_any_difference(const Value& old_val, const Value& new_val, bool recursive) {
    // Fast path: same object
    if (&old_val.data == &new_val.data) {
        return false;
    }
    return detail::values_differ(old_val, new_val, recursive);
}

namespace detail {

bool values_differ(const Value& old_val, const Value& new_val, bool recursive) {
    // Fast path: different types
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();
    if (old_index != new_index) [[unlikely]] {
        return true;
    }

    // Container Boxing: variant now holds BoxedValueMap, BoxedValueVector, etc.
    return std::visit(
        [&](const auto& old_arg) -> bool {
            using T = std::decay_t<decltype(old_arg)>;

            if constexpr (std::is_same_v<T, BoxedValueMap>) {
                const auto& new_boxed = std::get<BoxedValueMap>(new_val.data);
                const auto& old_map = old_arg.get();
                const auto& new_map = new_boxed.get();
                // O(1) identity check
                if (old_map.impl().root == new_map.impl().root && old_map.impl().size == new_map.impl().size)
                    [[likely]] {
                    return false;
                }
                // Shallow mode: identity check failed, containers are different
                if (!recursive) {
                    return true;
                }
                return maps_differ(old_map, new_map, recursive);
            } else if constexpr (std::is_same_v<T, BoxedValueVector>) {
                const auto& new_boxed = std::get<BoxedValueVector>(new_val.data);
                const auto& old_vec = old_arg.get();
                const auto& new_vec = new_boxed.get();
                // O(1) identity check
                if (old_vec.impl().root == new_vec.impl().root && old_vec.impl().tail == new_vec.impl().tail &&
                    old_vec.impl().size == new_vec.impl().size) [[likely]] {
                    return false;
                }
                // Shallow mode: identity check failed, containers are different
                if (!recursive) {
                    return true;
                }
                return vectors_differ(old_vec, new_vec, recursive);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                return false; // Both null
            } else {
                // Primitive types: direct comparison
                const auto& new_arg = std::get<T>(new_val.data);
                return old_arg != new_arg;
            }
        },
        old_val.data);
}

bool maps_differ(const ValueMap& old_map, const ValueMap& new_map, bool recursive) {
    // Quick size check
    if (old_map.size() != new_map.size()) {
        return true;
    }

    // Use immer::diff with early exit
    // Container Boxing: ValueMap now stores Value directly, not ValueBox
    bool found_difference = false;

    auto differ = immer::make_differ(
        // added - any addition means difference
        [&](const std::pair<const std::string, Value>&) { found_difference = true; },
        // removed - any removal means difference
        [&](const std::pair<const std::string, Value>&) { found_difference = true; },
        // retained - check if values differ
        [&](const std::pair<const std::string, Value>& old_kv,
            const std::pair<const std::string, Value>& new_kv) {
            if (found_difference)
                return; // Already found, skip

            // Recurse
            if (values_differ(old_kv.second, new_kv.second, recursive)) {
                found_difference = true;
            }
        });

    immer::diff(old_map, new_map, differ);
    return found_difference;
}

bool vectors_differ(const ValueVector& old_vec, const ValueVector& new_vec, bool recursive) {
    const size_t old_size = old_vec.size();
    const size_t new_size = new_vec.size();

    // Size difference means there's a change
    if (old_size != new_size) {
        return true;
    }

    // Container Boxing: ValueVector now stores Value directly, not ValueBox
    // Check each element (early exit on first difference)
    for (size_t i = 0; i < old_size; ++i) {
        const Value& old_val = old_vec[i];
        const Value& new_val = new_vec[i];

        // Recurse - early exit if different
        if (values_differ(old_val, new_val, recursive)) {
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
    case DiffEntry::Type::Add:
        return add_box;
    case DiffEntry::Type::Remove:
        return remove_box;
    case DiffEntry::Type::Change:
        return change_box;
    }
    return add_box; // fallback (should never reach)
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
} // namespace

void DiffValueCollector::diff(const Value& old_val, const Value& new_val, bool recursive) {
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

void DiffValueCollector::clear() {
    result_ = Value{};
    has_changes_ = false;
}

Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const ValueBox& val_box) {
    // Use transient for better performance (in-place mutation)
    auto transient = ValueMap{}.transient();

    // Use cached type box to avoid repeated allocation
    transient.set(diff_keys::TYPE, get_type_box(type));

    // Store the value box directly - no copy, just reference count increment
    // Add: only _new (value being added)
    // Remove: only _old (value being removed)
    // Change: should not use this overload - use the two-value version
    if (type == DiffEntry::Type::Add) {
        transient.set(diff_keys::NEW, val_box);
    } else if (type == DiffEntry::Type::Remove) {
        transient.set(diff_keys::OLD, val_box);
    }

    return Value{transient.persistent()};
}

Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const ValueBox& old_box, const ValueBox& new_box) {
    // For Change type, we need both values - use transient for better performance
    if (type == DiffEntry::Type::Change) {
        auto transient = ValueMap{}.transient();
        transient.set(diff_keys::TYPE, get_type_box(type));
        transient.set(diff_keys::OLD, old_box);
        transient.set(diff_keys::NEW, new_box);
        return Value{transient.persistent()};
    }

    // For Add/Remove, delegate to single-value version using the meaningful value
    return make_diff_node(type, (type == DiffEntry::Type::Add) ? new_box : old_box);
}

// Convenience overloads for Value& (wraps into ValueBox)
Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const Value& val) {
    return make_diff_node(type, ValueBox{val});
}

Value DiffValueCollector::make_diff_node(DiffEntry::Type type, const Value& old_val, const Value& new_val) {
    return make_diff_node(type, ValueBox{old_val}, ValueBox{new_val});
}

Value DiffValueCollector::diff_value_impl(const Value& old_val, const Value& new_val, bool& changed) {
    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();

    // Different types - whole value changed
    if (old_index != new_index) [[unlikely]] {
        changed = true;
        return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
    }

    // Container Boxing: variant now holds BoxedValueMap, BoxedValueVector, etc.
    return std::visit(
        [&](const auto& old_arg) -> Value {
            using T = std::decay_t<decltype(old_arg)>;

            if constexpr (std::is_same_v<T, BoxedValueMap>) {
                const auto& new_boxed = std::get<BoxedValueMap>(new_val.data);
                const auto& old_map = old_arg.get();
                const auto& new_map = new_boxed.get();
                // O(1) identity check
                if (old_map.impl().root == new_map.impl().root && old_map.impl().size == new_map.impl().size)
                    [[likely]] {
                    return Value{}; // No change
                }
                // Shallow mode: report container change without recursing
                if (!recursive_) {
                    changed = true;
                    return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
                }
                return diff_map_impl(old_map, new_map, changed);
            } else if constexpr (std::is_same_v<T, BoxedValueVector>) {
                const auto& new_boxed = std::get<BoxedValueVector>(new_val.data);
                const auto& old_vec = old_arg.get();
                const auto& new_vec = new_boxed.get();
                // O(1) identity check
                if (old_vec.impl().root == new_vec.impl().root && old_vec.impl().tail == new_vec.impl().tail &&
                    old_vec.impl().size == new_vec.impl().size) [[likely]] {
                    return Value{}; // No change
                }
                // Shallow mode: report container change without recursing
                if (!recursive_) {
                    changed = true;
                    return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
                }
                return diff_vector_impl(old_vec, new_vec, changed);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                return Value{}; // Both null, no change
            } else {
                // Primitive types: direct comparison
                const auto& new_arg = std::get<T>(new_val.data);
                if (old_arg != new_arg) {
                    changed = true;
                    return make_diff_node(DiffEntry::Type::Change, old_val, new_val);
                }
                return Value{}; // No change
            }
        },
        old_val.data);
}

// ValueBox version for zero-copy when we already have boxes from container traversal
// Note: With Container Boxing, this is primarily used for TableEntry which still uses ValueBox
Value DiffValueCollector::diff_value_impl_box(const ValueBox& old_box, const ValueBox& new_box, bool& changed) {
    const Value& old_val = *old_box;
    const Value& new_val = *new_box;

    const auto old_index = old_val.data.index();
    const auto new_index = new_val.data.index();

    // Different types - whole value changed, use the boxes directly
    if (old_index != new_index) [[unlikely]] {
        changed = true;
        return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
    }

    // Container Boxing: variant now holds BoxedValueMap, BoxedValueVector, etc.
    return std::visit(
        [&](const auto& old_arg) -> Value {
            using T = std::decay_t<decltype(old_arg)>;

            if constexpr (std::is_same_v<T, BoxedValueMap>) {
                const auto& new_boxed = std::get<BoxedValueMap>(new_val.data);
                const auto& old_map = old_arg.get();
                const auto& new_map = new_boxed.get();
                if (old_map.impl().root == new_map.impl().root && old_map.impl().size == new_map.impl().size)
                    [[likely]] {
                    return Value{};
                }
                if (!recursive_) {
                    changed = true;
                    return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
                }
                return diff_map_impl(old_map, new_map, changed);
            } else if constexpr (std::is_same_v<T, BoxedValueVector>) {
                const auto& new_boxed = std::get<BoxedValueVector>(new_val.data);
                const auto& old_vec = old_arg.get();
                const auto& new_vec = new_boxed.get();
                if (old_vec.impl().root == new_vec.impl().root && old_vec.impl().tail == new_vec.impl().tail &&
                    old_vec.impl().size == new_vec.impl().size) [[likely]] {
                    return Value{};
                }
                if (!recursive_) {
                    changed = true;
                    return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
                }
                return diff_vector_impl(old_vec, new_vec, changed);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                return Value{};
            } else {
                const auto& new_arg = std::get<T>(new_val.data);
                if (old_arg != new_arg) {
                    changed = true;
                    return make_diff_node(DiffEntry::Type::Change, old_box, new_box);
                }
                return Value{};
            }
        },
        old_val.data);
}

Value DiffValueCollector::diff_map_impl(const ValueMap& old_map, const ValueMap& new_map, bool& changed) {
    // Use transient for O(n) batch updates instead of O(n log n) with chained .set()
    auto transient = ValueMap{}.transient();
    bool has_any_change = false;

    // Container Boxing: ValueMap now stores Value directly, not ValueBox
    auto map_differ = immer::make_differ(
        // added - Value stored directly in map
        [&](const std::pair<const std::string, Value>& added_kv) {
            has_any_change = true;
            Value subtree = collect_entries(added_kv.second, DiffEntry::Type::Add);
            transient.set(added_kv.first, std::move(subtree));
        },
        // removed - Value stored directly in map
        [&](const std::pair<const std::string, Value>& removed_kv) {
            has_any_change = true;
            Value subtree = collect_entries(removed_kv.second, DiffEntry::Type::Remove);
            transient.set(removed_kv.first, std::move(subtree));
        },
        // changed (retained key)
        [&](const std::pair<const std::string, Value>& old_kv,
            const std::pair<const std::string, Value>& new_kv) {
            // Compare value data addresses for identity check
            if (&old_kv.second.data == &new_kv.second.data) [[likely]] {
                return;
            }
            bool subtree_changed = false;
            Value subtree = diff_value_impl(old_kv.second, new_kv.second, subtree_changed);
            if (subtree_changed) {
                has_any_change = true;
                transient.set(old_kv.first, std::move(subtree));
            }
        });

    immer::diff(old_map, new_map, map_differ);

    if (has_any_change) {
        changed = true;
        return Value{BoxedValueMap{transient.persistent()}};
    }
    return Value{};
}

Value DiffValueCollector::diff_vector_impl(const ValueVector& old_vec, const ValueVector& new_vec, bool& changed) {
    const size_t old_size = old_vec.size();
    const size_t new_size = new_vec.size();
    const size_t common_size = std::min(old_size, new_size);

    // Use transient for O(n) batch updates instead of O(n log n) with chained .set()
    // Note: Vector diffs are stored as a map with string keys for indices
    auto transient = ValueMap{}.transient();
    bool has_any_change = false;

    // Container Boxing: ValueVector now stores Value directly
    // Compare common elements
    for (size_t i = 0; i < common_size; ++i) {
        const Value& old_val = old_vec[i];
        const Value& new_val = new_vec[i];

        // Compare value data addresses for identity check
        if (&old_val.data == &new_val.data) [[likely]] {
            continue;
        }

        bool subtree_changed = false;
        Value subtree = diff_value_impl(old_val, new_val, subtree_changed);
        if (subtree_changed) {
            has_any_change = true;
            transient.set(get_index_string(i), std::move(subtree));
        }
    }

    // Removed tail elements
    for (size_t i = common_size; i < old_size; ++i) {
        has_any_change = true;
        Value subtree = collect_entries(old_vec[i], DiffEntry::Type::Remove);
        transient.set(get_index_string(i), std::move(subtree));
    }

    // Added tail elements
    for (size_t i = common_size; i < new_size; ++i) {
        has_any_change = true;
        Value subtree = collect_entries(new_vec[i], DiffEntry::Type::Add);
        transient.set(get_index_string(i), std::move(subtree));
    }

    if (has_any_change) {
        changed = true;
        return Value{BoxedValueMap{transient.persistent()}};
    }
    return Value{};
}

Value DiffValueCollector::collect_entries(const Value& val, DiffEntry::Type type) {
    // In shallow mode, just create a diff node for the entire value
    // Use single-value make_diff_node which stores val in the appropriate field
    if (!recursive_) {
        return make_diff_node(type, val);
    }

    // Recursive mode: descend into containers
    // Container Boxing: variant now holds BoxedValueMap, BoxedValueVector, etc.
    return std::visit(
        [&](const auto& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BoxedValueMap>) {
                // Use transient for O(n) batch updates
                auto transient = ValueMap{}.transient();
                // Container Boxing: map stores Value directly
                for (const auto& [k, v] : arg.get()) {
                    Value subtree = collect_entries(v, type);
                    transient.set(k, std::move(subtree));
                }
                return Value{BoxedValueMap{transient.persistent()}};
            } else if constexpr (std::is_same_v<T, BoxedValueVector>) {
                // Use transient for O(n) batch updates
                // Note: Vector diffs are stored as a map with string keys for indices
                auto transient = ValueMap{}.transient();
                // Container Boxing: vector stores Value directly
                const auto& vec = arg.get();
                for (size_t i = 0; i < vec.size(); ++i) {
                    Value subtree = collect_entries(vec[i], type);
                    transient.set(get_index_string(i), std::move(subtree));
                }
                return Value{BoxedValueMap{transient.persistent()}};
            } else if constexpr (!std::is_same_v<T, std::monostate>) {
                // Leaf value: create diff node - use single-value version
                return make_diff_node(type, val);
            } else {
                return Value{}; // monostate - nothing to collect
            }
        },
        val.data);
}

bool DiffValueCollector::is_diff_node(const Value& val) {
    if (auto* boxed_map = val.get_if<BoxedValueMap>()) {
        return boxed_map->get().count(diff_keys::TYPE) > 0;
    }
    return false;
}

DiffEntry::Type DiffValueCollector::get_diff_type(const Value& val) {
    if (auto* boxed_map = val.get_if<BoxedValueMap>()) {
        const auto& m = boxed_map->get();
        if (auto* type_val_ptr = m.find(diff_keys::TYPE)) {
            if (auto* type_val = type_val_ptr->get_if<uint8_t>()) {
                if (*type_val <= static_cast<uint8_t>(DiffEntry::Type::Change)) {
                    return static_cast<DiffEntry::Type>(*type_val);
                }
            }
        }
    }
    return DiffEntry::Type::Add;
}

Value DiffValueCollector::get_old_value(const Value& val) {
    if (auto* boxed_map = val.get_if<BoxedValueMap>()) {
        const auto& m = boxed_map->get();
        if (auto* old_val_ptr = m.find(diff_keys::OLD)) {
            return *old_val_ptr;
        }
    }
    return Value{};
}

Value DiffValueCollector::get_new_value(const Value& val) {
    if (auto* boxed_map = val.get_if<BoxedValueMap>()) {
        const auto& m = boxed_map->get();
        if (auto* new_val_ptr = m.find(diff_keys::NEW)) {
            return *new_val_ptr;
        }
    }
    return Value{};
}

void DiffValueCollector::print() const {
    std::cout << "DiffValueCollector tree:\n";
    if (!has_changes_) {
        std::cout << "  (no changes)\n";
        return;
    }
    print_value(result_, "  ");
}

Value diff_as_value(const Value& old_val, const Value& new_val, bool recursive) {
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
                throw std::runtime_error("apply_diff: Add operation missing _new value at path: " +
                                         path.to_dot_notation());
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
                throw std::runtime_error("apply_diff: Change operation missing _new value at path: " +
                                         path.to_dot_notation());
            }
            return new_val;
        }
        }
    }

    // This is an intermediate node - recurse into its children
    // Container Boxing: diff_tree should be a BoxedValueMap
    if (auto* diff_boxed_map = diff_tree.get_if<BoxedValueMap>()) {
        const auto& diff_map = diff_boxed_map->get();
        
        // The diff tree is a map - apply changes to the corresponding structure in root
        if (auto* root_boxed_map = root.get_if<BoxedValueMap>()) {
            const auto& root_map = root_boxed_map->get();
            // Root is also a map - merge changes using transient for O(n) batch updates
            auto transient = root_map.transient();

            // Container Boxing: ValueMap now stores Value directly
            for (const auto& [key, diff_child] : diff_map) {
                Path child_path = path;
                child_path.push_back(key);

                // Get the current value at this key (or null if not present)
                Value current_val;
                if (auto* existing_val_ptr = root_map.find(key)) {
                    current_val = *existing_val_ptr;
                } else {
                    current_val = Value{}; // null for missing keys
                }

                // Recursively apply diff to this child
                Value new_child = apply_diff_recursive(current_val, diff_child, child_path);

                // Update or remove the key based on the result
                if (new_child.is_null()) {
                    // Remove the key if the result is null
                    transient.erase(key);
                } else {
                    // Set the new value - Container Boxing: store Value directly
                    transient.set(key, std::move(new_child));
                }
            }

            return Value{BoxedValueMap{transient.persistent()}};
        } else if (root.is_null()) {
            // Root is null but we have changes to apply - create a new map using transient
            auto transient = ValueMap{}.transient();

            // Container Boxing: ValueMap now stores Value directly
            for (const auto& [key, diff_child] : diff_map) {
                Path child_path = path;
                child_path.push_back(key);

                // Apply diff to null value (effectively adding new keys)
                Value new_child = apply_diff_recursive(Value{}, diff_child, child_path);

                if (!new_child.is_null()) {
                    transient.set(key, std::move(new_child));
                }
            }

            return Value{BoxedValueMap{transient.persistent()}};
        } else {
            // Check if this looks like a vector diff (all keys are numeric strings)
            bool all_numeric = true;
            for (const auto& [key, _] : diff_map) {
                try {
                    [[maybe_unused]] size_t parsed = std::stoul(key);
                } catch (...) {
                    all_numeric = false;
                    break;
                }
            }

            if (all_numeric) {
                // This is a vector diff represented as a map with numeric string keys
                if (auto* root_boxed_vec = root.get_if<BoxedValueVector>()) {
                    const auto& root_vec = root_boxed_vec->get();
                    // Root is a vector - apply indexed changes using transient
                    auto transient = root_vec.transient();
                    size_t current_size = root_vec.size();

                    // Container Boxing: ValueVector now stores Value directly
                    for (const auto& [index_str, diff_child] : diff_map) {
                        size_t index = std::stoul(index_str);
                        Path child_path = path;
                        child_path.push_back(index);

                        // Get the current value at this index (or null if out of bounds)
                        Value current_val;
                        if (index < current_size) {
                            current_val = root_vec[index];
                        } else {
                            current_val = Value{}; // null for out-of-bounds indices
                        }

                        // Recursively apply diff to this child
                        Value new_child = apply_diff_recursive(current_val, diff_child, child_path);

                        // Update the vector
                        if (!new_child.is_null()) {
                            // Extend vector if necessary
                            while (transient.size() <= index) {
                                transient.push_back(Value{});
                            }
                            transient.set(index, std::move(new_child));
                        } else if (index < transient.size()) {
                            // For removal, set it to null to preserve indices
                            transient.set(index, Value{});
                        }
                    }

                    return Value{BoxedValueVector{transient.persistent()}};
                } else if (root.is_null()) {
                    // Root is null but we have vector changes to apply
                    auto transient = ValueVector{}.transient();

                    // Collect all indices and sort them
                    std::vector<std::pair<size_t, const Value*>> indexed_diffs;
                    for (const auto& [index_str, diff_child] : diff_map) {
                        size_t index = std::stoul(index_str);
                        indexed_diffs.emplace_back(index, &diff_child);
                    }
                    std::sort(indexed_diffs.begin(), indexed_diffs.end());

                    // Apply changes in order
                    for (const auto& [index, diff_child] : indexed_diffs) {
                        Path child_path = path;
                        child_path.push_back(index);

                        Value new_child = apply_diff_recursive(Value{}, *diff_child, child_path);

                        if (!new_child.is_null()) {
                            // Extend vector if necessary
                            while (transient.size() <= index) {
                                transient.push_back(Value{});
                            }
                            transient.set(index, std::move(new_child));
                        }
                    }

                    return Value{BoxedValueVector{transient.persistent()}};
                } else {
                    throw std::runtime_error("apply_diff: Type mismatch - diff expects vector but root "
                                             "is not a vector at path: " +
                                             path.to_dot_notation());
                }
            }
            
            throw std::runtime_error("apply_diff: Type mismatch - diff expects map but root is not a map at path: " +
                                     path.to_dot_notation());
        }
    }

    // If we reach here, the diff_tree structure doesn't match expected patterns
    throw std::runtime_error("apply_diff: Unexpected diff tree structure at path: " + path.to_dot_notation());
}
} // namespace

Value apply_diff(const Value& root, const Value& diff_tree) {
    // Handle empty diff
    if (diff_tree.is_null()) {
        return root; // No changes to apply
    }

    // Check if diff is empty (no changes recorded)
    // Container Boxing: use BoxedValueMap
    if (auto* diff_boxed_map = diff_tree.get_if<BoxedValueMap>()) {
        if (diff_boxed_map->get().empty()) {
            return root; // Empty diff, no changes
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
