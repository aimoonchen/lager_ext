// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file builders.h
/// @brief Builder classes for efficient O(n) construction of immutable ImmerValue containers.
///
/// This file provides transient-based builders for constructing ImmerValue containers:
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
///   ImmerValue config = MapBuilder()
///       .set("width", 1920)
///       .set("height", 1080)
///       .set("fullscreen", true)
///       .finish();
///
///   // Build a vector efficiently
///   ImmerValue items = VectorBuilder()
///       .push_back("item1")
///       .push_back("item2")
///       .push_back("item3")
///       .finish();
/// @endcode
///
/// Note: This header must be included separately from value.h if you need Builder functionality.

#pragma once

#include "value.h"     // Include full definitions for implementation
#include "value_fwd.h" // Use forward declarations for better compilation speed

namespace lager_ext {

// ============================================================
// Builder classes for O(n) construction using immer's transient API
// ============================================================

/// Builder for constructing ValueMap efficiently - O(n) complexity
class MapBuilder {
public:
    using transient_type = typename ValueMap::transient_type;

    MapBuilder() : transient_(ValueMap{}.transient()) {}
    explicit MapBuilder(const ValueMap& existing) : transient_(existing.transient()) {}

    ///   auto result = MapBuilder(config)
    ///       .set("updated", true)
    ///       .finish();
    explicit MapBuilder(const ImmerValue& existing)
        : transient_(existing.is<BoxedValueMap>() ? existing.get_if<BoxedValueMap>()->get().transient()
                                                  : ValueMap{}.transient()) {}

    // Move operations (allowed)
    MapBuilder(MapBuilder&&) noexcept = default;
    MapBuilder& operator=(MapBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    MapBuilder(const MapBuilder&) = delete;
    MapBuilder& operator=(const MapBuilder&) = delete;

    /// Set a key-value pair
    /// @param key The key (string_view to avoid allocation on lookup, but key is copied internally)
    /// @param val The value (any type convertible to ImmerValue)
    /// @return Reference to this builder for chaining
    template <typename T>
    MapBuilder& set(std::string_view key, T&& val) {
        transient_.set(std::string{key}, ImmerValue{std::forward<T>(val)});
        return *this;
    }

    /// Set a key with an already constructed ImmerValue
    MapBuilder& set(std::string_view key, ImmerValue val) {
        transient_.set(std::string{key}, std::move(val));
        return *this;
    }

    /// Set with const char* key (disambiguation for string literals)
    template <typename T>
    MapBuilder& set(const char* key, T&& val) {
        return set(std::string_view{key}, std::forward<T>(val));
    }

    /// Set with const char* key (disambiguation for string literals)
    MapBuilder& set(const char* key, ImmerValue val) { return set(std::string_view{key}, std::move(val)); }

    /// Set with const string& key (disambiguation for string lvalue)
    template <typename T>
    MapBuilder& set(const std::string& key, T&& val) {
        return set(std::string_view{key}, std::forward<T>(val));
    }

    /// Set with const string& key (disambiguation for string lvalue)
    MapBuilder& set(const std::string& key, ImmerValue val) { return set(std::string_view{key}, std::move(val)); }

    /// Set with rvalue string key (zero-copy key transfer)
    template <typename T>
    MapBuilder& set(std::string&& key, T&& val) {
        transient_.set(std::move(key), ImmerValue{std::forward<T>(val)});
        return *this;
    }

    /// Set with rvalue string key (zero-copy key transfer)
    MapBuilder& set(std::string&& key, ImmerValue val) {
        transient_.set(std::move(key), std::move(val));
        return *this;
    }

    /// Check if the builder contains a key
    [[nodiscard]] bool contains(std::string_view key) const { return transient_.count(key) > 0; }

    /// Get current size
    [[nodiscard]] std::size_t size() const { return transient_.size(); }

    // ============================================================
    // Access and Update Methods (for modifying previously set values)
    // ============================================================

    /// Get a previously set value by key
    /// @param key The key to look up
    /// @param default_val ImmerValue to return if key doesn't exist (default: null)
    /// @return The value, or default_val if not found
    [[nodiscard]] ImmerValue get(std::string_view key, ImmerValue default_val = ImmerValue{}) const {
        if (auto* found = transient_.find(key)) {
            return *found;  // Container Boxing: ValueMap stores ImmerValue directly
        }
        return default_val;
    }

    /// Update a previously set value by key using a function
    /// @param key The key to update
    /// @param fn Function taking ImmerValue and returning ImmerValue
    /// @return Reference to this builder for chaining
    /// @note If key doesn't exist, no change is made
    template <typename Fn>
        requires ValueTransformer<Fn, ImmerValue>
    MapBuilder& update_at(std::string_view key, Fn&& fn) {
        if (auto* found = transient_.find(key)) {
            auto new_val = std::forward<Fn>(fn)(*found);  // Container Boxing: no .get()
            transient_.set(std::string{key}, std::move(new_val));
        }
        return *this;
    }

    /// Update or insert: fn receives current value (null if key doesn't exist)
    /// @param key The key to upsert
    /// @param fn Function taking ImmerValue (may be null) and returning ImmerValue
    /// @return Reference to this builder for chaining
    template <typename Fn>
        requires ValueTransformer<Fn, ImmerValue>
    MapBuilder& upsert(std::string_view key, Fn&& fn) {
        ImmerValue current{};
        if (auto* found = transient_.find(key)) {
            current = *found;  // Container Boxing: no .get()
        }
        auto new_val = std::forward<Fn>(fn)(std::move(current));
        transient_.set(std::string{key}, std::move(new_val));
        return *this;
    }

    /// Set value at a nested path (strict mode)
    /// @param path The path (e.g., {"users", 0, "name"})
    /// @param val The value to set
    /// @return Reference to this builder for chaining
    /// @note If path doesn't exist, operation silently fails. Use set_at_path_vivify() to auto-create.
    template <typename T>
    MapBuilder& set_at_path(PathView path, T&& val) {
        if (path.empty())
            return *this;

        auto* first_key = std::get_if<std::string_view>(&path[0]);
        if (!first_key)
            return *this;

        if (path.size() == 1) {
            return set(*first_key, std::forward<T>(val));
        }

        ImmerValue root_val = get(*first_key);
        if (root_val.is_null())
            return *this; // Strict mode: path must exist

        // Use subpath(1) - zero-copy view slice
        ImmerValue new_root = set_at_path_strict_impl(root_val, path.subpath(1), ImmerValue{std::forward<T>(val)});
        if (!new_root.is_null()) {
            transient_.set(std::string{*first_key}, std::move(new_root));
        }
        return *this;
    }

    /// Set value at a nested path with auto-vivification
    /// Creates intermediate maps/vectors as needed
    /// @param path The path (e.g., {"users", 0, "name"})
    /// @param val The value to set
    /// @return Reference to this builder for chaining
    template <typename T>
    MapBuilder& set_at_path_vivify(PathView path, T&& val) {
        if (path.empty())
            return *this;

        // Get first key (must be string_view for map)
        auto* first_key = std::get_if<std::string_view>(&path[0]);
        if (!first_key)
            return *this;

        if (path.size() == 1) {
            // Single element path, just set directly
            return set(*first_key, std::forward<T>(val));
        }

        // Get or create the root value for this key
        ImmerValue root_val = get(*first_key);

        // Use subpath(1) - zero-copy view slice (no memory allocation)
        ImmerValue new_root = set_at_path_vivify_impl(root_val, path.subpath(1), ImmerValue{std::forward<T>(val)});
        transient_.set(std::string{*first_key}, std::move(new_root));
        return *this;
    }

    /// Update value at a nested path using a function (strict mode)
    /// @param path The path to the value
    /// @param fn Function taking ImmerValue and returning ImmerValue
    /// @return Reference to this builder for chaining
    /// @note If path doesn't exist, operation silently fails. Use update_at_path_vivify() to auto-create.
    template <typename Fn>
    MapBuilder& update_at_path(PathView path, Fn&& fn) {
        if (path.empty())
            return *this;

        auto* first_key = std::get_if<std::string_view>(&path[0]);
        if (!first_key)
            return *this;

        if (path.size() == 1) {
            return update_at(*first_key, std::forward<Fn>(fn));
        }

        ImmerValue root_val = get(*first_key);
        if (root_val.is_null())
            return *this; // Strict mode: path must exist

        PathView sub_path = path.subpath(1);

        // Get current value at path
        ImmerValue current = get_at_path_impl(root_val, sub_path);
        if (current.is_null())
            return *this; // Strict mode: path must exist

        // Apply function
        ImmerValue new_val = std::forward<Fn>(fn)(std::move(current));
        // Set back using strict impl
        ImmerValue new_root = set_at_path_strict_impl(root_val, sub_path, std::move(new_val));
        if (!new_root.is_null()) {
            transient_.set(std::string{*first_key}, std::move(new_root));
        }
        return *this;
    }

    /// Update value at a nested path using a function with auto-vivification
    /// @param path The path to the value
    /// @param fn Function taking ImmerValue and returning ImmerValue
    /// @return Reference to this builder for chaining
    template <typename Fn>
    MapBuilder& update_at_path_vivify(PathView path, Fn&& fn) {
        if (path.empty())
            return *this;

        auto* first_key = std::get_if<std::string_view>(&path[0]);
        if (!first_key)
            return *this;

        if (path.size() == 1) {
            return update_at(*first_key, std::forward<Fn>(fn));
        }

        ImmerValue root_val = get(*first_key);
        PathView sub_path = path.subpath(1);

        // Get current value at path (may be null)
        ImmerValue current = get_at_path_impl(root_val, sub_path);
        // Apply function
        ImmerValue new_val = std::forward<Fn>(fn)(std::move(current));
        // Set back with vivification
        ImmerValue new_root = set_at_path_vivify_impl(root_val, sub_path, std::move(new_val));
        transient_.set(std::string{*first_key}, std::move(new_root));
        return *this;
    }

    /// Finish building and return the immutable ImmerValue
    /// Note: After calling finish(), the builder is in an undefined state
    [[nodiscard]] ImmerValue finish() { return ImmerValue{BoxedValueMap{transient_.persistent()}}; }

    /// Finish and return just the map (not wrapped in ImmerValue)
    [[nodiscard]] ValueMap finish_map() { return transient_.persistent(); }

private:
    transient_type transient_;

    // Helper: get value at path using PathView (zero-copy)
    static ImmerValue get_at_path_impl(const ImmerValue& root, PathView path) {
        if (path.empty())
            return root;

        ImmerValue child = std::visit([&root](const auto& key_or_idx) { return root.at(key_or_idx); }, path[0]);

        if (child.is_null())
            return child;
        return get_at_path_impl(child, path.subpath(1));
    }

    // Helper: set value at path (strict mode - returns null on failure)
    // Uses PathView for zero-copy path traversal
    static ImmerValue set_at_path_strict_impl(const ImmerValue& root, PathView path, ImmerValue new_val) {
        if (path.empty())
            return new_val;

        const auto& elem = path[0];
        ImmerValue current_child = std::visit([&root](const auto& key_or_idx) { return root.at(key_or_idx); }, elem);

        // Strict mode: if path doesn't exist, return null to signal failure
        if (current_child.is_null() && path.size() > 1) {
            return ImmerValue{}; // Signal failure
        }

        ImmerValue new_child = set_at_path_strict_impl(current_child, path.subpath(1), std::move(new_val));
        if (new_child.is_null() && path.size() > 1) {
            return ImmerValue{}; // Propagate failure
        }

        // Set child back to parent using ImmerValue's set method (handles Container Boxing internally)
        return std::visit(
            [&root, &new_child](auto key_or_idx) -> ImmerValue {
                return root.set(key_or_idx, std::move(new_child));
            },
            elem);
    }

    // Helper: set value at path with vivification
    // Uses PathView for zero-copy path traversal
    static ImmerValue set_at_path_vivify_impl(const ImmerValue& root, PathView path, ImmerValue new_val) {
        if (path.empty())
            return new_val;

        const auto& elem = path[0];
        ImmerValue current_child = std::visit([&root](const auto& key_or_idx) { return root.at(key_or_idx); }, elem);

        // Prepare child for next level if needed
        if (current_child.is_null() && path.size() > 1) {
            const auto& next = path[1];
            if (std::holds_alternative<std::string_view>(next)) {
                current_child = ImmerValue{BoxedValueMap{ValueMap{}}};
            } else {
                current_child = ImmerValue{BoxedValueVector{ValueVector{}}};
            }
        }

        ImmerValue new_child = set_at_path_vivify_impl(current_child, path.subpath(1), std::move(new_val));

        // Set child back to parent
        return std::visit(
            [&root, &new_child](auto key_or_idx) -> ImmerValue {
                return root.set_vivify(key_or_idx, std::move(new_child));
            },
            elem);
    }
};

/// Builder for constructing ValueVector efficiently - O(n) complexity
class VectorBuilder {
public:
    using transient_type = typename ValueVector::transient_type;

    /// Create an empty vector builder
    VectorBuilder() : transient_(ValueVector{}.transient()) {}

    /// Create a builder from an existing vector (for incremental modification)
    explicit VectorBuilder(const ValueVector& existing) : transient_(existing.transient()) {}

    /// Create a builder from a ImmerValue containing a vector
    explicit VectorBuilder(const ImmerValue& existing)
        : transient_(existing.is<BoxedValueVector>() ? existing.get_if<BoxedValueVector>()->get().transient()
                                                     : ValueVector{}.transient()) {}

    // Move operations (allowed)
    VectorBuilder(VectorBuilder&&) noexcept = default;
    VectorBuilder& operator=(VectorBuilder&&) noexcept = default;

    // Copy operations (disabled - transient sharing is dangerous)
    VectorBuilder(const VectorBuilder&) = delete;
    VectorBuilder& operator=(const VectorBuilder&) = delete;

    /// Append a value to the end
    template <typename T>
    VectorBuilder& push_back(T&& val) {
        transient_.push_back(ImmerValue{std::forward<T>(val)});
        return *this;
    }

    /// Append an already constructed ImmerValue
    VectorBuilder& push_back(ImmerValue val) {
        transient_.push_back(std::move(val));
        return *this;
    }

    /// Set value at index (must be within current size)
    template <typename T>
    VectorBuilder& set(std::size_t index, T&& val) {
        if (index < transient_.size()) {
            transient_.set(index, ImmerValue{std::forward<T>(val)});
        }
        return *this;
    }

    /// Get current size
    [[nodiscard]] std::size_t size() const { return transient_.size(); }

    /// Get a previously set value by index
    [[nodiscard]] ImmerValue get(std::size_t index, ImmerValue default_val = ImmerValue{}) const {
        if (index < transient_.size()) {
            return transient_[index];  // Container Boxing: no .get()
        }
        return default_val;
    }

    /// Update a previously set value by index using a function
    template <typename Fn>
        requires ValueTransformer<Fn, ImmerValue>
    VectorBuilder& update_at(std::size_t index, Fn&& fn) {
        if (index < transient_.size()) {
            auto new_val = std::forward<Fn>(fn)(transient_[index]);  // Container Boxing: no .get()
            transient_.set(index, std::move(new_val));
        }
        return *this;
    }

    /// Set value at a nested path (strict mode)
    /// @note If path doesn't exist, operation silently fails. Use set_at_path_vivify() to auto-create.
    template <typename T>
    VectorBuilder& set_at_path(PathView path, T&& val) {
        if (path.empty())
            return *this;

        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size())
            return *this;

        if (path.size() == 1) {
            return set(*first_idx, std::forward<T>(val));
        }

        ImmerValue root_val = transient_[*first_idx];  // Container Boxing: no .get()
        if (root_val.is_null())
            return *this; // Strict mode: path must exist

        ImmerValue new_root = set_at_path_strict_impl(root_val, path.subpath(1), ImmerValue{std::forward<T>(val)});
        if (!new_root.is_null()) {
            transient_.set(*first_idx, std::move(new_root));
        }
        return *this;
    }

    /// Set value at a nested path with auto-vivification
    /// Creates intermediate maps/vectors as needed
    template <typename T>
    VectorBuilder& set_at_path_vivify(PathView path, T&& val) {
        if (path.empty())
            return *this;

        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size())
            return *this;

        if (path.size() == 1) {
            return set(*first_idx, std::forward<T>(val));
        }

        ImmerValue root_val = transient_[*first_idx];  // Container Boxing: no .get()
        ImmerValue new_root = set_at_path_vivify_impl(root_val, path.subpath(1), ImmerValue{std::forward<T>(val)});
        transient_.set(*first_idx, std::move(new_root));
        return *this;
    }

    /// Update value at a nested path using a function (strict mode)
    /// @note If path doesn't exist, operation silently fails. Use update_at_path_vivify() to auto-create.
    template <typename Fn>
        requires ValueTransformer<Fn, ImmerValue>
    VectorBuilder& update_at_path(PathView path, Fn&& fn) {
        if (path.empty())
            return *this;

        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size())
            return *this;

        if (path.size() == 1) {
            return update_at(*first_idx, std::forward<Fn>(fn));
        }

        ImmerValue root_val = transient_[*first_idx];  // Container Boxing: no .get()
        if (root_val.is_null())
            return *this; // Strict mode: path must exist

        PathView sub_path = path.subpath(1);
        ImmerValue current = get_at_path_impl(root_val, sub_path);
        if (current.is_null())
            return *this; // Strict mode: path must exist

        ImmerValue new_val = std::forward<Fn>(fn)(std::move(current));
        ImmerValue new_root = set_at_path_strict_impl(root_val, sub_path, std::move(new_val));
        if (!new_root.is_null()) {
            transient_.set(*first_idx, std::move(new_root));
        }
        return *this;
    }

    /// Update value at a nested path using a function with auto-vivification
    template <typename Fn>
        requires ValueTransformer<Fn, ImmerValue>
    VectorBuilder& update_at_path_vivify(PathView path, Fn&& fn) {
        if (path.empty())
            return *this;

        auto* first_idx = std::get_if<std::size_t>(&path[0]);
        if (!first_idx || *first_idx >= transient_.size())
            return *this;

        if (path.size() == 1) {
            return update_at(*first_idx, std::forward<Fn>(fn));
        }

        ImmerValue root_val = transient_[*first_idx];  // Container Boxing: no .get()
        PathView sub_path = path.subpath(1);
        ImmerValue current = get_at_path_impl(root_val, sub_path);
        ImmerValue new_val = std::forward<Fn>(fn)(std::move(current));
        ImmerValue new_root = set_at_path_vivify_impl(root_val, sub_path, std::move(new_val));
        transient_.set(*first_idx, std::move(new_root));
        return *this;
    }

    /// Finish building and return the immutable ImmerValue
    [[nodiscard]] ImmerValue finish() { return ImmerValue{BoxedValueVector{transient_.persistent()}}; }

    /// Finish and return just the vector (not wrapped in ImmerValue)
    [[nodiscard]] ValueVector finish_vector() { return transient_.persistent(); }

private:
    transient_type transient_;

    // Helper: get value at path using PathView (zero-copy)
    static ImmerValue get_at_path_impl(const ImmerValue& root, PathView path) {
        if (path.empty())
            return root;
        ImmerValue child = std::visit([&root](const auto& key_or_idx) { return root.at(key_or_idx); }, path[0]);
        if (child.is_null())
            return child;
        return get_at_path_impl(child, path.subpath(1));
    }

    // Helper: set value at path (strict mode - returns null on failure)
    // Uses PathView for zero-copy path traversal
    static ImmerValue set_at_path_strict_impl(const ImmerValue& root, PathView path, ImmerValue new_val) {
        if (path.empty())
            return new_val;

        const auto& elem = path[0];
        ImmerValue current_child = std::visit([&root](const auto& key_or_idx) { return root.at(key_or_idx); }, elem);

        // Strict mode: if path doesn't exist, return null to signal failure
        if (current_child.is_null() && path.size() > 1) {
            return ImmerValue{}; // Signal failure
        }

        ImmerValue new_child = set_at_path_strict_impl(current_child, path.subpath(1), std::move(new_val));
        if (new_child.is_null() && path.size() > 1) {
            return ImmerValue{}; // Propagate failure
        }

        // Set child back to parent using ImmerValue's set method (handles Container Boxing internally)
        return std::visit(
            [&root, &new_child](auto key_or_idx) -> ImmerValue {
                return root.set(key_or_idx, std::move(new_child));
            },
            elem);
    }

    // Helper: set value at path with vivification
    // Uses PathView for zero-copy path traversal
    static ImmerValue set_at_path_vivify_impl(const ImmerValue& root, PathView path, ImmerValue new_val) {
        if (path.empty())
            return new_val;
        const auto& elem = path[0];
        ImmerValue current_child = std::visit([&root](const auto& key_or_idx) { return root.at(key_or_idx); }, elem);
        if (current_child.is_null() && path.size() > 1) {
            const auto& next = path[1];
            if (std::holds_alternative<std::string_view>(next)) {
                current_child = ImmerValue{BoxedValueMap{ValueMap{}}};
            } else {
                current_child = ImmerValue{BoxedValueVector{ValueVector{}}};
            }
        }
        ImmerValue new_child = set_at_path_vivify_impl(current_child, path.subpath(1), std::move(new_val));
        return std::visit(
            [&root, &new_child](auto key_or_idx) -> ImmerValue {
                return root.set_vivify(key_or_idx, std::move(new_child));
            },
            elem);
    }
};

/// Builder for constructing ValueArray efficiently - O(n) complexity
class ArrayBuilder {
public:
    using transient_type = typename ValueArray::transient_type;

    ArrayBuilder() : transient_(ValueArray{}.transient()) {}
    explicit ArrayBuilder(const ValueArray& existing) : transient_(existing.transient()) {}
    explicit ArrayBuilder(const ImmerValue& existing)
        : transient_(existing.is<BoxedValueArray>() ? existing.get_if<BoxedValueArray>()->get().transient()
                                                    : ValueArray{}.transient()) {}

    ArrayBuilder(ArrayBuilder&&) noexcept = default;
    ArrayBuilder& operator=(ArrayBuilder&&) noexcept = default;
    ArrayBuilder(const ArrayBuilder&) = delete;
    ArrayBuilder& operator=(const ArrayBuilder&) = delete;

    template <typename T>
    ArrayBuilder& push_back(T&& val) {
        transient_.push_back(ImmerValue{std::forward<T>(val)});
        return *this;
    }

    ArrayBuilder& push_back(ImmerValue val) {
        transient_.push_back(std::move(val));
        return *this;
    }

    [[nodiscard]] std::size_t size() const { return transient_.size(); }

    [[nodiscard]] ImmerValue finish() { return ImmerValue{BoxedValueArray{transient_.persistent()}}; }

    [[nodiscard]] ValueArray finish_array() { return transient_.persistent(); }

private:
    transient_type transient_;
};

/// Builder for constructing ValueTable efficiently - O(n) complexity
class TableBuilder {
public:
    using transient_type = typename ValueTable::transient_type;

    TableBuilder() : transient_(ValueTable{}.transient()) {}
    explicit TableBuilder(const ValueTable& existing) : transient_(existing.transient()) {}
    explicit TableBuilder(const ImmerValue& existing)
        : transient_(existing.is<BoxedValueTable>() ? existing.get_if<BoxedValueTable>()->get().transient()
                                                    : ValueTable{}.transient()) {}

    TableBuilder(TableBuilder&&) noexcept = default;
    TableBuilder& operator=(TableBuilder&&) noexcept = default;
    TableBuilder(const TableBuilder&) = delete;
    TableBuilder& operator=(const TableBuilder&) = delete;

    /// Insert a value with string_view id (id is copied internally)
    /// Note: TableEntry::value still uses ValueBox (Element Boxing for tables)
    template <typename T>
    TableBuilder& insert(std::string_view id, T&& val) {
        transient_.insert(TableEntry{std::string{id}, ValueBox{ImmerValue{std::forward<T>(val)}}});
        return *this;
    }

    /// Insert an already constructed ImmerValue
    TableBuilder& insert(std::string_view id, ImmerValue val) {
        transient_.insert(TableEntry{std::string{id}, ValueBox{std::move(val)}});
        return *this;
    }

    /// Insert with const char* id (disambiguation for string literals)
    template <typename T>
    TableBuilder& insert(const char* id, T&& val) {
        return insert(std::string_view{id}, std::forward<T>(val));
    }

    /// Insert with const char* id (disambiguation for string literals)
    TableBuilder& insert(const char* id, ImmerValue val) { return insert(std::string_view{id}, std::move(val)); }

    /// Insert with const string& id (disambiguation for string lvalue)
    template <typename T>
    TableBuilder& insert(const std::string& id, T&& val) {
        return insert(std::string_view{id}, std::forward<T>(val));
    }

    /// Insert with const string& id (disambiguation for string lvalue)
    TableBuilder& insert(const std::string& id, ImmerValue val) {
        return insert(std::string_view{id}, std::move(val));
    }

    /// Insert with rvalue string id (zero-copy id transfer)
    template <typename T>
    TableBuilder& insert(std::string&& id, T&& val) {
        transient_.insert(TableEntry{std::move(id), ValueBox{ImmerValue{std::forward<T>(val)}}});
        return *this;
    }

    /// Insert with rvalue string id (zero-copy id transfer)
    TableBuilder& insert(std::string&& id, ImmerValue val) {
        transient_.insert(TableEntry{std::move(id), ValueBox{std::move(val)}});
        return *this;
    }

    [[nodiscard]] bool contains(std::string_view id) const { return transient_.count(id) > 0; }

    [[nodiscard]] ImmerValue get(std::string_view id, ImmerValue default_val = ImmerValue{}) const {
        const auto* ptr = transient_.find(id);
        if (ptr) {
            return ptr->value.get();  // TableEntry::value is ValueBox, needs .get()
        }
        return default_val;
    }

    template <typename Fn>
        requires ValueTransformer<Fn, ImmerValue>
    TableBuilder& update(std::string_view id, Fn&& fn) {
        const auto* ptr = transient_.find(id);
        if (ptr) {
            auto new_val = ImmerValue{std::forward<Fn>(fn)(ptr->value.get())};  // TableEntry::value is ValueBox
            transient_.insert(TableEntry{std::string{id}, ValueBox{std::move(new_val)}});
        }
        return *this;
    }

    template <typename Fn>
        requires ValueTransformer<Fn, ImmerValue>
    TableBuilder& upsert(std::string_view id, Fn&& fn) {
        ImmerValue current{};
        const auto* ptr = transient_.find(id);
        if (ptr) {
            current = ptr->value.get();  // TableEntry::value is ValueBox
        }
        auto new_val = std::forward<Fn>(fn)(std::move(current));
        transient_.insert(TableEntry{std::string{id}, ValueBox{std::move(new_val)}});
        return *this;
    }

    [[nodiscard]] std::size_t size() const { return transient_.size(); }

    [[nodiscard]] ImmerValue finish() { return ImmerValue{BoxedValueTable{transient_.persistent()}}; }

    [[nodiscard]] ValueTable finish_table() { return transient_.persistent(); }

private:
    transient_type transient_;
};

// ============================================================
// All Builder classes (MapBuilder, VectorBuilder, ArrayBuilder, TableBuilder)
// are now concrete types, not templates.
// ============================================================

} // namespace lager_ext