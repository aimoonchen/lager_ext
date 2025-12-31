// lager_lens.h - Type-erased lenses using lager::lens<Value, Value>

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/concepts.h>  // C++20 Concepts (StringLike, IndexType, PathElementType, etc.)
#include <lager_ext/value.h>
#include <lager_ext/path_utils.h>  // For get_at_path_direct, set_at_path_direct
#include <lager/lens.hpp>
#include <lager/lenses.hpp>
#include <zug/compose.hpp>

#include <concepts>
#include <type_traits>

namespace lager_ext {

template<typename T>
concept IndexLike = std::is_integral_v<std::decay_t<T>> && !StringLike<T>;

namespace detail {

// Helper to detect if a type has .get() method
template<typename L>
concept HasGetMethod = requires(const L& lens, const Value& v) {
    { lens.get(v) } -> std::convertible_to<Value>;
};

// Helper to detect if a type has .set() method
template<typename L>
concept HasSetMethod = requires(const L& lens, Value v, Value p) {
    { lens.set(std::move(v), std::move(p)) } -> std::convertible_to<Value>;
};

// Helper to detect if a type works with lager::view
template<typename L>
concept WorksWithLagerView = requires(const L& lens, const Value& v) {
    { lager::view(lens, v) } -> std::convertible_to<Value>;
};

// Helper to detect if a type works with lager::set
template<typename L>
concept WorksWithLagerSet = requires(const L& lens, Value v, Value p) {
    { lager::set(lens, std::move(v), std::move(p)) } -> std::convertible_to<Value>;
};

} // namespace detail

template<typename L>
concept ValueGetter = detail::HasGetMethod<L> || detail::WorksWithLagerView<L>;

template<typename L>
concept ValueSetter = detail::HasSetMethod<L> || detail::WorksWithLagerSet<L>;

template<typename L>
concept ValueLens = ValueGetter<L> && ValueSetter<L>;

template<ValueGetter L>
[[nodiscard]] Value lens_get(const L& lens, const Value& whole) {
    if constexpr (detail::HasGetMethod<L>) {
        return lens.get(whole);
    } else {
        return lager::view(lens, whole);
    }
}

template<ValueSetter L>
[[nodiscard]] Value lens_set(const L& lens, Value whole, Value part) {
    if constexpr (detail::HasSetMethod<L>) {
        return lens.set(std::move(whole), std::move(part));
    } else {
        return lager::set(lens, std::move(whole), std::move(part));
    }
}

template<ValueLens L, typename Fn>
[[nodiscard]] Value lens_over(const L& lens, Value whole, Fn&& fn) {
    if constexpr (requires { lens.over(whole, fn); }) {
        return lens.over(std::move(whole), std::forward<Fn>(fn));
    } else if constexpr (detail::HasGetMethod<L> && detail::HasSetMethod<L>) {
        Value current = lens.get(whole);
        Value updated = std::forward<Fn>(fn)(std::move(current));
        return lens.set(std::move(whole), std::move(updated));
    } else {
        return lager::over(lens, std::move(whole), std::forward<Fn>(fn));
    }
}

template<ValueLens L1, ValueLens L2>
[[nodiscard]] auto lens_compose(L1&& outer, L2&& inner) {
    return lager::lenses::getset(
        // Getter: apply outer, then inner
        [outer = std::forward<L1>(outer), inner = std::forward<L2>(inner)]
        (const Value& whole) -> Value {
            return lens_get(inner, lens_get(outer, whole));
        },
        // Setter: get outer part, set inner, then set outer
        [outer, inner](Value whole, Value new_part) -> Value {
            Value outer_part = lens_get(outer, whole);
            Value new_outer = lens_set(inner, std::move(outer_part), std::move(new_part));
            return lens_set(outer, std::move(whole), std::move(new_outer));
        }
    );
}

using LagerValueLens = lager::lens<Value, Value>;

/// @brief Create a lens for accessing a map key
[[nodiscard]] inline auto key_lens(const std::string& key)
{
    return lager::lenses::getset(
        // Getter
        [key](const Value& obj) -> Value {
            if (auto* map = obj.get_if<ValueMap>()) {
                if (auto found = map->find(key); found != nullptr) {
                    return **found;
                }
            }
            return Value{};
        },
        // Setter (strict mode)
        [key](Value obj, Value value) -> Value {
            if (auto* map = obj.get_if<ValueMap>()) {
                auto new_map = map->set(key, immer::box<Value>{std::move(value)});
                return Value{std::move(new_map)};
            }
            return obj;
        });
}

/// @brief Create a lens for accessing a vector index
[[nodiscard]] inline auto index_lens(std::size_t index)
{
    return lager::lenses::getset(
        // Getter
        [index](const Value& obj) -> Value {
            if (auto* vec = obj.get_if<ValueVector>()) {
                if (index < vec->size()) {
                    return *(*vec)[index];
                }
            }
            return Value{};
        },
        // Setter (strict mode)
        [index](Value obj, Value value) -> Value {
            if (auto* vec = obj.get_if<ValueVector>()) {
                if (index < vec->size()) {
                    auto new_vec = vec->update(index, [&](auto&&) {
                        return immer::box<Value>{std::move(value)};
                    });
                    return Value{std::move(new_vec)};
                }
            }
            return obj;
        });
}

/// @brief Build a lens from a runtime Path (with caching)
[[nodiscard]] LAGER_EXT_API LagerValueLens lager_path_lens(const Path& path);

} // namespace lager_ext

// Include static_path.h for LiteralPath support
// This is placed after closing namespace to avoid circular dependencies
#include <lager_ext/static_path.h>

namespace lager_ext {

/// @brief Convert a compile-time string literal path to a lager lens
/// @tparam Ptr JSON Pointer style path string (e.g., "/users/0/name")
/// @return Type-erased LagerValueLens that can be used with lager::view/set/over
/// 
/// @example
/// auto lens = static_path_lens<"/users/0/name">();
/// Value name = lager::view(lens, root);
/// Value updated = lager::set(lens, root, Value{"Alice"});
template<FixedString Ptr>
[[nodiscard]] LagerValueLens static_path_lens() {
    using PathType = LiteralPath<Ptr>;
    return lager_path_lens(PathType::to_runtime_path());
}

namespace detail {
auto element_to_lens(StringLike auto&& elem) {
    return key_lens(std::string{std::forward<decltype(elem)>(elem)});
}
auto element_to_lens(IndexLike auto&& elem) {
    return index_lens(static_cast<std::size_t>(elem));
}
} // namespace detail

template <PathElementType... Elements>
auto static_path_lens(Elements&&... elements)
{
    return (zug::identity | ... | detail::element_to_lens(std::forward<Elements>(elements)));
}

struct LensCacheStats {
    std::size_t hits = 0;
    std::size_t misses = 0;
    std::size_t size = 0;
    std::size_t capacity = 0;
    double hit_rate = 0.0;
};

LAGER_EXT_API void clear_lens_cache();
[[nodiscard]] LAGER_EXT_API LensCacheStats get_lens_cache_stats();

// ============================================================
// PathLens - A lens-compatible path accessor
//
// PathLens satisfies the lager lens functor protocol, so it can be
// used directly with lager::view, lager::set, lager::over without
// needing to call to_lens().
//
// Example:
//   PathLens path = root / "users" / 0 / "name";
//   Value name = lager::view(path, state);      // Direct use as lens
//   Value updated = lager::set(path, state, Value{"Alice"});
// ============================================================
class PathLens {
public:
    PathLens() = default;
    explicit PathLens(Path path) : path_(std::move(path)) {}

    // ============================================================
    // Lens Functor Protocol - Direct compatibility with lager::view/set/over
    //
    // This operator() makes PathLens satisfy the lens functor interface.
    // Instead of calling to_lens(), you can use PathLens directly:
    //   lager::view(path_lens, root)
    //   lager::set(path_lens, root, new_val)
    // ============================================================
    template<typename F>
    auto operator()(F&& f) const {
        return [this, f = std::forward<F>(f)](const Value& whole) {
            // Get the part at this path
            Value part = this->get(whole);
            // Apply the functor and get the setter
            return f(std::move(part))([this, &whole](Value new_part) {
                return this->set(whole, std::move(new_part));
            });
        };
    }

    [[nodiscard]] PathLens key(const std::string& k) const & {
        PathLens result = *this;
        result.path_.push_back(k);
        return result;
    }
    [[nodiscard]] PathLens key(const std::string& k) && {
        path_.push_back(k);
        return std::move(*this);
    }
    [[nodiscard]] PathLens index(std::size_t i) const & {
        PathLens result = *this;
        result.path_.push_back(i);
        return result;
    }
    [[nodiscard]] PathLens index(std::size_t i) && {
        path_.push_back(i);
        return std::move(*this);
    }

    [[nodiscard]] PathLens operator/(const std::string& k) const & { return key(k); }
    [[nodiscard]] PathLens operator/(const std::string& k) && { return std::move(*this).key(k); }
    [[nodiscard]] PathLens operator/(const char* k) const & { return key(std::string{k}); }
    [[nodiscard]] PathLens operator/(const char* k) && { return std::move(*this).key(std::string{k}); }
    [[nodiscard]] PathLens operator/(std::size_t i) const & { return index(i); }
    [[nodiscard]] PathLens operator/(std::size_t i) && { return std::move(*this).index(i); }
    [[nodiscard]] PathLens operator/(int i) const & { return index(static_cast<std::size_t>(i)); }
    [[nodiscard]] PathLens operator/(int i) && { return std::move(*this).index(static_cast<std::size_t>(i)); }

    [[nodiscard]] const Path& path() const noexcept { return path_; }
    [[nodiscard]] Path& path() noexcept { return path_; }
    
    /// Convert to LagerValueLens (for compatibility, but usually not needed)
    [[nodiscard]] LagerValueLens to_lens() const { return lager_path_lens(path_); }
    
    /// Direct get/set/over operations (more efficient than through to_lens())
    [[nodiscard]] Value get(const Value& root) const;
    [[nodiscard]] Value set(const Value& root, Value new_val) const;
    
    template<typename Fn>
    [[nodiscard]] Value over(const Value& root, Fn&& fn) const {
        return set(root, std::forward<Fn>(fn)(get(root)));
    }

    [[nodiscard]] bool empty() const noexcept { return path_.empty(); }
    [[nodiscard]] std::size_t depth() const noexcept { return path_.size(); }

    [[nodiscard]] PathLens concat(const PathLens& other) const {
        PathLens result = *this;
        for (const auto& elem : other.path_) {
            result.path_.push_back(elem);
        }
        return result;
    }
    [[nodiscard]] PathLens parent() const {
        if (path_.empty()) return *this;
        PathLens result;
        result.path_.assign(path_.begin(), path_.end() - 1);
        return result;
    }
    [[nodiscard]] std::string to_string() const { return path_to_string(path_); }

    bool operator==(const PathLens& other) const { return path_ == other.path_; }
    bool operator!=(const PathLens& other) const { return path_ != other.path_; }
    bool operator==(const Path& other) const { return path_ == other; }
    bool operator!=(const Path& other) const { return path_ != other; }

private:
    Path path_;
};

using PathBuilder = PathLens;  // Backward compatibility alias

inline const PathLens root;

template<PathElementType... Elements>
[[nodiscard]] PathLens make_path(Elements&&... elements) {
    PathLens builder;
    (builder.path().push_back(
        [](auto&& e) -> PathElement {
            using T = std::decay_t<decltype(e)>;
            if constexpr (StringLike<T>) {
                return std::string{std::forward<decltype(e)>(e)};
            } else {
                return static_cast<std::size_t>(e);
            }
        }(std::forward<Elements>(elements))
    ), ...);
    return builder;
}

template<PathElementType... Elements>
[[nodiscard]] Value get_at(const Value& root, Elements&&... path_elements) {
    return make_path(std::forward<Elements>(path_elements)...).get(root);
}

template<PathElementType... Elements>
[[nodiscard]] Value set_at(const Value& root, Value new_val, Elements&&... path_elements) {
    return make_path(std::forward<Elements>(path_elements)...).set(root, std::move(new_val));
}

template<typename Fn, PathElementType... Elements>
[[nodiscard]] Value over_at(const Value& root, Fn&& fn, Elements&&... path_elements) {
    return make_path(std::forward<Elements>(path_elements)...).over(root, std::forward<Fn>(fn));
}

enum class PathErrorCode {
    Success = 0,
    KeyNotFound,        // Map key doesn't exist
    IndexOutOfRange,    // Vector/array index out of bounds
    TypeMismatch,       // Expected container type, got primitive
    NullValue,          // Attempted access on null value
    EmptyPath,
};

struct PathAccessResult {
    Value value;                    // The accessed value (or null on error)
    bool success = false;           // Whether the access succeeded
    PathErrorCode error_code = PathErrorCode::Success;
    std::string error_message;      // Human-readable error description
    Path resolved_path;             // The portion of path that was successfully resolved
    std::size_t failed_at_index = 0;

    explicit operator bool() const noexcept { return success; }
    const Value& get() const {
        if (!success) {
            throw std::runtime_error("Path access failed: " + error_message);
        }
        return value;
    }
    Value get_or(Value default_val) const {
        return success ? value : std::move(default_val);
    }
};

[[nodiscard]] LAGER_EXT_API PathAccessResult get_at_path_safe(const Value& root, const Path& path);
[[nodiscard]] LAGER_EXT_API PathAccessResult set_at_path_safe(const Value& root, const Path& path, Value new_val);

// ============================================================
// ZoomedValue - A focused view into a Value tree
//
// Similar to lager's cursor.zoom() concept, ZoomedValue provides
// a "zoomed-in" view of a Value at a specific path. Unlike cursor,
// ZoomedValue is a lightweight, stack-allocated object that doesn't
// require a store.
//
// Key differences from lager cursor:
// - No subscription/watch mechanism (Value is immutable, no store)
// - Stack-allocated, zero-overhead abstraction
// - set() returns new root, not modifying in place
//
// Example:
//   Value state = /* ... */;
//   ZoomedValue users = ZoomedValue(state) / "users";
//   ZoomedValue first_user = users / 0;
//   Value name = (first_user / "name").get();
//   Value new_state = (first_user / "name").set(Value{"Alice"});
// ============================================================
class ZoomedValue {
public:
    /// Create a zoomed view at the root
    explicit ZoomedValue(const Value& root) : root_(&root) {}
    
    /// Create a zoomed view at a specific path
    ZoomedValue(const Value& root, Path path)
        : root_(&root), path_(std::move(path)) {}
    
    // ============================================================
    // Navigation - Zoom further into the structure
    // ============================================================
    
    /// Zoom into a map key
    [[nodiscard]] ZoomedValue key(const std::string& k) const & {
        ZoomedValue result = *this;
        result.path_.push_back(k);
        return result;
    }
    [[nodiscard]] ZoomedValue key(const std::string& k) && {
        path_.push_back(k);
        return std::move(*this);
    }
    
    /// Zoom into a vector index
    [[nodiscard]] ZoomedValue index(std::size_t i) const & {
        ZoomedValue result = *this;
        result.path_.push_back(i);
        return result;
    }
    [[nodiscard]] ZoomedValue index(std::size_t i) && {
        path_.push_back(i);
        return std::move(*this);
    }
    
    /// Operator/ for chained navigation
    [[nodiscard]] ZoomedValue operator/(const std::string& k) const & { return key(k); }
    [[nodiscard]] ZoomedValue operator/(const std::string& k) && { return std::move(*this).key(k); }
    [[nodiscard]] ZoomedValue operator/(const char* k) const & { return key(std::string{k}); }
    [[nodiscard]] ZoomedValue operator/(const char* k) && { return std::move(*this).key(std::string{k}); }
    [[nodiscard]] ZoomedValue operator/(std::size_t i) const & { return index(i); }
    [[nodiscard]] ZoomedValue operator/(std::size_t i) && { return std::move(*this).index(i); }
    [[nodiscard]] ZoomedValue operator/(int i) const & { return index(static_cast<std::size_t>(i)); }
    [[nodiscard]] ZoomedValue operator/(int i) && { return std::move(*this).index(static_cast<std::size_t>(i)); }
    
    // ============================================================
    // Access Operations
    // ============================================================
    
    /// Get the value at the current zoom path
    [[nodiscard]] Value get() const {
        return get_at_path_direct(*root_, path_);
    }
    
    /// Set the value at the current zoom path (returns new root)
    [[nodiscard]] Value set(Value new_val) const {
        return set_at_path_direct(*root_, path_, std::move(new_val));
    }
    
    /// Update the value using a function (returns new root)
    template<typename Fn>
    [[nodiscard]] Value over(Fn&& fn) const {
        return set(std::forward<Fn>(fn)(get()));
    }
    
    /// Dereference operator - same as get()
    [[nodiscard]] Value operator*() const { return get(); }
    
    // ============================================================
    // Introspection
    // ============================================================
    
    /// Get the root value
    [[nodiscard]] const Value& root() const noexcept { return *root_; }
    
    /// Get the current zoom path
    [[nodiscard]] const Path& path() const noexcept { return path_; }
    
    /// Check if at root level
    [[nodiscard]] bool at_root() const noexcept { return path_.empty(); }
    
    /// Get zoom depth (number of path elements)
    [[nodiscard]] std::size_t depth() const noexcept { return path_.size(); }
    
    /// Convert to PathLens (for use with lager APIs)
    [[nodiscard]] PathLens to_lens() const { return PathLens{path_}; }
    
    /// Get parent zoom (go up one level)
    [[nodiscard]] ZoomedValue parent() const {
        if (path_.empty()) return *this;
        ZoomedValue result = *this;
        result.path_.pop_back();
        return result;
    }
    
    /// Create a new ZoomedValue with updated root
    /// Useful after set() to continue working with the new state
    [[nodiscard]] ZoomedValue with_root(const Value& new_root) const {
        return ZoomedValue{new_root, path_};
    }

private:
    const Value* root_ = nullptr;
    Path path_;
};

/// Create a ZoomedValue at root
[[nodiscard]] inline ZoomedValue zoom(const Value& root) {
    return ZoomedValue{root};
}

/// Create a ZoomedValue at a specific path
[[nodiscard]] inline ZoomedValue zoom(const Value& root, const Path& path) {
    return ZoomedValue{root, path};
}

/// Create a ZoomedValue using variadic path elements
template<PathElementType... Elements>
[[nodiscard]] ZoomedValue zoom(const Value& root, Elements&&... path_elements) {
    return ZoomedValue{root, make_path(std::forward<Elements>(path_elements)...).path()};
}

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
// Example:
//   PathWatcher watcher;
//   watcher.watch("/users/0/name", [](const Value& old_v, const Value& new_v) {
//       std::cout << "Name changed: " << old_v.as_string() << " -> " << new_v.as_string() << "\n";
//   });
//   watcher.check(old_state, new_state);
// ============================================================

class LAGER_EXT_API PathWatcher {
public:
    using ChangeCallback = std::function<void(const Value& old_val, const Value& new_val)>;
    
    PathWatcher() = default;
    
    /// Add a path to watch with a callback
    /// @param path_str JSON Pointer style path (e.g., "/users/0/name")
    /// @param callback Function called when value at path changes
    void watch(const std::string& path_str, ChangeCallback callback);
    
    /// Add a path to watch with a callback
    /// @param path Path elements
    /// @param callback Function called when value at path changes
    void watch(Path path, ChangeCallback callback);
    
    /// Add a path to watch using PathLens
    void watch(const PathLens& lens, ChangeCallback callback) {
        watch(lens.path(), std::move(callback));
    }
    
    /// Remove a watched path
    void unwatch(const std::string& path_str);
    void unwatch(const Path& path);
    
    /// Clear all watched paths
    void clear();
    
    /// Check for changes between old and new state
    /// Calls callbacks for any paths that have changed
    /// @return Number of callbacks triggered
    std::size_t check(const Value& old_state, const Value& new_state);
    
    /// Get number of watched paths
    [[nodiscard]] std::size_t size() const noexcept { return watches_.size(); }
    
    /// Check if any paths are being watched
    [[nodiscard]] bool empty() const noexcept { return watches_.empty(); }

private:
    struct WatchEntry {
        Path path;
        ChangeCallback callback;
    };
    std::vector<WatchEntry> watches_;
};

} // namespace lager_ext
