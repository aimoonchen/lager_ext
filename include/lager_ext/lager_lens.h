// lager_lens.h - Type-erased lenses using lager::lens<ImmerValue, ImmerValue>

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/concepts.h> // C++20 Concepts (StringLike, IndexType, PathElementType, etc.)
#include <lager_ext/path.h>     // For Path, PathView types
#include <lager_ext/value.h>

#include <lager/lens.hpp>
#include <lager/lenses.hpp>

#include <concepts>
#include <type_traits>
#include <zug/compose.hpp>

namespace lager_ext {

// Forward declarations for path traversal functions (defined in path_utils.h)
// These are used by ZoomedValue and PathLens for their get/set operations
[[nodiscard]] LAGER_EXT_API ImmerValue get_at_path(const ImmerValue& root, PathView path);
[[nodiscard]] LAGER_EXT_API ImmerValue set_at_path(const ImmerValue& root, PathView path, ImmerValue new_val);

template <typename T>
concept IndexLike = std::is_integral_v<std::decay_t<T>> && !StringLike<T>;

namespace detail {

// Helper to detect if a type has .get() method
template <typename L>
concept HasGetMethod = requires(const L& lens, const ImmerValue& v) {
    { lens.get(v) } -> std::convertible_to<ImmerValue>;
};

// Helper to detect if a type has .set() method
template <typename L>
concept HasSetMethod = requires(const L& lens, ImmerValue v, ImmerValue p) {
    { lens.set(std::move(v), std::move(p)) } -> std::convertible_to<ImmerValue>;
};

// Helper to detect if a type works with lager::view
template <typename L>
concept WorksWithLagerView = requires(const L& lens, const ImmerValue& v) {
    { lager::view(lens, v) } -> std::convertible_to<ImmerValue>;
};

// Helper to detect if a type works with lager::set
template <typename L>
concept WorksWithLagerSet = requires(const L& lens, ImmerValue v, ImmerValue p) {
    { lager::set(lens, std::move(v), std::move(p)) } -> std::convertible_to<ImmerValue>;
};

} // namespace detail

template <typename L>
concept ValueGetter = detail::HasGetMethod<L> || detail::WorksWithLagerView<L>;

template <typename L>
concept ValueSetter = detail::HasSetMethod<L> || detail::WorksWithLagerSet<L>;

template <typename L>
concept ValueLens = ValueGetter<L> && ValueSetter<L>;

template <ValueGetter L>
[[nodiscard]] ImmerValue lens_get(const L& lens, const ImmerValue& whole) {
    if constexpr (detail::HasGetMethod<L>) {
        return lens.get(whole);
    } else {
        return lager::view(lens, whole);
    }
}

template <ValueSetter L>
[[nodiscard]] ImmerValue lens_set(const L& lens, ImmerValue whole, ImmerValue part) {
    if constexpr (detail::HasSetMethod<L>) {
        return lens.set(std::move(whole), std::move(part));
    } else {
        return lager::set(lens, std::move(whole), std::move(part));
    }
}

template <ValueLens L, typename Fn>
[[nodiscard]] ImmerValue lens_over(const L& lens, ImmerValue whole, Fn&& fn) {
    if constexpr (requires { lens.over(whole, fn); }) {
        return lens.over(std::move(whole), std::forward<Fn>(fn));
    } else if constexpr (detail::HasGetMethod<L> && detail::HasSetMethod<L>) {
        ImmerValue current = lens.get(whole);
        ImmerValue updated = std::forward<Fn>(fn)(std::move(current));
        return lens.set(std::move(whole), std::move(updated));
    } else {
        return lager::over(lens, std::move(whole), std::forward<Fn>(fn));
    }
}

template <ValueLens L1, ValueLens L2>
[[nodiscard]] auto lens_compose(L1&& outer, L2&& inner) {
    return lager::lenses::getset(
        // Getter: apply outer, then inner
        [outer = std::forward<L1>(outer), inner = std::forward<L2>(inner)](const ImmerValue& whole) -> ImmerValue {
            return lens_get(inner, lens_get(outer, whole));
        },
        // Setter: get outer part, set inner, then set outer
        [outer, inner](ImmerValue whole, ImmerValue new_part) -> ImmerValue {
            ImmerValue outer_part = lens_get(outer, whole);
            ImmerValue new_outer = lens_set(inner, std::move(outer_part), std::move(new_part));
            return lens_set(outer, std::move(whole), std::move(new_outer));
        });
}

using LagerValueLens = lager::lens<ImmerValue, ImmerValue>;

/// @brief Create a lens for accessing a map key
[[nodiscard]] inline auto key_lens(std::string_view key) {
    return lager::lenses::getset(
        // Getter - Container Boxing: access boxed map, then find value directly
        [key](const ImmerValue& obj) -> ImmerValue {
            if (auto* boxed_map = obj.get_if<BoxedValueMap>()) {
                if (auto* found = boxed_map->get().find(key); found != nullptr) {
                    return *found;  // Container Boxing: ValueMap stores ImmerValue directly
                }
            }
            return ImmerValue{};
        },
        // Setter (strict mode) - Container Boxing: unbox -> modify -> rebox
        [key](ImmerValue obj, ImmerValue value) -> ImmerValue {
            if (auto* boxed_map = obj.get_if<BoxedValueMap>()) {
                auto new_map = boxed_map->get().set(std::string{key}, std::move(value));
                return ImmerValue{BoxedValueMap{std::move(new_map)}};
            }
            return obj;
        });
}

/// @brief Create a lens for accessing a vector index
[[nodiscard]] inline auto index_lens(std::size_t index) {
    return lager::lenses::getset(
        // Getter - Container Boxing: access boxed vector, then get value directly
        [index](const ImmerValue& obj) -> ImmerValue {
            if (auto* boxed_vec = obj.get_if<BoxedValueVector>()) {
                const auto& vec = boxed_vec->get();
                if (index < vec.size()) {
                    return vec[index];  // Container Boxing: ValueVector stores ImmerValue directly
                }
            }
            return ImmerValue{};
        },
        // Setter (strict mode) - Container Boxing: unbox -> modify -> rebox
        [index](ImmerValue obj, ImmerValue value) -> ImmerValue {
            if (auto* boxed_vec = obj.get_if<BoxedValueVector>()) {
                const auto& vec = boxed_vec->get();
                if (index < vec.size()) {
                    auto new_vec = vec.set(index, std::move(value));
                    return ImmerValue{BoxedValueVector{std::move(new_vec)}};
                }
            }
            return obj;
        });
}

/// @brief Build a lens from a runtime Path (with caching)
[[nodiscard]] LAGER_EXT_API LagerValueLens lager_path_lens(const Path& path);

} // namespace lager_ext

// Include static_path.h for StaticPath support
// This is placed after closing namespace to avoid circular dependencies
#include <lager_ext/static_path.h>

namespace lager_ext {

/// @brief Convert a compile-time string literal path to a lager lens
/// @tparam Ptr JSON Pointer style path string (e.g., "/users/0/name")
/// @return Type-erased LagerValueLens that can be used with lager::view/set/over
///
/// @example
/// auto lens = static_path_lens<"/users/0/name">();
/// ImmerValue name = lager::view(lens, root);
/// ImmerValue updated = lager::set(lens, root, ImmerValue{"Alice"});
template <FixedString Ptr>
[[nodiscard]] LagerValueLens static_path_lens() {
    using PathType = StaticPath<Ptr>;
    return lager_path_lens(PathType::to_runtime_path());
}

namespace detail {
auto element_to_lens(StringLike auto&& elem) {
    return key_lens(std::forward<decltype(elem)>(elem));
}
auto element_to_lens(IndexLike auto&& elem) {
    return index_lens(static_cast<std::size_t>(elem));
}
} // namespace detail

template <PathElementType... Elements>
auto static_path_lens(Elements&&... elements) {
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
//   ImmerValue name = lager::view(path, state);      // Direct use as lens
//   ImmerValue updated = lager::set(path, state, ImmerValue{"Alice"});
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
    template <typename F>
    auto operator()(F&& f) const {
        return [this, f = std::forward<F>(f)](const ImmerValue& whole) {
            // Get the part at this path
            ImmerValue part = this->get(whole);
            // Apply the functor and get the setter
            return f(std::move(part))([this, &whole](ImmerValue new_part) { return this->set(whole, std::move(new_part)); });
        };
    }

    // ============================================================
    // key() methods - Template overloads for zero-copy literals
    //
    // Three categories of string keys:
    // 1. String literals (const char(&)[N]): Zero-copy, direct reference
    // 2. std::string&&: Move into Path's internal storage
    // 3. std::string_view/const char*/std::string&: Copy into storage
    //
    // The template overload captures string literals at compile time,
    // preserving their static storage duration information so Path
    // can reference them directly without copying.
    // ============================================================

    /// Add a string literal key (zero-copy!)
    /// @note Template captures literal type to enable zero-allocation path building
    /// @example path.key("users")  // No allocation!
    template <std::size_t N>
    [[nodiscard]] PathLens key(const char (&literal)[N]) const& {
        PathLens result = *this;
        result.path_.push_back(literal); // Matches Path's template overload
        return result;
    }

    template <std::size_t N>
    [[nodiscard]] PathLens key(const char (&literal)[N]) && {
        path_.push_back(literal);
        return std::move(*this);
    }

    /// Add a string key by moving ownership
    /// @note String content is transferred to Path's internal storage
    [[nodiscard]] PathLens key(std::string&& k) const& {
        PathLens result = *this;
        result.path_.push_back(std::move(k));
        return result;
    }

    [[nodiscard]] PathLens key(std::string&& k) && {
        path_.push_back(std::move(k));
        return std::move(*this);
    }

    /// Add a string key by copying (fallback for runtime strings)
    /// @note Accepts any string-like type via implicit conversion to string_view
    [[nodiscard]] PathLens key(std::string_view k) const& {
        PathLens result = *this;
        result.path_.push_back(k);
        return result;
    }

    [[nodiscard]] PathLens key(std::string_view k) && {
        path_.push_back(k);
        return std::move(*this);
    }
    [[nodiscard]] PathLens index(std::size_t i) const& {
        PathLens result = *this;
        result.path_.push_back(i);
        return result;
    }
    [[nodiscard]] PathLens index(std::size_t i) && {
        path_.push_back(i);
        return std::move(*this);
    }

    /// Operator/ for chained navigation (literal version - zero-copy!)
    template <std::size_t N>
    [[nodiscard]] PathLens operator/(const char (&literal)[N]) const& {
        return key(literal);
    }
    template <std::size_t N>
    [[nodiscard]] PathLens operator/(const char (&literal)[N]) && {
        return std::move(*this).key(literal);
    }

    /// Operator/ for dynamic strings
    [[nodiscard]] PathLens operator/(std::string&& k) const& { return key(std::move(k)); }
    [[nodiscard]] PathLens operator/(std::string&& k) && { return std::move(*this).key(std::move(k)); }
    [[nodiscard]] PathLens operator/(std::string_view k) const& { return key(k); }
    [[nodiscard]] PathLens operator/(std::string_view k) && { return std::move(*this).key(k); }

    /// Operator/ for indices
    [[nodiscard]] PathLens operator/(std::size_t i) const& { return index(i); }
    [[nodiscard]] PathLens operator/(std::size_t i) && { return std::move(*this).index(i); }
    [[nodiscard]] PathLens operator/(int i) const& { return index(static_cast<std::size_t>(i)); }
    [[nodiscard]] PathLens operator/(int i) && { return std::move(*this).index(static_cast<std::size_t>(i)); }

    [[nodiscard]] const Path& path() const noexcept { return path_; }
    [[nodiscard]] Path& path() noexcept { return path_; }

    /// Convert to LagerValueLens (for compatibility, but usually not needed)
    [[nodiscard]] LagerValueLens to_lens() const { return lager_path_lens(path_); }

    /// Direct get/set/over operations (more efficient than through to_lens())
    [[nodiscard]] ImmerValue get(const ImmerValue& root) const;
    [[nodiscard]] ImmerValue set(const ImmerValue& root, ImmerValue new_val) const;

    template <typename Fn>
    [[nodiscard]] ImmerValue over(const ImmerValue& root, Fn&& fn) const {
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
        if (path_.empty())
            return *this;
        PathLens result;
        result.path_.assign(path_.begin(), path_.end() - 1);
        return result;
    }
    [[nodiscard]] std::string to_string() const { return path_.to_dot_notation(); }

    bool operator==(const PathLens& other) const { return path_ == other.path_; }
    bool operator!=(const PathLens& other) const { return path_ != other.path_; }
    bool operator==(const Path& other) const { return path_ == other; }
    bool operator!=(const Path& other) const { return path_ != other; }

private:
    Path path_;
};

using PathBuilder = PathLens; // Backward compatibility alias

inline const PathLens root;

template <PathElementType... Elements>
[[nodiscard]] PathLens make_path(Elements&&... elements) {
    PathLens builder;
    (builder.path().push_back([](auto&& e) -> PathElement {
        using T = std::decay_t<decltype(e)>;
        if constexpr (StringLike<T>) {
            return std::string{std::forward<decltype(e)>(e)};
        } else {
            return static_cast<std::size_t>(e);
        }
    }(std::forward<Elements>(elements))),
     ...);
    return builder;
}

template <PathElementType... Elements>
[[nodiscard]] ImmerValue get_at(const ImmerValue& root, Elements&&... path_elements) {
    return make_path(std::forward<Elements>(path_elements)...).get(root);
}

template <PathElementType... Elements>
[[nodiscard]] ImmerValue set_at(const ImmerValue& root, ImmerValue new_val, Elements&&... path_elements) {
    return make_path(std::forward<Elements>(path_elements)...).set(root, std::move(new_val));
}

template <typename Fn, PathElementType... Elements>
[[nodiscard]] ImmerValue over_at(const ImmerValue& root, Fn&& fn, Elements&&... path_elements) {
    return make_path(std::forward<Elements>(path_elements)...).over(root, std::forward<Fn>(fn));
}

enum class PathErrorCode {
    Success = 0,
    KeyNotFound,     // Map key doesn't exist
    IndexOutOfRange, // Vector/array index out of bounds
    TypeMismatch,    // Expected container type, got primitive
    NullValue,       // Attempted access on null value
    EmptyPath,
};

struct PathAccessResult {
    ImmerValue value;          // The accessed value (or null on error)
    bool success = false; // Whether the access succeeded
    PathErrorCode error_code = PathErrorCode::Success;
    std::string error_message; // Human-readable error description
    Path resolved_path;        // The portion of path that was successfully resolved
    std::size_t failed_at_index = 0;

    explicit operator bool() const noexcept { return success; }
    const ImmerValue& get() const {
        if (!success) {
            throw std::runtime_error("Path access failed: " + error_message);
        }
        return value;
    }
    ImmerValue get_or(ImmerValue default_val) const { return success ? value : std::move(default_val); }
};

[[nodiscard]] LAGER_EXT_API PathAccessResult get_at_path_safe(const ImmerValue& root, const Path& path);
[[nodiscard]] LAGER_EXT_API PathAccessResult set_at_path_safe(const ImmerValue& root, const Path& path, ImmerValue new_val);

// ============================================================
// ZoomedValue - A focused view into a ImmerValue tree
//
// Similar to lager's cursor.zoom() concept, ZoomedValue provides
// a "zoomed-in" view of a ImmerValue at a specific path. Unlike cursor,
// ZoomedValue is a lightweight, stack-allocated object that doesn't
// require a store.
//
// Key differences from lager cursor:
// - No subscription/watch mechanism (ImmerValue is immutable, no store)
// - Stack-allocated, zero-overhead abstraction
// - set() returns new root, not modifying in place
//
// Example:
//   ImmerValue state = /* ... */;
//   ZoomedValue users = ZoomedValue(state) / "users";
//   ZoomedValue first_user = users / 0;
//   ImmerValue name = (first_user / "name").get();
//   ImmerValue new_state = (first_user / "name").set(ImmerValue{"Alice"});
// ============================================================
class ZoomedValue {
public:
    /// Create a zoomed view at the root
    explicit ZoomedValue(const ImmerValue& root) : root_(&root) {}

    /// Create a zoomed view at a specific path
    ZoomedValue(const ImmerValue& root, Path path) : root_(&root), path_(std::move(path)) {}

    // ============================================================
    // Navigation - Zoom further into the structure
    //
    // Template overloads for zero-copy literal support (same as PathLens).
    // ============================================================

    /// Zoom into a map key using string literal (zero-copy!)
    template <std::size_t N>
    [[nodiscard]] ZoomedValue key(const char (&literal)[N]) const& {
        ZoomedValue result = *this;
        result.path_.push_back(literal);
        return result;
    }

    template <std::size_t N>
    [[nodiscard]] ZoomedValue key(const char (&literal)[N]) && {
        path_.push_back(literal);
        return std::move(*this);
    }

    /// Zoom into a map key by moving ownership
    [[nodiscard]] ZoomedValue key(std::string&& k) const& {
        ZoomedValue result = *this;
        result.path_.push_back(std::move(k));
        return result;
    }

    [[nodiscard]] ZoomedValue key(std::string&& k) && {
        path_.push_back(std::move(k));
        return std::move(*this);
    }

    /// Zoom into a map key by copying (fallback)
    [[nodiscard]] ZoomedValue key(std::string_view k) const& {
        ZoomedValue result = *this;
        result.path_.push_back(k);
        return result;
    }

    [[nodiscard]] ZoomedValue key(std::string_view k) && {
        path_.push_back(k);
        return std::move(*this);
    }

    /// Zoom into a vector index
    [[nodiscard]] ZoomedValue index(std::size_t i) const& {
        ZoomedValue result = *this;
        result.path_.push_back(i);
        return result;
    }
    [[nodiscard]] ZoomedValue index(std::size_t i) && {
        path_.push_back(i);
        return std::move(*this);
    }

    /// Operator/ for chained navigation (literal version - zero-copy!)
    template <std::size_t N>
    [[nodiscard]] ZoomedValue operator/(const char (&literal)[N]) const& {
        return key(literal);
    }
    template <std::size_t N>
    [[nodiscard]] ZoomedValue operator/(const char (&literal)[N]) && {
        return std::move(*this).key(literal);
    }

    /// Operator/ for dynamic strings
    [[nodiscard]] ZoomedValue operator/(std::string&& k) const& { return key(std::move(k)); }
    [[nodiscard]] ZoomedValue operator/(std::string&& k) && { return std::move(*this).key(std::move(k)); }
    [[nodiscard]] ZoomedValue operator/(std::string_view k) const& { return key(k); }
    [[nodiscard]] ZoomedValue operator/(std::string_view k) && { return std::move(*this).key(k); }

    /// Operator/ for indices
    [[nodiscard]] ZoomedValue operator/(std::size_t i) const& { return index(i); }
    [[nodiscard]] ZoomedValue operator/(std::size_t i) && { return std::move(*this).index(i); }
    [[nodiscard]] ZoomedValue operator/(int i) const& { return index(static_cast<std::size_t>(i)); }
    [[nodiscard]] ZoomedValue operator/(int i) && { return std::move(*this).index(static_cast<std::size_t>(i)); }

    // ============================================================
    // Access Operations
    // ============================================================

    /// Get the value at the current zoom path
    [[nodiscard]] ImmerValue get() const { return get_at_path(*root_, path_); }

    /// Set the value at the current zoom path (returns new root)
    [[nodiscard]] ImmerValue set(ImmerValue new_val) const { return set_at_path(*root_, path_, std::move(new_val)); }

    /// Update the value using a function (returns new root)
    template <typename Fn>
    [[nodiscard]] ImmerValue over(Fn&& fn) const {
        return set(std::forward<Fn>(fn)(get()));
    }

    /// Dereference operator - same as get()
    [[nodiscard]] ImmerValue operator*() const { return get(); }

    // ============================================================
    // Introspection
    // ============================================================

    /// Get the root value
    [[nodiscard]] const ImmerValue& root() const noexcept { return *root_; }

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
        if (path_.empty())
            return *this;
        ZoomedValue result = *this;
        result.path_.pop_back();
        return result;
    }

    /// Create a new ZoomedValue with updated root
    /// Useful after set() to continue working with the new state
    [[nodiscard]] ZoomedValue with_root(const ImmerValue& new_root) const { return ZoomedValue{new_root, path_}; }

private:
    const ImmerValue* root_ = nullptr;
    Path path_;
};

/// Create a ZoomedValue at root
[[nodiscard]] inline ZoomedValue zoom(const ImmerValue& root) {
    return ZoomedValue{root};
}

/// Create a ZoomedValue at a specific path
[[nodiscard]] inline ZoomedValue zoom(const ImmerValue& root, const Path& path) {
    return ZoomedValue{root, path};
}

/// Create a ZoomedValue using variadic path elements
template <PathElementType... Elements>
[[nodiscard]] ZoomedValue zoom(const ImmerValue& root, Elements&&... path_elements) {
    return ZoomedValue{root, make_path(std::forward<Elements>(path_elements)...).path()};
}

} // namespace lager_ext
