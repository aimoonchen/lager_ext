// value_diff.h - Diff operations for ImmerValue types

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/value.h>

#include <optional>
#include <vector>

namespace lager_ext {

struct DiffEntry {
    enum class Type { Add, Remove, Change };

    Type type;
    Path path;          // Path to the changed value
    ValueBox old_value; // Old value box (meaningful for Remove and Change) - zero-copy reference
    ValueBox new_value; // New value box (meaningful for Add and Change) - zero-copy reference

    // Note: For Add, old_value references the actual value being added (stored in new_value).
    //       For Remove, new_value references the actual value being removed (stored in old_value).
    //       Using ValueBox ensures zero-copy by only incrementing reference count.

    // Constructor for emplace_back optimization with ImmerValue& (wraps into ValueBox)
    DiffEntry(Type t, const Path& p, const ImmerValue& old_v, const ImmerValue& new_v)
        : type(t), path(p), old_value(ValueBox{old_v}), new_value(ValueBox{new_v}) {}

    // Constructor for emplace_back optimization with ValueBox (zero-copy)
    DiffEntry(Type t, const Path& p, const ValueBox& old_box, const ValueBox& new_box)
        : type(t), path(p), old_value(old_box), new_value(new_box) {}

    // Default constructor
    DiffEntry() : type(Type::Add), old_value(ValueBox{ImmerValue{}}), new_value(ValueBox{ImmerValue{}}) {}

    /// Get the value that was added/removed/changed (convenience accessor)
    /// For Add: returns new_value
    /// For Remove: returns old_value
    /// For Change: returns new_value (use old_value for the previous value)
    [[nodiscard]] const ImmerValue& value() const { return (type == Type::Remove) ? *old_value : *new_value; }

    /// Get old value as const reference
    [[nodiscard]] const ImmerValue& get_old() const { return *old_value; }

    /// Get new value as const reference
    [[nodiscard]] const ImmerValue& get_new() const { return *new_value; }
};

/// Special keys used in DiffValueCollector leaf nodes
namespace diff_keys {
inline constexpr const char* TYPE = "_diff_type";
inline constexpr const char* OLD = "_old";
inline constexpr const char* NEW = "_new";
} // namespace diff_keys

// ============================================================
// DiffNodeView - Lightweight accessor for diff nodes
//
// Use this when you need to access the same diff node multiple times.
// It parses the node once and provides O(1) access to all fields,
// avoiding repeated hash lookups.
//
// Example:
//   DiffNodeView view;
//   if (view.parse(node)) {
//       switch (view.type) {
//           case DiffEntry::Type::Add:
//               process(*view.new_value);
//               break;
//           case DiffEntry::Type::Remove:
//               process(*view.old_value);
//               break;
//           case DiffEntry::Type::Change:
//               process(*view.old_value, *view.new_value);
//               break;
//       }
//   }
// ============================================================

struct DiffNodeView {
    DiffEntry::Type type = DiffEntry::Type::Add;
    const ImmerValue* old_value = nullptr; ///< Pointer to old value (valid for Remove/Change)
    const ImmerValue* new_value = nullptr; ///< Pointer to new value (valid for Add/Change)

    /// Parse a diff node ImmerValue into this view
    /// @param val The ImmerValue to parse (should be a diff leaf node from DiffValueCollector)
    /// @return true if val is a valid diff node, false otherwise
    ///
    /// After successful parse, access type/old_value/new_value directly (O(1))
    /// The pointers remain valid as long as the original ImmerValue is alive.
    bool parse(const ImmerValue& val) {
        // Container Boxing: get the boxed map first, then access the raw map
        auto* boxed_m = val.get_if<BoxedValueMap>();
        if (!boxed_m)
            return false;
        const auto& m = boxed_m->get();

        // Look up type (required field)
        // Container Boxing: ValueMap stores ImmerValue directly now
        auto* type_ptr = m.find(diff_keys::TYPE);
        if (!type_ptr)
            return false;

        auto* type_val = type_ptr->get_if<uint8_t>();
        if (!type_val || *type_val > static_cast<uint8_t>(DiffEntry::Type::Change)) {
            return false;
        }

        type = static_cast<DiffEntry::Type>(*type_val);

        // Look up old value (optional, present for Remove/Change)
        // Container Boxing: ValueMap stores ImmerValue directly
        if (auto* old_ptr = m.find(diff_keys::OLD)) {
            old_value = old_ptr;
        } else {
            old_value = nullptr;
        }

        // Look up new value (optional, present for Add/Change)
        // Container Boxing: ValueMap stores ImmerValue directly
        if (auto* new_ptr = m.find(diff_keys::NEW)) {
            new_value = new_ptr;
        } else {
            new_value = nullptr;
        }

        return true;
    }

    /// Check if old_value is available
    [[nodiscard]] bool has_old() const { return old_value != nullptr; }

    /// Check if new_value is available
    [[nodiscard]] bool has_new() const { return new_value != nullptr; }

    /// Get old value (throws if not available)
    [[nodiscard]] const ImmerValue& get_old() const {
        if (!old_value)
            throw std::runtime_error("DiffNodeView: old_value not available");
        return *old_value;
    }

    /// Get new value (throws if not available)
    [[nodiscard]] const ImmerValue& get_new() const {
        if (!new_value)
            throw std::runtime_error("DiffNodeView: new_value not available");
        return *new_value;
    }

    /// Get the meaningful value based on type
    /// - Add: returns new_value
    /// - Remove: returns old_value
    /// - Change: returns new_value
    [[nodiscard]] const ImmerValue& value() const { return (type == DiffEntry::Type::Remove) ? get_old() : get_new(); }
};

// ============================================================
// DiffEntryCollector - Collects diff as a flat list of DiffEntry
//
// Use this when you need a flat list of all changes for processing.
// Each DiffEntry contains the path, old value, new value, and change type.
// ============================================================

class LAGER_EXT_API DiffEntryCollector {
private:
    std::vector<DiffEntry> diffs_;
    bool recursive_ = true;

    // OPTIMIZATION: Path stack for zero-allocation path building
    mutable std::vector<PathElement> path_stack_;

    // Optimized methods using path stack (no path copying)
    void diff_value_optimized(const ImmerValue& old_val, const ImmerValue& new_val, std::size_t path_depth);
    void diff_map_optimized(const ValueMap& old_map, const ValueMap& new_map, std::size_t path_depth);
    void diff_vector_optimized(const ValueVector& old_vec, const ValueVector& new_vec, std::size_t path_depth);
    void collect_entries_optimized(const ImmerValue& val, std::size_t path_depth, bool is_add);
    void collect_removed_optimized(const ImmerValue& val, std::size_t path_depth);
    void collect_added_optimized(const ImmerValue& val, std::size_t path_depth);

    // Helper: create PathView from current path stack
    [[nodiscard]] PathView current_path_view(std::size_t depth) const noexcept {
        return PathView{path_stack_.data(), depth};
    }

public:
    void diff(const ImmerValue& old_val, const ImmerValue& new_val, bool recursive = true);
    [[nodiscard]] const std::vector<DiffEntry>& get_diffs() const;
    void clear();
    [[nodiscard]] bool has_changes() const;
    [[nodiscard]] bool is_recursive() const { return recursive_; }
    void print_diffs() const;

    /// Build a ImmerValue tree from collected diffs
    ///
    /// Leaf nodes store pointers to DiffEntry as uint64_t for maximum performance.
    /// This is much faster than DiffValueCollector because:
    /// - Leaf nodes are 8 bytes vs ~100+ bytes (ValueMap)
    /// - No copying of old/new values - just pointer references
    ///
    /// @warning The returned ImmerValue is only valid while this DiffEntryCollector
    ///          (and its diffs_) remains alive and unchanged!
    ///
    /// To access a leaf node:
    /// @code
    ///   if (auto* node = tree.get_at_path("users", 0, "name")) {
    ///       if (auto* entry = DiffEntryCollector::get_entry(*node)) {
    ///           // Use entry->type, entry->get_old(), entry->get_new()
    ///       }
    ///   }
    /// @endcode
    [[nodiscard]] ImmerValue as_value_tree() const;

    /// Check if a ImmerValue node is a DiffEntry leaf (contains pointer)
    [[nodiscard]] static bool is_entry_node(const ImmerValue& node) { return node.is<uint64_t>(); }

    /// Extract DiffEntry pointer from a leaf node
    /// @param node A leaf node from as_value_tree() result
    /// @return Pointer to DiffEntry, or nullptr if not a valid leaf
    [[nodiscard]] static const DiffEntry* get_entry(const ImmerValue& node) {
        if (auto* ptr_val = node.get_if<uint64_t>()) {
            return reinterpret_cast<const DiffEntry*>(static_cast<uintptr_t>(*ptr_val));
        }
        return nullptr;
    }
};

// ============================================================
// DiffValueCollector - Collects diff directly as a ImmerValue tree
//
// More efficient than DiffEntryCollector + DiffValue as it builds
// the tree structure during the diff traversal (single pass).
//
// Structure:
//   - Leaf nodes with changes contain a map with special keys:
//     {
//       "_diff_type": <uint8_t: 0=Add, 1=Remove, 2=Change>,
//       "_old": <old value>,    // present for "remove" and "change"
//       "_new": <new value>     // present for "add" and "change"
//     }
//   - Intermediate nodes mirror the original structure (map/vector)
// ============================================================

class LAGER_EXT_API DiffValueCollector {
private:
    ImmerValue result_;
    bool has_changes_ = false;
    bool recursive_ = true;

    // Core diff functions that return the diff subtree for that node
    ImmerValue diff_value_impl(const ImmerValue& old_val, const ImmerValue& new_val, bool& changed);
    ImmerValue diff_value_impl_box(const ValueBox& old_box, const ValueBox& new_box, bool& changed);
    ImmerValue diff_map_impl(const ValueMap& old_map, const ValueMap& new_map, bool& changed);
    ImmerValue diff_vector_impl(const ValueVector& old_vec, const ValueVector& new_vec, bool& changed);

    // Collect all entries under a container (for add/remove of entire subtree)
    ImmerValue collect_entries(const ImmerValue& val, DiffEntry::Type type);
    ImmerValue collect_entries_box(const ValueBox& val_box, DiffEntry::Type type);

    // Create a leaf diff node using ValueBox directly (zero-copy for referenced values)
    // Preferred when we have access to the original ValueBox from container traversal
    static ImmerValue make_diff_node(DiffEntry::Type type, const ValueBox& val_box);
    static ImmerValue make_diff_node(DiffEntry::Type type, const ValueBox& old_box, const ValueBox& new_box);

    // Convenience overloads for ImmerValue& (creates temporary ValueBox)
    static ImmerValue make_diff_node(DiffEntry::Type type, const ImmerValue& val);
    static ImmerValue make_diff_node(DiffEntry::Type type, const ImmerValue& old_val, const ImmerValue& new_val);

public:
    DiffValueCollector() = default;

    /// Compute diff and organize as ImmerValue tree in a single pass
    void diff(const ImmerValue& old_val, const ImmerValue& new_val, bool recursive = true);

    /// Get the diff result as a ImmerValue tree
    /// Returns an empty map if no differences found
    [[nodiscard]] const ImmerValue& get() const { return result_; }

    /// Check if any changes were detected
    [[nodiscard]] bool has_changes() const { return has_changes_; }

    /// Check if recursive mode was used
    [[nodiscard]] bool is_recursive() const { return recursive_; }

    /// Clear the result
    void clear();

    /// Check if a path in the diff result is a diff leaf node
    /// (contains _diff_type key)
    [[nodiscard]] static bool is_diff_node(const ImmerValue& val);

    /// Extract diff type from a diff leaf node as DiffEntry::Type
    /// @pre val must be a diff node (verified by is_diff_node())
    /// @return The diff type. Returns DiffEntry::Type::Add if not a valid diff node.
    [[nodiscard]] static DiffEntry::Type get_diff_type(const ImmerValue& val);

    /// Extract old value from a diff leaf node
    /// Returns null ImmerValue if not present
    [[nodiscard]] static ImmerValue get_old_value(const ImmerValue& val);

    /// Extract new value from a diff leaf node
    /// Returns null ImmerValue if not present
    [[nodiscard]] static ImmerValue get_new_value(const ImmerValue& val);

    /// Print the diff tree in a readable format
    void print() const;
};

[[nodiscard]] LAGER_EXT_API bool has_any_difference(const ImmerValue& old_val, const ImmerValue& new_val, bool recursive = true);

namespace detail {
[[nodiscard]] bool values_differ(const ImmerValue& old_val, const ImmerValue& new_val, bool recursive);
[[nodiscard]] bool maps_differ(const ValueMap& old_map, const ValueMap& new_map, bool recursive);
[[nodiscard]] bool vectors_differ(const ValueVector& old_vec, const ValueVector& new_vec, bool recursive);
} // namespace detail

/// Convenience function to compute diff as a ImmerValue tree (uses DiffValueCollector)
[[nodiscard]] LAGER_EXT_API ImmerValue diff_as_value(const ImmerValue& old_val, const ImmerValue& new_val, bool recursive = true);

/// Apply a diff tree to a ImmerValue, creating a new ImmerValue with the changes applied
/// @param root The base ImmerValue to apply changes to
/// @param diff_tree The diff tree (created by DiffValueCollector or diff_as_value)
/// @return A new ImmerValue with the diff applied
///
/// The diff_tree should have the structure produced by DiffValueCollector:
/// - Leaf nodes with changes contain special keys (_diff_type, _old, _new)
/// - Intermediate nodes mirror the original structure
///
/// This function leverages ImmerValue::set methods for efficient incremental updates.
[[nodiscard]] LAGER_EXT_API ImmerValue apply_diff(const ImmerValue& root, const ImmerValue& diff_tree);

} // namespace lager_ext
