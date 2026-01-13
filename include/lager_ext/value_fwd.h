// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file value_fwd.h
/// @brief Forward declarations for Value and Builder types
///
/// This header provides forward declarations for Value and Builder types, allowing
/// headers to declare functions using these types without including the full
/// value.h or builders.h headers. This reduces compilation dependencies.
///
/// Usage:
/// @code
///   // In header file:
///   #include <lager_ext/value_fwd.h>
///   Value create_config();                    // ✅ Forward declaration
///   MapBuilder& configure_builder();          // ✅ Forward declaration
///
///   // In implementation file:
///   #include <lager_ext/value.h>
///   #include <lager_ext/builders.h>           // Include full definitions when needed
///   Value create_config() { return Value::map({}); }  // ✅ Full definition available
/// @endcode

#pragma once

#include <immer/memory_policy.hpp>

namespace lager_ext {

// ============================================================
// Configuration (must match value.h)
// ============================================================

#ifndef LAGER_EXT_ENABLE_THREAD_SAFE
#define LAGER_EXT_ENABLE_THREAD_SAFE 0
#endif

// ============================================================
// Memory Policy Forward Declarations
// ============================================================

using unsafe_memory_policy = immer::memory_policy<immer::unsafe_free_list_heap_policy<immer::cpp_heap>,
                                                  immer::unsafe_refcount_policy, immer::no_lock_policy>;

using thread_safe_memory_policy = immer::default_memory_policy;

// ============================================================
// Value Type Forward Declarations
// ============================================================

// Forward declare the basic template
template <typename MemoryPolicy>
struct BasicValue;

// Main type aliases - these can be forward declared since
// we're providing the template parameter explicitly
using Value = BasicValue<unsafe_memory_policy>;

// Conditional thread-safe forward declarations
#if LAGER_EXT_ENABLE_THREAD_SAFE
using SyncValue = BasicValue<thread_safe_memory_policy>;
#endif

// ============================================================
// Builder Type Forward Declarations
// ============================================================

// Forward declare builder templates
template <typename MemoryPolicy>
class BasicMapBuilder;
template <typename MemoryPolicy>
class BasicVectorBuilder;
template <typename MemoryPolicy>
class BasicArrayBuilder;
template <typename MemoryPolicy>
class BasicTableBuilder;

// Unsafe (single-threaded) builder aliases
using MapBuilder = BasicMapBuilder<unsafe_memory_policy>;
using VectorBuilder = BasicVectorBuilder<unsafe_memory_policy>;
using ArrayBuilder = BasicArrayBuilder<unsafe_memory_policy>;
using TableBuilder = BasicTableBuilder<unsafe_memory_policy>;

// Thread-safe builder aliases - only available when enabled
#if LAGER_EXT_ENABLE_THREAD_SAFE
using SyncMapBuilder = BasicMapBuilder<thread_safe_memory_policy>;
using SyncVectorBuilder = BasicVectorBuilder<thread_safe_memory_policy>;
using SyncArrayBuilder = BasicArrayBuilder<thread_safe_memory_policy>;
using SyncTableBuilder = BasicTableBuilder<thread_safe_memory_policy>;
#endif

} // namespace lager_ext
