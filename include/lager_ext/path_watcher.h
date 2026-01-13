// path_watcher.h - Watch for changes at specific paths in Value trees
//
// PathWatcher uses a Trie-based structure for efficient change detection
// with structural sharing optimization leveraging immer's immutable data.
//
// Performance characteristics:
// - O(ChangedNodes) instead of O(Watchers * PathDepth)
// - Automatic pruning of unchanged subtrees via immer identity checks
// - Fast path for identical state objects

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/path_utils.h>
#include <lager_ext/value.h>

#include <functional>
#include <memory>
#include <string>

namespace lager_ext {

// ============================================================
// PathWatcher - Watch for changes at specific paths
//
// This utility helps detect and react to changes at specific paths
// when comparing two Value trees (e.g., before and after state update).
//
// Unlike lager's cursor.watch() which uses reactive updates,
// PathWatcher uses explicit diff checking, which fits the
// immutable Value model better.
//
// **Performance Optimizations:**
// 1. Fast path: Skip check entirely if old_state == new_state
// 2. Trie structure: Shared path prefixes are only traversed once
// 3. Structural sharing: Uses immer's pointer equality for early pruning
//
// Example:
//   PathWatcher watcher;
//   watcher.watch("/users/0/name", [](const Value& old_v, const Value& new_v) {
//       std::cout << "Name changed!\n";
//   });
//   watcher.check(old_state, new_state);
// ============================================================

class LAGER_EXT_API PathWatcher {
public:
    using ChangeCallback = std::function<void(const Value& old_val, const Value& new_val)>;

    PathWatcher();
    ~PathWatcher();

    // Non-copyable, movable
    PathWatcher(const PathWatcher&) = delete;
    PathWatcher& operator=(const PathWatcher&) = delete;
    PathWatcher(PathWatcher&&) noexcept;
    PathWatcher& operator=(PathWatcher&&) noexcept;

    /// Add a path to watch with a callback
    /// @param path_str JSON Pointer style path (e.g., "/users/0/name")
    /// @param callback Function called when value at path changes
    void watch(const std::string& path_str, ChangeCallback callback);

    /// Add a path to watch with a callback
    /// @param path Path elements
    /// @param callback Function called when value at path changes
    void watch(Path path, ChangeCallback callback);

    /// Remove all callbacks at a watched path
    void unwatch(const std::string& path_str);
    void unwatch(const Path& path);

    /// Clear all watched paths
    void clear();

    /// Check for changes between old and new state
    /// Calls callbacks for any paths that have changed
    /// Uses trie-based traversal with structural sharing optimization
    /// @return Number of callbacks triggered
    std::size_t check(const Value& old_state, const Value& new_state);

    /// Get number of watched paths (callbacks registered)
    [[nodiscard]] std::size_t size() const noexcept { return watch_count_; }

    /// Check if any paths are being watched
    [[nodiscard]] bool empty() const noexcept { return watch_count_ == 0; }

    /// Statistics for performance monitoring
    struct Stats {
        std::size_t total_checks = 0;        ///< Total check() calls
        std::size_t skipped_equal = 0;       ///< Skipped due to equal state
        std::size_t nodes_visited = 0;       ///< Trie nodes visited
        std::size_t nodes_pruned = 0;        ///< Nodes pruned via structural sharing
        std::size_t callbacks_triggered = 0; ///< Total callbacks triggered
    };

    /// Get performance statistics
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    /// Reset statistics
    void reset_stats() noexcept { stats_ = Stats{}; }

private:
    // Trie node for organizing watches by path prefix
    struct WatchNode;

    std::unique_ptr<WatchNode> root_;
    std::size_t watch_count_ = 0;
    Stats stats_;

    // Recursive check with structural sharing optimization
    std::size_t check_node(WatchNode* node, const Value& old_val, const Value& new_val);

    // Insert a path into the trie
    void insert_path(const Path& path, ChangeCallback callback);

    // Remove a path from the trie
    bool remove_path(const Path& path);
};

} // namespace lager_ext
