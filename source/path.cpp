// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path.cpp
/// @brief Implementation of Path class for dynamic paths.

#include <lager_ext/path.h>

namespace lager_ext {

namespace {

/// Check if string contains only digits (for index detection)
bool is_numeric(std::string_view sv) noexcept {
    if (sv.empty()) return false;
    for (char c : sv) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

/// Parse string_view to size_t (assumes is_numeric() is true)
std::size_t parse_index(std::string_view sv) noexcept {
    std::size_t result = 0;
    for (char c : sv) {
        result = result * 10 + static_cast<std::size_t>(c - '0');
    }
    return result;
}

} // anonymous namespace

// ============================================================
// Path - Copy/Move constructors and assignment
// ============================================================

Path::Path(const Path& other)
    : storage_(other.storage_)
    , elements_(other.elements_)
    , key_spans_(other.key_spans_)
{
    // Rebuild string_views to point to our own storage
    rebuild_views();
    
    // Update original_path_ if it pointed to storage_
    if (!other.original_path_.empty() && !storage_.empty()) {
        original_path_ = storage_;
    }
}

Path::Path(Path&& other) noexcept
    : storage_(std::move(other.storage_))
    , elements_(std::move(other.elements_))
    , key_spans_(std::move(other.key_spans_))
    , original_path_(other.original_path_)
{
    // After move, rebuild views because storage pointer changed
    rebuild_views();
    
    // Update original_path_ to point to our storage_
    if (!original_path_.empty() && !storage_.empty()) {
        original_path_ = storage_;
    }
    
    other.original_path_ = {};
}

Path& Path::operator=(const Path& other) {
    if (this != &other) {
        Path temp(other);
        *this = std::move(temp);
    }
    return *this;
}

Path& Path::operator=(Path&& other) noexcept {
    if (this != &other) {
        storage_ = std::move(other.storage_);
        elements_ = std::move(other.elements_);
        key_spans_ = std::move(other.key_spans_);
        original_path_ = other.original_path_;
        
        rebuild_views();
        
        if (!original_path_.empty() && !storage_.empty()) {
            original_path_ = storage_;
        }
        
        other.original_path_ = {};
    }
    return *this;
}

// ============================================================
// Path - Constructor from PathView
// ============================================================

Path::Path(PathView view) {
    reserve(view.size());
    for (const auto& elem : view) {
        push_back(elem);
    }
}

// ============================================================
// Path - Zero-copy construction from literal
// ============================================================

Path Path::from_literal_impl(std::string_view path_str) {
    Path result;
    
    // For literals, we DON'T copy to storage_ - views point directly to the literal
    // This is safe because literals have static storage duration
    result.original_path_ = path_str;
    
    if (path_str.empty()) {
        return result;
    }

    // Skip leading '/' for parsing
    std::string_view parse_str = path_str;
    if (!parse_str.empty() && parse_str[0] == '/') {
        parse_str = parse_str.substr(1);
    }

    if (parse_str.empty()) {
        return result;
    }

    // Count segments for reserve
    std::size_t segment_count = 1;
    for (char c : parse_str) {
        if (c == '/') ++segment_count;
    }
    result.elements_.reserve(segment_count);
    // Note: key_spans_ stays empty for literals - we don't need to rebuild

    // Parse segments - string_views point directly to the literal
    while (!parse_str.empty()) {
        auto pos = parse_str.find('/');
        std::string_view segment = (pos == std::string_view::npos)
                                    ? parse_str
                                    : parse_str.substr(0, pos);

        if (is_numeric(segment)) {
            result.elements_.emplace_back(parse_index(segment));
        } else {
            result.elements_.emplace_back(segment);  // Points to static literal!
        }

        if (pos == std::string_view::npos) break;
        parse_str = parse_str.substr(pos + 1);
    }

    return result;
}

// ============================================================
// Path - Construction from strings
// ============================================================

Path::Path(std::string&& path_str) {
    // Take ownership of the string
    storage_ = std::move(path_str);
    original_path_ = storage_;
    parse_path_string(storage_);
}

Path::Path(std::string_view path_str) {
    // Copy the string into storage_
    storage_ = std::string(path_str);
    original_path_ = storage_;
    parse_path_string(storage_);
}

void Path::parse_path_string(std::string_view source) {
    if (source.empty()) {
        return;
    }

    // Skip leading '/' for parsing
    std::string_view parse_str = source;
    if (!parse_str.empty() && parse_str[0] == '/') {
        parse_str = parse_str.substr(1);
    }

    if (parse_str.empty()) {
        return;
    }

    // Count segments for reserve
    std::size_t segment_count = 1;
    for (char c : parse_str) {
        if (c == '/') ++segment_count;
    }
    elements_.reserve(segment_count);
    key_spans_.reserve(segment_count);

    // Parse segments
    while (!parse_str.empty()) {
        auto pos = parse_str.find('/');
        std::string_view segment = (pos == std::string_view::npos)
                                    ? parse_str
                                    : parse_str.substr(0, pos);

        if (is_numeric(segment)) {
            elements_.emplace_back(parse_index(segment));
            // No span for index elements
        } else {
            // Calculate offset from storage_.data()
            std::size_t offset = static_cast<std::size_t>(segment.data() - storage_.data());
            std::size_t elem_idx = elements_.size();
            key_spans_.push_back(KeySpan{elem_idx, offset, segment.size()});
            elements_.emplace_back(segment);
        }

        if (pos == std::string_view::npos) break;
        parse_str = parse_str.substr(pos + 1);
    }
}

// ============================================================
// Path - Modifiers
// ============================================================

Path& Path::push_back_literal(std::string_view literal_sv) {
    original_path_ = {};  // Path modified, invalidate cached source
    
    // Zero-copy: directly reference the literal (static storage duration)
    // No need to copy to storage_ or track in key_spans_
    elements_.emplace_back(literal_sv);
    return *this;
}

Path& Path::push_back(std::string&& key) {
    original_path_ = {};  // Path modified, invalidate cached source
    
    // Move the string content into storage_
    std::size_t offset = storage_.size();
    std::size_t len = key.size();
    
    // If storage_ is empty and key is large enough, we can take ownership directly
    if (storage_.empty() && key.capacity() >= len) {
        storage_ = std::move(key);
    } else {
        storage_.append(key);
    }
    
    std::size_t elem_idx = elements_.size();
    key_spans_.push_back(KeySpan{elem_idx, offset, len});
    elements_.emplace_back(std::string_view{storage_.data() + offset, len});
    return *this;
}

Path& Path::push_back(std::string_view key) {
    original_path_ = {};  // Path modified, invalidate cached source
    
    // Copy the string content into storage_
    std::size_t offset = storage_.size();
    std::size_t len = key.size();
    storage_.append(key);
    
    std::size_t elem_idx = elements_.size();
    key_spans_.push_back(KeySpan{elem_idx, offset, len});
    
    // Create view pointing to the stored copy
    elements_.emplace_back(std::string_view{storage_.data() + offset, len});
    return *this;
}

Path& Path::push_back(std::size_t index) {
    original_path_ = {};  // Path modified, invalidate cached source
    elements_.emplace_back(index);
    return *this;
}

void Path::pop_back() {
    if (elements_.empty()) return;
    
    original_path_ = {};  // Path modified, invalidate cached source
    
    std::size_t last_idx = elements_.size() - 1;
    
    // Check if the last element is tracked in key_spans_ (i.e., stored in storage_)
    if (!key_spans_.empty() && key_spans_.back().element_idx == last_idx) {
        // Shrink storage_ if this was the last key
        const auto& span = key_spans_.back();
        if (span.offset + span.length == storage_.size()) {
            storage_.resize(span.offset);
        }
        key_spans_.pop_back();
    }
    // else: it's a literal or index, no storage cleanup needed
    
    elements_.pop_back();
}

void Path::clear() noexcept {
    original_path_ = {};
    storage_.clear();
    elements_.clear();
    key_spans_.clear();
}

void Path::reserve(std::size_t n) {
    elements_.reserve(n);
    key_spans_.reserve(n);
    storage_.reserve(n * 8);  // Estimate average key length
}

// ============================================================
// Path - Internal helpers
// ============================================================

void Path::rebuild_views() {
    if (elements_.empty() || key_spans_.empty()) return;
    
    // Rebuild string_view elements that point to storage_ (tracked by key_spans_)
    // Each KeySpan knows exactly which element index it corresponds to
    // Literals are NOT tracked and don't need rebuilding
    for (const auto& span : key_spans_) {
        elements_[span.element_idx] = std::string_view{
            storage_.data() + span.offset, 
            span.length
        };
    }
}

// ============================================================
// Path - Comparison operators
// ============================================================

bool Path::operator==(const Path& other) const noexcept {
    return PathView(*this) == PathView(other);
}

bool Path::operator==(PathView other) const noexcept {
    return PathView(*this) == other;
}

// ============================================================
// Path - Serialization
// ============================================================

std::string Path::to_string_path() const {
    // Fast path: return cached original if available
    if (!original_path_.empty()) {
        return std::string(original_path_);
    }
    
    // Fall back to PathView serialization
    return view().to_string_path();
}

// ============================================================
// PathView - Serialization
// ============================================================

std::string PathView::to_string_path() const {
    if (empty()) {
        return "";  // Root reference
    }

    std::string result;
    result.reserve(size() * 10);  // Estimate
    
    for (const auto& elem : *this) {
        result += '/';
        if (auto* key = std::get_if<std::string_view>(&elem)) {
            result += *key;
        } else {
            result += std::to_string(std::get<std::size_t>(elem));
        }
    }
    return result;
}

std::string PathView::to_dot_notation() const {
    if (empty()) {
        return "(root)";
    }
    
    std::string result;
    result.reserve(size() * 10);  // Estimate
    
    for (const auto& elem : *this) {
        if (auto* key = std::get_if<std::string_view>(&elem)) {
            result += '.';
            result += *key;
        } else {
            result += '[';
            result += std::to_string(std::get<std::size_t>(elem));
            result += ']';
        }
    }
    
    return result;
}

} // namespace lager_ext