// lager_lens.h - Type-erased lenses using lager::lens<Value, Value>

#pragma once

#include <lager_ext/concepts.h>  // C++20 Concepts (StringLike, IndexType, PathElementType, etc.)
#include <lager_ext/value.h>
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

[[nodiscard]] auto key_lens(const std::string& key);
[[nodiscard]] auto index_lens(std::size_t index);
[[nodiscard]] LagerValueLens lager_key_lens(const std::string& key);
[[nodiscard]] LagerValueLens lager_index_lens(std::size_t index);
[[nodiscard]] LagerValueLens lager_path_lens(const Path& path);

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

void clear_lens_cache();
[[nodiscard]] LensCacheStats get_lens_cache_stats();

class PathLens {
public:
    PathLens() = default;
    explicit PathLens(Path path) : path_(std::move(path)) {}

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
    [[nodiscard]] LagerValueLens to_lens() const { return lager_path_lens(path_); }
    [[nodiscard]] Value get(const Value& root) const { return lager::view(to_lens(), root); }
    [[nodiscard]] Value set(const Value& root, Value new_val) const { return lager::set(to_lens(), root, std::move(new_val)); }

    template<typename Fn>
    [[nodiscard]] Value over(const Value& root, Fn&& fn) const { return lager::over(to_lens(), root, std::forward<Fn>(fn)); }

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

[[nodiscard]] PathAccessResult get_at_path_safe(const Value& root, const Path& path);
[[nodiscard]] PathAccessResult set_at_path_safe(const Value& root, const Path& path, Value new_val);

void demo_lager_lens();

} // namespace lager_ext
