// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file builders.h
/// @brief Builder classes for efficient O(n) construction of immutable Value containers.
///
/// This file provides transient-based builders for constructing Value containers:
/// - MapBuilder: Build value_map efficiently
/// - VectorBuilder: Build value_vector efficiently  
/// - ArrayBuilder: Build value_array efficiently
/// - TableBuilder: Build value_table efficiently
///
/// Usage:
/// @code
///   #include <lager_ext/builders.h>
///
///   // Build a map with multiple entries in O(n) time
///   Value config = MapBuilder()
///       .set("width", 1920)
///       .set("height", 1080)
///       .set("fullscreen", true)
///       .finish();
///
///   // Build a vector efficiently
///   Value items = VectorBuilder()
///       .push_back("item1")
///       .push_back("item2")
///       .push_back("item3")
///       .finish();
/// @endcode
///
/// Note: This header must be included separately from value.h if you need Builder functionality.

#pragma once

#include "value.h"

namespace lager_ext {

// ============================================================
// Builder classes for O(n) construction using immer's transient API
// ============================================================

/// Builder for constructing value_map efficiently - O(n) complexity
template <typename MemoryPolicy>
class BasicMapBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_map = BasicValueMap<MemoryPolicy>;
    using value_vector = BasicValueVector<MemoryPolicy>;
    using transient_type = typename value_map::transient_type;

    BasicMapBuilder() : transient_(value_map{}.transient()) {}
    explicit BasicMapBuilder(const value_map& existing) : transient_(existing.transient()) {}

    ///   auto result = MapBuilder(config)
    ///       .set("updated", true)
    ///       .finish();
    explicit BasicMapBuilder(const value_type& existing) 
        : transient_(existing.template is<value_map>() 
            ? existing.template get_if<value_map>()->transient()
            : value_map{}.transient()) {}

    // Move operations (allowed)
    BasicMapBuilder(BasicMapBuilder&&) noexcept = default;
    BasicMapBuilder& operator=(BasicMapBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    BasicMapBuilder(const BasicMapBuilder&) = delete;
    BasicMapBuilder& operator=(const BasicMapBuilder&) = delete;

    /// Set a key-value pair
    /// @param key The key
    /// @param val The value (any type convertible to BasicValue)
    /// @return Reference to this builder for chaining
    template <typename T>
    BasicMapBuilder& set(const std::string& key, T&& val) {
        transient_.set(key, value_box{value_type{std::forward<T>(val)}});
        return *this;
    }

    /// Set a key with an already constructed BasicValue
    BasicMapBuilder& set(const std::string& key, value_type val) {
        transient_.set(key, value_box{std::move(val)});
        return *this;
    }

    /// Check if the builder contains a key
    [[nodiscard]] bool contains(const std::string& key) const {
        return transient_.count(key) > 0;
    }

    /// Get current size
    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    // ============================================================
    // Access and Update Methods (for modifying previously set values)
    // ============================================================

    /// Get a previously set value by key
    /// @param key The key to look up
    /// @param default_val Value to return if key doesn't exist (default: null)
    /// @return The value, or default_val if not found
    [[nodiscard]] value_type get(const std::string& key, value_type default_val = value_type{}) const {
        if (auto* found = transient_.find(key)) {
            return found->get();
        }
        return default_val;
    }

    /// Update a previously set value by key using a function
    /// @param key The key to update
    /// @param fn Function taking value_type and returning value_type
    /// @return Reference to this builder for chaining
    /// @note If key doesn't exist, no change is made
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicMapBuilder& update_at(const std::string& key, Fn&& fn) {
        if (auto* found = transient_.find(key)) {
            auto new_val = std::forward<Fn>(fn)(found->get());
            transient_.set(key, value_box{std::move(new_val)});
        }
        return *this;
    }

    /// Update or insert: fn receives current value (null if key doesn't exist)
    /// @param key The key to upsert
    /// @param fn Function taking value_type (may be null) and returning value_type
    /// @return Reference to this builder for chaining
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicMapBuilder& upsert(const std::string& key, Fn&& fn) {
        value_type current{};
        if (auto* found = transient_.find(key)) {
            current = found->get();
        }
        auto new_val = std::forward<Fn>(fn)(std::move(current));
        transient_.set(key, value_box{std::move(new_val)});
        return *this;
    }

    /// Set value at a nested path with auto-vivification
    /// Creates intermediate maps/vectors as needed
    /// @param path The path (e.g., {"users", 0, "name"})
    /// @param val The value to set
    /// @return Reference to this builder for chaining
    template <typename T>
    BasicMapBuilder& set_in(const Path& path, T&& val) {
        if (path.empty()) return *this;
        
        // Get first key (must be string for map)
        auto* first_key = std::get_if<std::string>(&path[0]);
        if (!first_key) return *this;
        
        if (path.size() == 1) {
            // Single element path, just set directly
            return set(*first_key, std::forward<T>(val));
        }
        
        // Get or create the root value for this key
        value_type root_val = get(*first_key);
        
        // Build sub-path (skip first element)
        Path sub_path(path.begin() + 1, path.end());
        
        // Use recursive vivify to set the nested value
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, value_type{std::forward<T>(val)});
        transient_.set(*first_key, value_box{std::move(new_root)});
        return *this;
    }

    /// Update value at a nested path using a function
    /// @param path The path to the value
    /// @param fn Function taking value_type and returning value_type
    /// @return Reference to this builder for chaining
    template<typename Fn>
    BasicMapBuilder& update_in(const Path& path, Fn&& fn) {
        if (path.empty()) return *this;
        
        auto* first_key = std::get_if<std::string>(&path[0]);
        if (!first_key) return *this;
        
        if (path.size() == 1) {
            return update_at(*first_key, std::forward<Fn>(fn));
        }
        
        value_type root_val = get(*first_key);
        Path sub_path(path.begin() + 1, path.end());
        
        // Get current value at path
        value_type current = get_at_path_impl(root_val, sub_path, 0);
        // Apply function
        value_type new_val = std::forward<Fn>(fn)(std::move(current));
        // Set back
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, std::move(new_val));
        transient_.set(*first_key, value_box{std::move(new_root)});
        return *this;
    }

    /// Finish building and return the immutable Value
    /// Note: After calling finish(), the builder is in an undefined state
    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    /// Finish and return just the map (not wrapped in Value)
    [[nodiscard]] value_map finish_map() {
        return transient_.persistent();
    }

private:
    transient_type transient_;

    // Helper: get value at path
    static value_type get_at_path_impl(const value_type& root, const Path& path, std::size_t idx) {
        if (idx >= path.size()) return root;
        
        value_type child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, path[idx]);
        
        if (child.is_null()) return child;
        return get_at_path_impl(child, path, idx + 1);
    }

    // Helper: set value at path with vivification
    static value_type set_at_path_vivify_impl(
        const value_type& root,
        const Path& path,
        std::size_t idx,
        value_type new_val)
    {
        if (idx >= path.size()) return new_val;
        
        const auto& elem = path[idx];
        value_type current_child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, elem);
        
        // Prepare child for next level if needed
        if (current_child.is_null() && idx + 1 < path.size()) {
            const auto& next = path[idx + 1];
            if (std::holds_alternative<std::string>(next)) {
                current_child = value_type{value_map{}};
            } else {
                current_child = value_type{value_vector{}};
            }
        }
        
        value_type new_child = set_at_path_vivify_impl(current_child, path, idx + 1, std::move(new_val));
        
        // Set child back to parent
        return std::visit([&root, &new_child](const auto& key_or_idx) -> value_type {
            using T = std::decay_t<decltype(key_or_idx)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return root.set_vivify(key_or_idx, std::move(new_child));
            } else {
                return root.set_vivify(key_or_idx, std::move(new_child));
            }
        }, elem);
    }
};

/// Builder for constructing value_vector efficiently - O(n) complexity
template <typename MemoryPolicy>
class BasicVectorBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_map = BasicValueMap<MemoryPolicy>;
    using value_vector = BasicValueVector<MemoryPolicy>;
    using transient_type = typename value_vector::transient_type;

    /// Create an empty vector builder
    BasicVectorBuilder() : transient_(value_vector{}.transient()) {}

    /// Create a builder from an existing vector (for incremental modification)
    explicit BasicVectorBuilder(const value_vector& existing) 
        : transient_(existing.transient()) {}

    /// Create a builder from a Value containing a vector
    explicit BasicVectorBuilder(const value_type& existing) 
        : transient_(existing.template is<value_vector>() 
            ? existing.template get_if<value_vector>()->transient()
            : value_vector{}.transient()) {}

    // Move operations (allowed)
    BasicVectorBuilder(BasicVectorBuilder&&) noexcept = default;
    BasicVectorBuilder& operator=(BasicVectorBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    BasicVectorBuilder(const BasicVectorBuilder&) = delete;
    BasicVectorBuilder& operator=(const BasicVectorBuilder&) = delete;

    /// Append a value to the end
    template <typename T>
    BasicVectorBuilder& push_back(T&& val) {
        transient_.push_back(value_box{value_type{std::forward<T>(val)}});
        return *this;
    }

    /// Append an already constructed BasicValue
    BasicVectorBuilder& push_back(value_type val) {
        transient_.push_back(value_box{std::move(val)});
        return *this;
    }

    /// Set value at index (must be within current size)
    template <typename T>
    BasicVectorBuilder& set(std::size_t index, T&& val) {
        if (index < transient_.size()) {
            transient_.set(index, value_box{value_type{std::forward<T>(val)}});
        }
        return *this;
    }

    /// Get current size
    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    /// Get a previously set value by index
    [[nodiscard]] value_type get(std::size_t index, value_type default_val = value_type{}) const {
        if (index < transient_.size()) {
            return transient_[index].get();
        }
        return default_val;
    }

    /// Update a previously set value by index using a function
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicVectorBuilder& update_at(std::size_t index, Fn&& fn) {
        if (index < transient_.size()) {
            auto new_val = std::forward<Fn>(fn)(transient_[index].get());
            transient_.set(index, value_box{std::move(new_val)});
        }
        return *this;
    }

    /// Set value at a nested path with auto-vivification
    template <typename T>
    BasicVectorBuilder& set_in(const Path& path, T&& val) {
        if (path.empty()) return *this;
        
        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size()) return *this;
        
        if (path.size() == 1) {
            return set(*first_idx, std::forward<T>(val));
        }
        
        value_type root_val = transient_[*first_idx].get();
        Path sub_path(path.begin() + 1, path.end());
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, value_type{std::forward<T>(val)});
        transient_.set(*first_idx, value_box{std::move(new_root)});
        return *this;
    }

    /// Update value at a nested path using a function
    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicVectorBuilder& update_in(const Path& path, Fn&& fn) {
        if (path.empty()) return *this;
        
        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size()) return *this;
        
        if (path.size() == 1) {
            return update_at(*first_idx, std::forward<Fn>(fn));
        }
        
        value_type root_val = transient_[*first_idx].get();
        Path sub_path(path.begin() + 1, path.end());
        value_type current = get_at_path_impl(root_val, sub_path, 0);
        value_type new_val = std::forward<Fn>(fn)(std::move(current));
        value_type new_root = set_at_path_vivify_impl(root_val, sub_path, 0, std::move(new_val));
        transient_.set(*first_idx, value_box{std::move(new_root)});
        return *this;
    }

    /// Finish building and return the immutable Value
    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    /// Finish and return just the vector (not wrapped in Value)
    [[nodiscard]] value_vector finish_vector() {
        return transient_.persistent();
    }

private:
    transient_type transient_;

    static value_type get_at_path_impl(const value_type& root, const Path& path, std::size_t idx) {
        if (idx >= path.size()) return root;
        value_type child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, path[idx]);
        if (child.is_null()) return child;
        return get_at_path_impl(child, path, idx + 1);
    }

    static value_type set_at_path_vivify_impl(
        const value_type& root,
        const Path& path,
        std::size_t idx,
        value_type new_val)
    {
        if (idx >= path.size()) return new_val;
        const auto& elem = path[idx];
        value_type current_child = std::visit([&root](const auto& key_or_idx) {
            return root.at(key_or_idx);
        }, elem);
        if (current_child.is_null() && idx + 1 < path.size()) {
            const auto& next = path[idx + 1];
            if (std::holds_alternative<std::string>(next)) {
                current_child = value_type{value_map{}};
            } else {
                current_child = value_type{value_vector{}};
            }
        }
        value_type new_child = set_at_path_vivify_impl(current_child, path, idx + 1, std::move(new_val));
        return std::visit([&root, &new_child](const auto& key_or_idx) -> value_type {
            return root.set_vivify(key_or_idx, std::move(new_child));
        }, elem);
    }
};

/// Builder for constructing value_array efficiently - O(n) complexity
template <typename MemoryPolicy>
class BasicArrayBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_array = BasicValueArray<MemoryPolicy>;
    using transient_type = typename value_array::transient_type;

    BasicArrayBuilder() : transient_(value_array{}.transient()) {}
    explicit BasicArrayBuilder(const value_array& existing) 
        : transient_(existing.transient()) {}
    explicit BasicArrayBuilder(const value_type& existing) 
        : transient_(existing.template is<value_array>() 
            ? existing.template get_if<value_array>()->transient()
            : value_array{}.transient()) {}

    BasicArrayBuilder(BasicArrayBuilder&&) noexcept = default;
    BasicArrayBuilder& operator=(BasicArrayBuilder&&) noexcept = default;
    BasicArrayBuilder(const BasicArrayBuilder&) = delete;
    BasicArrayBuilder& operator=(const BasicArrayBuilder&) = delete;

    template <typename T>
    BasicArrayBuilder& push_back(T&& val) {
        transient_.push_back(value_box{value_type{std::forward<T>(val)}});
        return *this;
    }

    BasicArrayBuilder& push_back(value_type val) {
        transient_.push_back(value_box{std::move(val)});
        return *this;
    }

    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    [[nodiscard]] value_array finish_array() {
        return transient_.persistent();
    }

private:
    transient_type transient_;
};

/// Builder for constructing value_table efficiently - O(n) complexity
template <typename MemoryPolicy>
class BasicTableBuilder {
public:
    using value_type = BasicValue<MemoryPolicy>;
    using value_box = BasicValueBox<MemoryPolicy>;
    using value_table = BasicValueTable<MemoryPolicy>;
    using table_entry = BasicTableEntry<MemoryPolicy>;
    using transient_type = typename value_table::transient_type;

    BasicTableBuilder() : transient_(value_table{}.transient()) {}
    explicit BasicTableBuilder(const value_table& existing) 
        : transient_(existing.transient()) {}
    explicit BasicTableBuilder(const value_type& existing) 
        : transient_(existing.template is<value_table>() 
            ? existing.template get_if<value_table>()->transient()
            : value_table{}.transient()) {}

    BasicTableBuilder(BasicTableBuilder&&) noexcept = default;
    BasicTableBuilder& operator=(BasicTableBuilder&&) noexcept = default;
    BasicTableBuilder(const BasicTableBuilder&) = delete;
    BasicTableBuilder& operator=(const BasicTableBuilder&) = delete;

    template <typename T>
    BasicTableBuilder& insert(const std::string& id, T&& val) {
        transient_.insert(table_entry{id, value_box{value_type{std::forward<T>(val)}}});
        return *this;
    }

    BasicTableBuilder& insert(const std::string& id, value_type val) {
        transient_.insert(table_entry{id, value_box{std::move(val)}});
        return *this;
    }

    [[nodiscard]] bool contains(const std::string& id) const {
        return transient_.count(id) > 0;
    }

    [[nodiscard]] value_type get(const std::string& id, value_type default_val = value_type{}) const {
        const auto* ptr = transient_.find(id);
        if (ptr) {
            return ptr->value.get();
        }
        return default_val;
    }

    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicTableBuilder& update(const std::string& id, Fn&& fn) {
        const auto* ptr = transient_.find(id);
        if (ptr) {
            auto new_val = value_type{std::forward<Fn>(fn)(ptr->value.get())};
            transient_.insert(table_entry{id, value_box{std::move(new_val)}});
        }
        return *this;
    }

    template<typename Fn>
    requires ValueTransformer<Fn, value_type>
    BasicTableBuilder& upsert(const std::string& id, Fn&& fn) {
        value_type current{};
        const auto* ptr = transient_.find(id);
        if (ptr) {
            current = ptr->value.get();
        }
        auto new_val = std::forward<Fn>(fn)(std::move(current));
        transient_.insert(table_entry{id, value_box{std::move(new_val)}});
        return *this;
    }

    [[nodiscard]] std::size_t size() const {
        return transient_.size();
    }

    [[nodiscard]] value_type finish() {
        return value_type{transient_.persistent()};
    }

    [[nodiscard]] value_table finish_table() {
        return transient_.persistent();
    }

private:
    transient_type transient_;
};

// ============================================================
// Builder type aliases
// ============================================================

// Unsafe (single-threaded) builders - use with Value
using MapBuilder    = BasicMapBuilder<unsafe_memory_policy>;
using VectorBuilder = BasicVectorBuilder<unsafe_memory_policy>;
using ArrayBuilder  = BasicArrayBuilder<unsafe_memory_policy>;
using TableBuilder  = BasicTableBuilder<unsafe_memory_policy>;

// Thread-safe builders - use with SyncValue
using SyncMapBuilder    = BasicMapBuilder<thread_safe_memory_policy>;
using SyncVectorBuilder = BasicVectorBuilder<thread_safe_memory_policy>;
using SyncArrayBuilder  = BasicArrayBuilder<thread_safe_memory_policy>;
using SyncTableBuilder  = BasicTableBuilder<thread_safe_memory_policy>;

// ============================================================
// Extern Template Declarations for Builders
// ============================================================

extern template class BasicMapBuilder<unsafe_memory_policy>;
extern template class BasicVectorBuilder<unsafe_memory_policy>;
extern template class BasicArrayBuilder<unsafe_memory_policy>;
extern template class BasicTableBuilder<unsafe_memory_policy>;

extern template class BasicMapBuilder<thread_safe_memory_policy>;
extern template class BasicVectorBuilder<thread_safe_memory_policy>;
extern template class BasicArrayBuilder<thread_safe_memory_policy>;
extern template class BasicTableBuilder<thread_safe_memory_policy>;

} // namespace lager_ext
