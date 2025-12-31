// value_diff.h - Diff operations for Value types

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/value.h>
#include <optional>
#include <vector>

namespace lager_ext {

struct DiffEntry {
    enum class Type { Add, Remove, Change };

    Type type;
    Path path;              // Path to the changed value
    ValueBox old_value;     // Old value box (meaningful for Remove and Change) - zero-copy reference
    ValueBox new_value;     // New value box (meaningful for Add and Change) - zero-copy reference
    
    // Note: For Add, old_value references the actual value being added (stored in new_value).
    //       For Remove, new_value references the actual value being removed (stored in old_value).
    //       Using ValueBox ensures zero-copy by only incrementing reference count.

    // Constructor for emplace_back optimization with Value& (wraps into ValueBox)
    DiffEntry(Type t, const Path& p, const Value& old_v, const Value& new_v)
        : type(t), path(p), old_value(ValueBox{old_v}), new_value(ValueBox{new_v}) {}
    
    // Constructor for emplace_back optimization with ValueBox (zero-copy)
    DiffEntry(Type t, const Path& p, const ValueBox& old_box, const ValueBox& new_box)
        : type(t), path(p), old_value(old_box), new_value(new_box) {}

    // Default constructor
    DiffEntry() : type(Type::Add), old_value(ValueBox{Value{}}), new_value(ValueBox{Value{}}) {}
    
    /// Get the value that was added/removed/changed (convenience accessor)
    /// For Add: returns new_value
    /// For Remove: returns old_value
    /// For Change: returns new_value (use old_value for the previous value)
    [[nodiscard]] const Value& value() const {
        return (type == Type::Remove) ? *old_value : *new_value;
    }
    
    /// Get old value as const reference
    [[nodiscard]] const Value& get_old() const { return *old_value; }
    
    /// Get new value as const reference
    [[nodiscard]] const Value& get_new() const { return *new_value; }
};

/// Special keys used in DiffValueCollector leaf nodes
namespace diff_keys {
    inline constexpr const char* TYPE = "_diff_type";
    inline constexpr const char* OLD  = "_old";
    inline constexpr const char* NEW  = "_new";
}

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
    const Value* old_value = nullptr;  ///< Pointer to old value (valid for Remove/Change)
    const Value* new_value = nullptr;  ///< Pointer to new value (valid for Add/Change)
    
    /// Parse a diff node Value into this view
    /// @param val The Value to parse (should be a diff leaf node from DiffValueCollector)
    /// @return true if val is a valid diff node, false otherwise
    /// 
    /// After successful parse, access type/old_value/new_value directly (O(1))
    /// The pointers remain valid as long as the original Value is alive.
    bool parse(const Value& val) {
        auto* m = val.get_if<ValueMap>();
        if (!m) return false;
        
        // Look up type (required field)
        auto* type_box = m->find(diff_keys::TYPE);
        if (!type_box) return false;
        
        auto* type_val = type_box->get().get_if<uint8_t>();
        if (!type_val || *type_val > static_cast<uint8_t>(DiffEntry::Type::Change)) {
            return false;
        }
        
        type = static_cast<DiffEntry::Type>(*type_val);
        
        // Look up old value (optional, present for Remove/Change)
        if (auto* old_box = m->find(diff_keys::OLD)) {
            old_value = &old_box->get();
        } else {
            old_value = nullptr;
        }
        
        // Look up new value (optional, present for Add/Change)
        if (auto* new_box = m->find(diff_keys::NEW)) {
            new_value = &new_box->get();
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
    [[nodiscard]] const Value& get_old() const {
        if (!old_value) throw std::runtime_error("DiffNodeView: old_value not available");
        return *old_value;
    }
    
    /// Get new value (throws if not available)
    [[nodiscard]] const Value& get_new() const {
        if (!new_value) throw std::runtime_error("DiffNodeView: new_value not available");
        return *new_value;
    }
    
    /// Get the meaningful value based on type
    /// - Add: returns new_value
    /// - Remove: returns old_value
    /// - Change: returns new_value
    [[nodiscard]] const Value& value() const {
        return (type == DiffEntry::Type::Remove) ? get_old() : get_new();
    }
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

    void diff_value(const Value& old_val, const Value& new_val, Path& current_path);
    void diff_map(const ValueMap& old_map, const ValueMap& new_map, Path& current_path);
    void diff_vector(const ValueVector& old_vec, const ValueVector& new_vec, Path& current_path);
    void collect_entries(const Value& val, Path& current_path, bool is_add);
    void collect_removed(const Value& val, Path& current_path);
    void collect_added(const Value& val, Path& current_path);

public:
    void diff(const Value& old_val, const Value& new_val, bool recursive = true);
    [[nodiscard]] const std::vector<DiffEntry>& get_diffs() const;
    void clear();
    [[nodiscard]] bool has_changes() const;
    [[nodiscard]] bool is_recursive() const { return recursive_; }
    void print_diffs() const;
};


// ============================================================
// DiffValueCollector - Collects diff directly as a Value tree
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
    Value result_;
    bool has_changes_ = false;
    bool recursive_ = true;
    
    // Core diff functions that return the diff subtree for that node
    Value diff_value_impl(const Value& old_val, const Value& new_val, bool& changed);
    Value diff_value_impl_box(const ValueBox& old_box, const ValueBox& new_box, bool& changed);
    Value diff_map_impl(const ValueMap& old_map, const ValueMap& new_map, bool& changed);
    Value diff_vector_impl(const ValueVector& old_vec, const ValueVector& new_vec, bool& changed);
    
    // Collect all entries under a container (for add/remove of entire subtree)
    Value collect_entries(const Value& val, DiffEntry::Type type);
    Value collect_entries_box(const ValueBox& val_box, DiffEntry::Type type);
    
    // Create a leaf diff node using ValueBox directly (zero-copy for referenced values)
    // Preferred when we have access to the original ValueBox from container traversal
    static Value make_diff_node(DiffEntry::Type type, const ValueBox& val_box);
    static Value make_diff_node(DiffEntry::Type type, const ValueBox& old_box, const ValueBox& new_box);
    
    // Convenience overloads for Value& (creates temporary ValueBox)
    static Value make_diff_node(DiffEntry::Type type, const Value& val);
    static Value make_diff_node(DiffEntry::Type type, const Value& old_val, const Value& new_val);

public:
    DiffValueCollector() = default;
    
    /// Compute diff and organize as Value tree in a single pass
    void diff(const Value& old_val, const Value& new_val, bool recursive = true);
    
    /// Get the diff result as a Value tree
    /// Returns an empty map if no differences found
    [[nodiscard]] const Value& get() const { return result_; }
    
    /// Check if any changes were detected
    [[nodiscard]] bool has_changes() const { return has_changes_; }
    
    /// Check if recursive mode was used
    [[nodiscard]] bool is_recursive() const { return recursive_; }
    
    /// Clear the result
    void clear();
    
    /// Check if a path in the diff result is a diff leaf node
    /// (contains _diff_type key)
    [[nodiscard]] static bool is_diff_node(const Value& val);
    
    /// Extract diff type from a diff leaf node as DiffEntry::Type
    /// @pre val must be a diff node (verified by is_diff_node())
    /// @return The diff type. Returns DiffEntry::Type::Add if not a valid diff node.
    [[nodiscard]] static DiffEntry::Type get_diff_type(const Value& val);
    
    /// Extract old value from a diff leaf node
    /// Returns null Value if not present
    [[nodiscard]] static Value get_old_value(const Value& val);
    
    /// Extract new value from a diff leaf node  
    /// Returns null Value if not present
    [[nodiscard]] static Value get_new_value(const Value& val);
    
    /// Print the diff tree in a readable format
    void print() const;
};

[[nodiscard]] LAGER_EXT_API bool has_any_difference(const Value& old_val, const Value& new_val, bool recursive = true);

namespace detail {
    [[nodiscard]] bool values_differ(const Value& old_val, const Value& new_val, bool recursive);
    [[nodiscard]] bool maps_differ(const ValueMap& old_map, const ValueMap& new_map, bool recursive);
    [[nodiscard]] bool vectors_differ(const ValueVector& old_vec, const ValueVector& new_vec, bool recursive);
}

/// Convenience function to compute diff as a Value tree (uses DiffValueCollector)
[[nodiscard]] LAGER_EXT_API Value diff_as_value(const Value& old_val, const Value& new_val, bool recursive = true);

} // namespace lager_ext
