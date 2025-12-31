// Copyright (c) 2024 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file undo.h
/// @brief Unified Undo/Redo Interface
///
/// This file provides a unified abstract interface for undo/redo functionality.
/// Two concrete implementations are available:
///
/// 1. **SnapshotUndo** (from editor_engine.h):
///    - Stores complete state snapshots
///    - Pro: Simple, works with any state
///    - Con: Memory intensive for large states
///
/// 2. **DeltaUndo** (from delta_undo.h):
///    - Stores reversible operations (deltas)
///    - Pro: Memory efficient, preserves system state through undo
///    - Con: More complex, requires explicit delta creation
///
/// Usage:
/// @code
/// // Use abstract interface
/// IUndoController& undo = get_undo_controller();
/// undo.set_property("obj1", "name", Value{"NewName"});
/// 
/// if (undo.can_undo()) {
///     undo.undo();
/// }
/// @endcode

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/scene_types.h>
#include <lager_ext/value.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace lager_ext {

/// @brief Abstract interface for undo/redo controllers
/// 
/// This interface provides a unified API for both snapshot-based and delta-based
/// undo implementations. Clients can program against this interface without
/// knowing the underlying implementation.
class LAGER_EXT_API IUndoController {
public:
    virtual ~IUndoController() = default;

    // ============================================================
    // Initialization
    // ============================================================

    /// Initialize with scene state
    virtual void initialize(const SceneState& initial_state) = 0;

    // ============================================================
    // State Access
    // ============================================================

    /// Get the current scene state
    [[nodiscard]] virtual const SceneState& get_scene() const = 0;

    /// Get object by ID (returns nullptr if not found)
    [[nodiscard]] virtual const SceneObject* get_object(const std::string& id) const = 0;

    /// Get the currently selected object (returns nullptr if none selected)
    [[nodiscard]] virtual const SceneObject* get_selected_object() const = 0;

    /// Get property value from an object
    [[nodiscard]] virtual Value get_property(const std::string& object_id, const std::string& path) const = 0;

    // ============================================================
    // User Operations (create undo entries)
    // ============================================================

    /// Set a single property (creates undo entry)
    virtual void set_property(const std::string& object_id, const std::string& path, Value value) = 0;

    /// Set multiple properties atomically (creates single undo entry)
    virtual void set_properties(const std::string& object_id, const std::map<std::string, Value>& updates) = 0;

    // ============================================================
    // System Operations (no undo entries)
    // ============================================================

    /// Select an object (no undo entry)
    virtual void select_object(const std::string& object_id) = 0;

    // ============================================================
    // Transaction Support
    // ============================================================

    /// Begin a transaction - groups subsequent operations into a single undo step
    virtual void begin_transaction(const std::string& description) = 0;

    /// End the current transaction
    virtual void end_transaction() = 0;

    // ============================================================
    // Undo/Redo Operations
    // ============================================================

    /// Check if undo is available
    [[nodiscard]] virtual bool can_undo() const = 0;

    /// Check if redo is available
    [[nodiscard]] virtual bool can_redo() const = 0;

    /// Get description of the operation that would be undone
    [[nodiscard]] virtual std::string get_undo_description() const = 0;

    /// Get description of the operation that would be redone
    [[nodiscard]] virtual std::string get_redo_description() const = 0;

    /// Perform undo
    virtual void undo() = 0;

    /// Perform redo
    virtual void redo() = 0;

    // ============================================================
    // History Management
    // ============================================================

    /// Get the number of undo steps available
    [[nodiscard]] virtual std::size_t undo_count() const = 0;

    /// Get the number of redo steps available
    [[nodiscard]] virtual std::size_t redo_count() const = 0;

    /// Clear all undo/redo history
    virtual void clear_history() = 0;

    // ============================================================
    // Event Loop Integration
    // ============================================================

    /// Process pending events (for manual event loop)
    virtual void step() = 0;

    // ============================================================
    // Change Notification
    // ============================================================

    /// Callback type for state change notifications
    using WatchCallback = std::function<void()>;

    /// Watch for changes (returns unsubscribe function)
    [[nodiscard]] virtual std::function<void()> watch(WatchCallback callback) = 0;
};

/// @brief RAII helper for transactions
/// 
/// Automatically calls begin_transaction on construction and
/// end_transaction on destruction (or commit).
/// 
/// @example
/// @code
/// {
///     UndoTransaction tx(controller, "Move objects");
///     controller.set_property("obj1", "x", Value{10});
///     controller.set_property("obj1", "y", Value{20});
///     // Transaction committed on scope exit
/// }
/// @endcode
class LAGER_EXT_API UndoTransaction {
public:
    /// Begin a transaction
    explicit UndoTransaction(IUndoController& controller, const std::string& description)
        : controller_(controller), committed_(false)
    {
        controller_.begin_transaction(description);
    }

    /// End transaction on destruction if not already committed
    ~UndoTransaction() {
        if (!committed_) {
            controller_.end_transaction();
        }
    }

    /// Explicitly commit the transaction
    void commit() {
        if (!committed_) {
            controller_.end_transaction();
            committed_ = true;
        }
    }

    /// Cancel the transaction (undo all operations in this transaction)
    void rollback() {
        if (!committed_) {
            controller_.end_transaction();
            controller_.undo();
            committed_ = true;
        }
    }

    // Non-copyable, non-movable
    UndoTransaction(const UndoTransaction&) = delete;
    UndoTransaction& operator=(const UndoTransaction&) = delete;
    UndoTransaction(UndoTransaction&&) = delete;
    UndoTransaction& operator=(UndoTransaction&&) = delete;

private:
    IUndoController& controller_;
    bool committed_;
};

/// @brief Undo implementation type
enum class UndoType {
    Snapshot,  ///< Snapshot-based (stores complete states)
    Delta      ///< Delta-based (stores reversible operations)
};

/// @brief Factory function to create an undo controller
/// 
/// @param type The implementation type to use
/// @return Unique pointer to the undo controller
[[nodiscard]] LAGER_EXT_API std::unique_ptr<IUndoController> create_undo_controller(UndoType type = UndoType::Delta);

} // namespace lager_ext
