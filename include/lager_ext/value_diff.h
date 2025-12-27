// value_diff.h - Diff operations for Value types

#pragma once

#include <lager_ext/value.h>
#include <vector>

namespace lager_ext {

struct DiffEntry {
    enum class Type { Add, Remove, Change };

    Type type;
    Path path;              // Path to the changed value
    Value old_value;        // Old value (null for Add)
    Value new_value;        // New value (null for Remove)

    // Constructor for emplace_back optimization
    DiffEntry(Type t, const Path& p, const Value& old_v, const Value& new_v)
        : type(t), path(p), old_value(old_v), new_value(new_v) {}

    // Default constructor
    DiffEntry() = default;
};

class DiffCollector {
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

using RecursiveDiffCollector = DiffCollector;

[[nodiscard]] bool has_any_difference(const Value& old_val, const Value& new_val, bool recursive = true);

namespace detail {
    [[nodiscard]] bool values_differ(const Value& old_val, const Value& new_val, bool recursive);
    [[nodiscard]] bool maps_differ(const ValueMap& old_map, const ValueMap& new_map, bool recursive);
    [[nodiscard]] bool vectors_differ(const ValueVector& old_vec, const ValueVector& new_vec, bool recursive);
}

void demo_immer_diff();
void demo_recursive_diff_collector();

} // namespace lager_ext
