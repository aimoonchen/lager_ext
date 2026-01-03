// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file path_types.cpp
/// @brief Implementation of Path class for dynamic paths.

#include <lager_ext/path_types.h>

namespace lager_ext {

// ============================================================
// Path - Copy/Move constructors and assignment
// ============================================================

Path::Path(const Path& other)
    : storage_(other.storage_)
    , elements_()
    , offsets_(other.offsets_)
{
    // Reserve space for elements
    elements_.reserve(other.elements_.size());
    
    // Rebuild elements with string_views pointing to our own storage
    for (std::size_t i = 0, offset_idx = 0; i < other.elements_.size(); ++i) {
        const auto& elem = other.elements_[i];
        if (auto* sv = std::get_if<std::string_view>(&elem)) {
            // Get offset and length from the original
            auto offset = offsets_[offset_idx];
            auto len = sv->size();
            elements_.emplace_back(std::string_view{storage_.data() + offset, len});
            ++offset_idx;
        } else {
            elements_.push_back(elem);
        }
    }
}

Path::Path(Path&& other) noexcept
    : storage_(std::move(other.storage_))
    , elements_(std::move(other.elements_))
    , offsets_(std::move(other.offsets_))
{
    // After move, we need to rebuild views because the storage_ pointer changed
    rebuild_views();
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
        offsets_ = std::move(other.offsets_);
        rebuild_views();
    }
    return *this;
}

// ============================================================
// Path - Constructors from initializer_list and PathView
// ============================================================

Path::Path(std::initializer_list<PathElement> init) {
    reserve(init.size());
    for (const auto& elem : init) {
        push_back(elem);
    }
}

Path::Path(PathView view) {
    reserve(view.size());
    for (const auto& elem : view) {
        push_back(elem);
    }
}

// ============================================================
// Path - Modifiers
// ============================================================

std::string_view Path::store_key(std::string_view key) {
    auto offset = storage_.size();
    offsets_.push_back(offset);
    storage_.append(key);
    return std::string_view{storage_.data() + offset, key.size()};
}

Path& Path::push_back(std::string_view key) {
    auto stored = store_key(key);
    elements_.emplace_back(stored);
    return *this;
}

Path& Path::push_back(std::size_t index) {
    elements_.emplace_back(index);
    return *this;
}

Path& Path::push_back(const PathElement& elem) {
    if (auto* sv = std::get_if<std::string_view>(&elem)) {
        push_back(*sv);
    } else {
        push_back(std::get<std::size_t>(elem));
    }
    return *this;
}

void Path::pop_back() {
    if (elements_.empty()) return;
    
    const auto& last = elements_.back();
    if (std::holds_alternative<std::string_view>(last)) {
        // Remove the last offset
        if (!offsets_.empty()) {
            auto last_offset = offsets_.back();
            offsets_.pop_back();
            // Shrink storage to remove the last key
            storage_.resize(last_offset);
        }
    }
    elements_.pop_back();
}

void Path::clear() noexcept {
    storage_.clear();
    elements_.clear();
    offsets_.clear();
}

void Path::reserve(std::size_t n) {
    elements_.reserve(n);
    offsets_.reserve(n);
    // Estimate average key length of 8 characters
    storage_.reserve(n * 8);
}

// ============================================================
// Path - Internal helpers
// ============================================================

void Path::rebuild_views() {
    // Rebuild all string_view elements to point to the current storage_
    std::size_t offset_idx = 0;
    for (auto& elem : elements_) {
        if (auto* sv = std::get_if<std::string_view>(&elem)) {
            auto len = sv->size();
            auto offset = offsets_[offset_idx];
            elem = std::string_view{storage_.data() + offset, len};
            ++offset_idx;
        }
    }
}

// ============================================================
// Path - Comparison operators
// ============================================================

bool Path::operator==(const Path& other) const noexcept {
    return PathView(*this) == PathView(other);
}

bool Path::operator==(PathView other) const noexcept {
    PathView self = *this;
    return self == other;
}

// ============================================================
// Utility functions
// ============================================================

std::string path_to_string(PathView path) {
    if (path.empty()) {
        return "(root)";
    }
    
    std::string result;
    result.reserve(path.size() * 10);  // Estimate
    
    for (const auto& elem : path) {
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
