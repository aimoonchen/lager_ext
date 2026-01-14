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

// IMPORTANT: Include immer configuration BEFORE any immer headers
// This ensures consistent macro settings across all compilation units
#include <lager_ext/immer_config.h>

#include <immer/memory_policy.hpp>

namespace lager_ext {

// ============================================================
// Value Type Forward Declarations
// ============================================================
//
// Since IMMER_NO_THREAD_SAFETY=1 is set in immer_config.h:
//   - immer::default_memory_policy is already the optimal single-threaded policy
//   - No custom memory policy aliases needed
//   - BasicValue template uses immer::default_memory_policy as default parameter
//
// ============================================================

// Forward declare the basic template
template <typename MemoryPolicy = immer::default_memory_policy>
struct BasicValue;

/// @brief The primary Value type - optimized for single-threaded use
/// 
/// This is the main type for representing JSON-like dynamic data.
/// Uses immer::default_memory_policy which is configured for single-threaded
/// high-performance use via immer_config.h.
using Value = BasicValue<>;

// ============================================================
// Builder Type Forward Declarations
// ============================================================

// Forward declare builder templates
template <typename MemoryPolicy = immer::default_memory_policy>
class BasicMapBuilder;
template <typename MemoryPolicy = immer::default_memory_policy>
class BasicVectorBuilder;
template <typename MemoryPolicy = immer::default_memory_policy>
class BasicArrayBuilder;
template <typename MemoryPolicy = immer::default_memory_policy>
class BasicTableBuilder;

/// @brief Builder for constructing map Values
using MapBuilder = BasicMapBuilder<>;

/// @brief Builder for constructing vector Values
using VectorBuilder = BasicVectorBuilder<>;

/// @brief Builder for constructing array Values  
using ArrayBuilder = BasicArrayBuilder<>;

/// @brief Builder for constructing table Values
using TableBuilder = BasicTableBuilder<>;

} // namespace lager_ext