// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path.h
/// @brief Core path types for navigating Value trees with zero-copy support.
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
/// - Path can own a source string for zero-copy parsing of string paths

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

    // ============================================================
    // Serialization
    // ============================================================

    /// Convert to JSON Pointer format (RFC 6901, e.g., "/users/0/name")
    [[nodiscard]] LAGER_EXT_API std::string to_string_path() const;

    /// Convert to dot notation format (e.g., ".users[0].name")
    /// @note Returns "(root)" for empty path
    [[nodiscard]] LAGER_EXT_API std::string to_dot_notation() const;

private:
    const PathElement* data_ = nullptr;
    std::size_t size_ = 0;
};

// ============================================================
// Path - Owning path for dynamic paths
//
// Use this when path keys come from runtime data (user input,
// computed strings, etc.). 
//
// Features:
// - Zero-copy parsing: when constructed from string literal or rvalue string,
//   no per-segment string allocation is needed
// - Implicit conversion to PathView for API compatibility
// - Safe: owns all string data when needed
//
// Storage modes:
// 1. External reference: string_views point to external static storage (literals)
// 2. Owned source: string_views point to owned_source_ (moved or copied string)
// 3. Per-segment storage: string_views point to segment_storage_ (push_back)
// ============================================================

/// Owning path that supports zero-copy parsing
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

    /// Construct from PathView
    /// All string_views are copied into internal storage
    explicit Path(PathView view);

    // ============================================================
    // Zero-copy construction from string path
    // ============================================================

    /// Zero-copy construction from string literal
    /// @note String literals have static storage duration, so no copy needed
    /// @param path_str Path string literal (e.g., "/users/0/name")
    /// @example Path path{"/users/0/name"};  // Zero-copy!
    template<std::size_t N>
    explicit Path(const char (&path_str)[N])
        : Path(from_literal_impl(std::string_view{path_str, N - 1}))
    {}

    /// Zero-copy construction from rvalue string
    /// @note Takes ownership of the string, string_views point into it
    /// @param path_str Path string (e.g., "/users/0/name")
    explicit Path(std::string&& path_str);

    /// Construction from string_view (copies the string)
    /// @note Use this when the source string may be temporary
    /// @param path_str Path string (e.g., "/users/0/name")
    explicit Path(std::string_view path_str);

    // ============================================================
    // Modifiers
    // ============================================================

    /// Add a string key (copied into internal storage)
    Path& push_back(std::string_view key);

    /// Add a numeric index
    Path& push_back(std::size_t index);

    /// Add a PathElement (dispatches to appropriate overload)
    /// @note Uses requires clause to avoid ambiguity with string-like types
    template<typename T>
        requires std::same_as<std::decay_t<T>, PathElement>
    Path& push_back(T&& elem) {
        if (auto* sv = std::get_if<std::string_view>(&elem)) {
            return push_back(*sv);
        } else {
            return push_back(std::get<std::size_t>(elem));
        }
    }

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

    // ============================================================
    // Serialization
    // ============================================================

    /// Convert to JSON Pointer format (RFC 6901, e.g., "/users/0/name")
    /// @note If Path was constructed from a string and not modified,
    ///       returns the original string directly (zero-copy).
    ///       Otherwise, serializes from elements.
    [[nodiscard]] std::string to_string_path() const;

    /// Convert to dot notation format (e.g., ".users[0].name")
    /// @note Returns "(root)" for empty path
    [[nodiscard]] std::string to_dot_notation() const { return view().to_dot_notation(); }

private:
    /// Internal implementation for literal construction
    static Path from_literal_impl(std::string_view path_str);

    /// Parse path string and populate elements
    void parse_path_string(std::string_view source);

    /// Rebuild all string_view elements to point to storage_
    void rebuild_views();

    /// Unified storage for all string keys
    /// Contains either: parsed path string, or concatenated push_back keys
    std::string storage_;

    /// Path elements (string_views point to storage_ or static literals)
    std::vector<PathElement> elements_;

    /// (offset, length) pairs for each string key in storage_
    /// Used to rebuild string_views after copy/move
    std::vector<std::pair<std::size_t, std::size_t>> key_spans_;

    /// Original path string for fast serialization
    /// Points to storage_ (if parsed) or static literal
    /// Empty if path was modified or built via push_back
    std::string_view original_path_;
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

} // namespace lager_ext