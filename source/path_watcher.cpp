// path_watcher.cpp
// Implementation of PathWatcher with Trie-based change detection

#include <lager_ext/path_watcher.h>

#include <unordered_map>
#include <vector>

namespace lager_ext {

// ============================================================
// PathWatcher implementation - Optimized with Trie + Structural Sharing
// ============================================================

namespace {

// Hash function for PathElement (string_view or size_t)
struct PathElementHash {
    std::size_t operator()(const PathElement& elem) const {
        return std::visit(
            [](const auto& v) -> std::size_t {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string_view>) {
                    return std::hash<std::string_view>{}(v);
                } else {
                    return std::hash<std::size_t>{}(v);
                }
            },
            elem);
    }
};

// Helper to check if two Values share the same underlying data (structural sharing)
bool values_share_structure(const Value& a, const Value& b) {
    // Quick check: same variant index?
    if (a.type_index() != b.type_index())
        return false;

    // For containers, check if they share the same immer internal pointer
    // Container Boxing: use BoxedValueMap, BoxedValueVector, etc.
    if (auto* boxed_map_a = a.get_if<BoxedValueMap>()) {
        if (auto* boxed_map_b = b.get_if<BoxedValueMap>()) {
            // immer::map uses structural sharing - same identity means same data
            return boxed_map_a->get().identity() == boxed_map_b->get().identity();
        }
    }
    if (auto* boxed_vec_a = a.get_if<BoxedValueVector>()) {
        if (auto* boxed_vec_b = b.get_if<BoxedValueVector>()) {
            return boxed_vec_a->get().identity() == boxed_vec_b->get().identity();
        }
    }
    if (auto* boxed_arr_a = a.get_if<BoxedValueArray>()) {
        if (auto* boxed_arr_b = b.get_if<BoxedValueArray>()) {
            // ValueArray doesn't have identity(), fall back to equality
            return a == b;
        }
    }
    if (auto* boxed_tab_a = a.get_if<BoxedValueTable>()) {
        if (auto* boxed_tab_b = b.get_if<BoxedValueTable>()) {
            // ValueTable doesn't expose identity(), fall back to equality
            return a == b;
        }
    }

    // For primitives, just compare values
    return a == b;
}

// Get child value at path element
Value get_child(const Value& parent, const PathElement& elem) {
    return std::visit(
        [&parent](const auto& key) -> Value {
            using T = std::decay_t<decltype(key)>;
            if constexpr (std::is_same_v<T, std::string_view>) {
                // Convert string_view to string for map lookup
                return parent.at(std::string{key});
            } else {
                return parent.at(key); // size_t index
            }
        },
        elem);
}

} // anonymous namespace

// ============================================================
// Trie Node Definition
// ============================================================

struct PathWatcher::WatchNode {
    // Callbacks registered at this exact path
    std::vector<ChangeCallback> callbacks;

    // Children indexed by next path element
    std::unordered_map<PathElement, std::unique_ptr<WatchNode>, PathElementHash> children;

    // Check if this node or any descendant has callbacks
    [[nodiscard]] bool has_any_watches() const {
        if (!callbacks.empty())
            return true;
        for (const auto& [_, child] : children) {
            if (child && child->has_any_watches())
                return true;
        }
        return false;
    }

    // Count total watches in this subtree
    [[nodiscard]] std::size_t count_watches() const {
        std::size_t count = callbacks.size();
        for (const auto& [_, child] : children) {
            if (child)
                count += child->count_watches();
        }
        return count;
    }
};

// ============================================================
// Constructor / Destructor / Move Operations
// ============================================================

PathWatcher::PathWatcher() = default;
PathWatcher::~PathWatcher() = default;

PathWatcher::PathWatcher(PathWatcher&&) noexcept = default;
PathWatcher& PathWatcher::operator=(PathWatcher&&) noexcept = default;

// ============================================================
// Watch / Unwatch Operations
// ============================================================

void PathWatcher::watch(const std::string& path_str, ChangeCallback callback) {
    watch(Path{path_str}, std::move(callback));
}

void PathWatcher::watch(Path path, ChangeCallback callback) {
    insert_path(path, std::move(callback));
}

void PathWatcher::insert_path(const Path& path, ChangeCallback callback) {
    if (!root_) {
        root_ = std::make_unique<WatchNode>();
    }

    WatchNode* node = root_.get();

    // Traverse/create trie nodes for each path element
    for (const auto& elem : path) {
        auto& child = node->children[elem];
        if (!child) {
            child = std::make_unique<WatchNode>();
        }
        node = child.get();
    }

    // Add callback at the leaf node
    node->callbacks.push_back(std::move(callback));
    ++watch_count_;
}

void PathWatcher::unwatch(const std::string& path_str) {
    unwatch(Path{path_str});
}

void PathWatcher::unwatch(const Path& path) {
    remove_path(path);
}

bool PathWatcher::remove_path(const Path& path) {
    if (!root_)
        return false;

    // Stack to track path for cleanup
    std::vector<std::pair<WatchNode*, const PathElement*>> ancestors;
    WatchNode* node = root_.get();

    // Traverse to the target node
    for (const auto& elem : path) {
        auto it = node->children.find(elem);
        if (it == node->children.end() || !it->second) {
            return false; // Path not found
        }
        ancestors.push_back({node, &elem});
        node = it->second.get();
    }

    if (node->callbacks.empty()) {
        return false; // No callbacks at this path
    }

    // Remove all callbacks at this path
    std::size_t removed = node->callbacks.size();
    node->callbacks.clear();
    watch_count_ -= removed;

    // Clean up empty nodes (bottom-up)
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        WatchNode* parent = it->first;
        const PathElement* elem = it->second;

        auto child_it = parent->children.find(*elem);
        if (child_it != parent->children.end() && child_it->second && !child_it->second->has_any_watches()) {
            parent->children.erase(child_it);
        } else {
            break; // Stop if this subtree still has watches
        }
    }

    return true;
}

void PathWatcher::clear() {
    root_.reset();
    watch_count_ = 0;
}

// ============================================================
// Change Detection
// ============================================================

std::size_t PathWatcher::check(const Value& old_state, const Value& new_state) {
    ++stats_.total_checks;

    // Optimization 1: Fast path - identical objects
    if (&old_state == &new_state) {
        ++stats_.skipped_equal;
        return 0;
    }

    // Optimization 2: Quick equality check using structural sharing
    if (values_share_structure(old_state, new_state)) {
        ++stats_.skipped_equal;
        return 0;
    }

    // No watches registered
    if (!root_ || watch_count_ == 0) {
        return 0;
    }

    // Optimization 3: Trie-based traversal with pruning
    std::size_t triggered = check_node(root_.get(), old_state, new_state);
    stats_.callbacks_triggered += triggered;

    return triggered;
}

std::size_t PathWatcher::check_node(WatchNode* node, const Value& old_val, const Value& new_val) {
    if (!node)
        return 0;

    ++stats_.nodes_visited;

    std::size_t triggered = 0;

    // Trigger callbacks at this node if values differ
    if (!node->callbacks.empty()) {
        if (old_val != new_val) {
            for (const auto& callback : node->callbacks) {
                callback(old_val, new_val);
                ++triggered;
            }
        }
    }

    // Recurse into children
    for (const auto& [elem, child] : node->children) {
        if (!child)
            continue;

        Value old_child = get_child(old_val, elem);
        Value new_child = get_child(new_val, elem);

        // Optimization: Prune if children share structure (haven't changed)
        if (values_share_structure(old_child, new_child)) {
            ++stats_.nodes_pruned;
            continue; // Skip entire subtree!
        }

        triggered += check_node(child.get(), old_child, new_child);
    }

    return triggered;
}

} // namespace lager_ext
