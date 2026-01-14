// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file immer_config.h
/// @brief Immer library configuration for lager_ext
///
/// This file defines the compile-time configuration for the immer library.
/// It MUST be included before any immer headers to ensure consistent settings.
///
/// lager_ext is optimized for single-threaded, high-performance scenarios.
/// All code using lager_ext will automatically inherit these settings,
/// so users don't need to manually define any immer macros.
///
/// @warning Do NOT include immer headers directly without including this file first.
///          All lager_ext public headers already include this file, so users
///          who only use lager_ext headers don't need to do anything special.

#pragma once

// ============================================================
// Immer Library Configuration for lager_ext
// ============================================================

// Guard to ensure this file is included before any immer headers
#if defined(IMMER_CONFIG_HPP_INCLUDED_) && !defined(LAGER_EXT_IMMER_CONFIGURED)
#error "immer headers were included before lager_ext/immer_config.h. " \
       "Please include lager_ext headers before any direct immer includes."
#endif

#define LAGER_EXT_IMMER_CONFIGURED 1

// ============================================================
// Performance Optimization Settings
// ============================================================

/// @brief Disable thread safety for single-threaded performance
/// 
/// This enables:
/// - Non-atomic reference counting (faster inc/dec)
/// - Thread-unsafe free list heap (no locks)
/// - No lock policy for atoms
///
/// Performance improvement: ~15-30%
#ifndef IMMER_NO_THREAD_SAFETY
#define IMMER_NO_THREAD_SAFETY 1
#endif

/// @brief Disable tagged node assertions
///
/// When enabled (=1), immer stores type tags in nodes for runtime
/// assertion checks. Disabling this:
/// - Reduces node memory size
/// - Eliminates assertion overhead
///
/// Performance improvement: ~5-10%
#ifndef IMMER_TAGGED_NODE
#define IMMER_TAGGED_NODE 0
#endif

// ============================================================
// Debug Settings (all disabled for performance)
// ============================================================

/// @brief Disable debug trace output
#ifndef IMMER_DEBUG_TRACES
#define IMMER_DEBUG_TRACES 0
#endif

/// @brief Disable debug print output
#ifndef IMMER_DEBUG_PRINT
#define IMMER_DEBUG_PRINT 0
#endif

/// @brief Disable debug statistics collection
#ifndef IMMER_DEBUG_STATS
#define IMMER_DEBUG_STATS 0
#endif

/// @brief Disable deep data structure consistency checks
#ifndef IMMER_DEBUG_DEEP_CHECK
#define IMMER_DEBUG_DEEP_CHECK 0
#endif

/// @brief Disable debug size heap tracking
#ifndef IMMER_ENABLE_DEBUG_SIZE_HEAP
#define IMMER_ENABLE_DEBUG_SIZE_HEAP 0
#endif

// ============================================================
// Error Handling Settings
// ============================================================

/// @brief Don't throw on invalid state (use assertions instead)
#ifndef IMMER_THROW_ON_INVALID_STATE
#define IMMER_THROW_ON_INVALID_STATE 0
#endif

// ============================================================
// Configuration Summary (compile-time message)
// ============================================================

#ifdef LAGER_EXT_CONFIG_VERBOSE
#if IMMER_NO_THREAD_SAFETY
#pragma message("lager_ext/immer: Thread safety DISABLED (optimized for single-thread)")
#else
#pragma message("lager_ext/immer: Thread safety ENABLED")
#endif

#if IMMER_TAGGED_NODE
#pragma message("lager_ext/immer: Tagged nodes ENABLED (debug mode)")
#else
#pragma message("lager_ext/immer: Tagged nodes DISABLED (optimized)")
#endif
#endif // LAGER_EXT_CONFIG_VERBOSE
