// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_types.h
/// @brief Path types for navigating Value trees with zero-copy support.
///
/// This file provides two complementary path types:
///
/// - **PathView**: Zero-allocation path for static/literal paths (most common)
/// - **Path**: Owning path for dynamic paths (when keys come from runtime)
///
/// ## Usage Examples
///
/// ### Static paths (zero allocation, maximum performance)
/// ```cpp
/// using namespace std::string_view_literals;
/// auto val = get_at_path(root, {{"users"sv, 0, "name"sv}});
/// ```
///
/// ### Dynamic paths (safe, owns memory)
/// ```cpp
/// Path path;
/// path.push_back(get_key_from_input());  // copied into internal storage
/// path.push_back(0);
/// auto val = get_at_path(root, path);    // Path implicitly converts to PathView
/// ```
///
/// ## Design Philosophy
///
/// - PathElement uses `std::string_view` for minimal size (16 bytes vs 32 bytes for std::string)
/// - PathView is a non-owning span over PathElements - perfect for literals
/// - Path owns a contiguous string buffer for all keys, avoiding per-key heap allocations

#pragma once

#include <lager_ext/api.h>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lager_ext {

// ============================================================
// PathElement - Unified type using string_view
//
// Size: ~24 bytes (variant<string_view(16), size_t(8)> + discriminant)
// Compare to: variant<string(32), size_t(8)> = ~40 bytes
// ============================================================

/// A single path element: either a string key or a numeric index
using PathElement = std::variant<std::string_view, std::size_t>;

// ============================================================
// PathView - Zero-copy path for static/literal paths
//
// This is the primary interface for path operations. Use this for:
// - Literal paths: {"users", 0, "name"}
// - Constexpr paths
// - Any path where keys have static storage duration
//
// WARNING: PathView does NOT own the string data. Ensure the
// underlying strings outlive the PathView.
// ============================================================

/// Non-owning view over a sequence of PathElements
class PathView {
public:
    using value_type = PathElement;
    using iterator = const PathElement*;
    using const_iterator = const PathElement*;
    using size_type = std::size_t;

    /// Default constructor - empty path
    constexpr PathView() noexcept = default;

    /// Construct from initializer list (for literal paths)
    /// @note The initializer_list's backing array has temporary lifetime,
    ///       but this is safe when used directly in function calls.
    constexpr PathView(std::initializer_list<PathElement> init) noexcept
        : data_(init.begin())
        , size_(init.size())
    {}

    /// Construct from pointer and size
    constexpr PathView(const PathElement* data, std::size_t size) noexcept
        : data_(data)
        , size_(size)
    {}

    /// Construct from any contiguous container with data() and size()
    template <typename Container>
        requires requires(const Container& c) {
            { c.data() } -> std::convertible_to<const PathElement*>;
            { c.size() } -> std::convertible_to<std::size_t>;
        }
    constexpr PathView(const Container& container) noexcept
        : data_(container.data())
        , size_(container.size())
    {}

    // Iterators
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return data_ + size_; }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }

    // Capacity
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    // Element access
    [[nodiscard]] constexpr const PathElement& operator[](std::size_t i) const noexcept {
        return data_[i];
    }

    [[nodiscard]] constexpr const PathElement& front() const noexcept { return data_[0]; }
    [[nodiscard]] constexpr const PathElement& back() const noexcept { return data_[size_ - 1]; }

    // Subviews
    [[nodiscard]] constexpr PathView subpath(std::size_t start) const noexcept {
        if (start >= size_) return {};
        return {data_ + start, size_ - start};
    }

    [[nodiscard]] constexpr PathView subpath(std::size_t start, std::size_t count) const noexcept {
        if (start >= size_) return {};
        if (start + count > size_) count = size_ - start;
        return {data_ + start, count};
    }

    // Pointer to underlying data
    [[nodiscard]] constexpr const PathElement* data() const noexcept { return data_; }

private:
    const PathElement* data_ = nullptr;
    std::size_t size_ = 0;
};

// ============================================================
// Path - Owning path for dynamic paths
//
// Use this when path keys come from runtime data (user input,
// computed strings, etc.). All string keys are stored in a
// contiguous buffer for cache efficiency.
//
// Features:
// - Single heap allocation for all keys (amortized)
// - Implicit conversion to PathView for API compatibility
// - Safe: owns all string data
// ============================================================

/// Owning path that stores string keys in a contiguous buffer
class LAGER_EXT_API Path {
public:
    using value_type = PathElement;
    using iterator = std::vector<PathElement>::const_iterator;
    using const_iterator = std::vector<PathElement>::const_iterator;
    using size_type = std::size_t;

    /// Default constructor - empty path
    Path() = default;

    /// Copy constructor - deep copies storage
    Path(const Path& other);

    /// Move constructor
    Path(Path&& other) noexcept;

    /// Copy assignment
    Path& operator=(const Path& other);

    /// Move assignment
    Path& operator=(Path&& other) noexcept;

    /// Destructor
    ~Path() = default;

    /// Construct from initializer list
    /// All string_views are copied into internal storage
    Path(std::initializer_list<PathElement> init);

    /// Construct from PathView
    /// All string_views are copied into internal storage
    explicit Path(PathView view);

    // ============================================================
    // Modifiers
    // ============================================================

    /// Add a string key (copied into internal storage)
    Path& push_back(std::string_view key);

    /// Add a std::string key (copied into internal storage)
    /// This overload resolves ambiguity when passing std::string
    Path& push_back(const std::string& key) { return push_back(std::string_view{key}); }

    /// Add a numeric index
    Path& push_back(std::size_t index);

    /// Add a PathElement (string_view keys are copied)
    Path& push_back(const PathElement& elem);

    /// Remove the last element
    void pop_back();

    /// Clear all elements
    void clear() noexcept;

    /// Reserve space for n elements
    void reserve(std::size_t n);

    /// Assign from an iterator range
    template<typename InputIt>
    void assign(InputIt first, InputIt last) {
        clear();
        for (; first != last; ++first) {
            push_back(*first);
        }
    }

    // ============================================================
    // Conversion
    // ============================================================

    /// Implicit conversion to PathView
    operator PathView() const noexcept {
        return PathView{elements_.data(), elements_.size()};
    }

    /// Explicit view accessor
    [[nodiscard]] PathView view() const noexcept { return *this; }

    // ============================================================
    // Iterators
    // ============================================================

    [[nodiscard]] const_iterator begin() const noexcept { return elements_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return elements_.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return elements_.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return elements_.cend(); }

    // ============================================================
    // Capacity
    // ============================================================

    [[nodiscard]] std::size_t size() const noexcept { return elements_.size(); }
    [[nodiscard]] bool empty() const noexcept { return elements_.empty(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return elements_.capacity(); }

    // ============================================================
    // Element access
    // ============================================================

    [[nodiscard]] const PathElement& operator[](std::size_t i) const noexcept {
        return elements_[i];
    }

    [[nodiscard]] const PathElement& front() const noexcept { return elements_.front(); }
    [[nodiscard]] const PathElement& back() const noexcept { return elements_.back(); }
    [[nodiscard]] const PathElement* data() const noexcept { return elements_.data(); }

    // ============================================================
    // Comparison
    // ============================================================

    [[nodiscard]] bool operator==(const Path& other) const noexcept;
    [[nodiscard]] bool operator!=(const Path& other) const noexcept { return !(*this == other); }

    [[nodiscard]] bool operator==(PathView other) const noexcept;
    [[nodiscard]] bool operator!=(PathView other) const noexcept { return !(*this == other); }

private:
    /// Rebuild all string_view elements to point to storage_
    void rebuild_views();

    /// Copy a string_view into storage and return a view pointing to the copy
    std::string_view store_key(std::string_view key);

    std::string storage_;               ///< Contiguous buffer for all string keys
    std::vector<PathElement> elements_; ///< Path elements (string_views point to storage_)
    std::vector<std::size_t> offsets_;  ///< Offset of each string key in storage_
};

// ============================================================
// PathView comparison operators
// ============================================================

[[nodiscard]] inline bool operator==(PathView a, PathView b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

[[nodiscard]] inline bool operator!=(PathView a, PathView b) noexcept {
    return !(a == b);
}

// ============================================================
// Utility functions
// ============================================================

/// Convert a PathView to a human-readable string (e.g., ".users[0].name")
[[nodiscard]] LAGER_EXT_API std::string path_to_string(PathView path);

} // namespace lager_ext
