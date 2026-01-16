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
///   Value create_config();                    // OK - Forward declaration
///   MapBuilder& configure_builder();          // OK - Forward declaration
///
///   // In implementation file:
///   #include <lager_ext/value.h>
///   #include <lager_ext/builders.h>           // Include full definitions when needed
///   Value create_config() { return Value::map({}); }  // OK - Full definition available
/// @endcode

#pragma once

// IMPORTANT: Include lager_ext configuration BEFORE any library headers
// This ensures consistent macro settings across all compilation units
#include <lager_ext/lager_ext_config.h>

namespace lager_ext {

// ============================================================
// Value Type Forward Declarations
// ============================================================
//
// Value is a concrete type (not a template) optimized for single-threaded use.
// Since IMMER_NO_THREAD_SAFETY=1 is set in lager_ext_config.h:
//   - immer::default_memory_policy is already the optimal single-threaded policy
//   - All immer containers use this policy by default
//
// ============================================================

/// @brief Forward declaration of the Value type
/// 
/// This is the main type for representing JSON-like dynamic data.
/// Uses immer::default_memory_policy which is configured for single-threaded
/// high-performance use via lager_ext_config.h.
struct Value;

// ============================================================
// Builder Type Forward Declarations
// ============================================================

/// @brief Builder for constructing map Values
class MapBuilder;

/// @brief Builder for constructing vector Values
class VectorBuilder;

/// @brief Builder for constructing array Values  
class ArrayBuilder;

/// @brief Builder for constructing table Values
class TableBuilder;

} // namespace lager_ext