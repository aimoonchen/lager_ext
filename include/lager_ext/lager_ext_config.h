// Copyright (c) 2024-2025 chenmou. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.

/// @file lager_ext_config.h
/// @brief Centralized configuration for lager_ext and its dependencies
///
/// This file defines the compile-time configuration for all third-party libraries
/// used by lager_ext:
///   - immer: Immutable data structures
///   - zug: Functional utilities and transducers
///   - boost: Various utilities (date_time, interprocess, etc.)
///
/// It MUST be included before any library headers to ensure consistent settings.
///
/// lager_ext is optimized for single-threaded, high-performance scenarios.
/// All code using lager_ext will automatically inherit these settings,
/// so users don't need to manually define any library macros.
///
/// @warning Do NOT include library headers directly without including this file first.
///          All lager_ext public headers already include this file, so users
///          who only use lager_ext headers don't need to do anything special.

#pragma once

// ============================================================
// Configuration Guard
// ============================================================
// Ensure this file is included before any library headers

#if defined(IMMER_CONFIG_HPP_INCLUDED_) && !defined(LAGER_EXT_CONFIGURED)
#error "immer headers were included before lager_ext/lager_ext_config.h. " \
       "Please include lager_ext headers before any direct immer includes."
#endif

#define LAGER_EXT_CONFIGURED 1

// Legacy macro for backward compatibility (if users check LAGER_EXT_IMMER_CONFIGURED)
#define LAGER_EXT_IMMER_CONFIGURED 1

// ============================================================
// Immer Performance Optimization Settings
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
// Immer Debug Settings (all disabled for performance)
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
// Immer Error Handling Settings
// ============================================================

/// @brief Don't throw on invalid state (use assertions instead)
#ifndef IMMER_THROW_ON_INVALID_STATE
#define IMMER_THROW_ON_INVALID_STATE 0
#endif

// ============================================================
// Lager Library Configuration
// ============================================================

/// @brief Disable store dependency SFINAE checks
///
/// On MSVC, this is already disabled by default due to template instantiation
/// issues. Explicitly disabling it on all platforms provides:
/// - Faster compilation (less complex SFINAE in deps.hpp)
/// - Avoids potential template errors with boost::hana intersection
///
/// Note: This only affects compile-time dependency validation in lager::deps,
/// not runtime behavior.
#ifndef LAGER_DISABLE_STORE_DEPENDENCY_CHECKS
#define LAGER_DISABLE_STORE_DEPENDENCY_CHECKS 1
#endif

// ============================================================
// Zug Library Configuration
// ============================================================

/// @brief Force zug to use std::variant instead of boost::variant
///
/// This ensures:
/// - No boost::variant dependency in zug-based code
/// - Consistent variant type across the codebase
/// - Better integration with C++17 standard library
#ifndef ZUG_VARIANT_STD
#define ZUG_VARIANT_STD 1
#endif

// ============================================================
// Boost Library Configuration
// ============================================================

/// @brief Disable Boost auto-linking for date_time library (MSVC)
///
/// By default, MSVC tries to auto-link boost_date_time when boost
/// headers are included. This macro prevents that behavior, which is
/// useful when:
/// - Using header-only boost::posix_time features
/// - Not needing the full date_time library
/// - Avoiding unnecessary link dependencies
#ifndef BOOST_DATE_TIME_NO_LIB
#define BOOST_DATE_TIME_NO_LIB 1
#endif

/// @brief Disable Boost auto-linking for all libraries (MSVC)
///
/// This is a more aggressive option that prevents MSVC from
/// auto-linking ANY boost library. Useful for header-only usage
/// or when manually managing link dependencies.
#ifndef BOOST_ALL_NO_LIB
#define BOOST_ALL_NO_LIB 1
#endif

// ============================================================
// Feature Toggle: IPC (Inter-Process Communication)
// ============================================================

/// @brief Enable IPC features (SharedBufferSPSC, SharedValue, etc.)
///
/// When enabled (default):
/// - SharedBufferSPSC: Lock-free SPSC buffer for cross-process data transfer
/// - SharedValue: Zero-copy Value type in shared memory
/// - SharedValueHandle: Convenient shared memory management
///
/// Dependencies when enabled:
/// - Boost.Interprocess (bundled with lager_ext, not exposed in headers)
///
/// To disable IPC features (reduce binary size):
///   #define LAGER_EXT_ENABLE_IPC 0
///   #include <lager_ext/lager_ext_config.h>
#ifndef LAGER_EXT_ENABLE_IPC
#define LAGER_EXT_ENABLE_IPC 1
#endif

// ============================================================
// Configuration Summary (compile-time message)
// ============================================================

#ifdef LAGER_EXT_CONFIG_VERBOSE
#if IMMER_NO_THREAD_SAFETY
#pragma message("lager_ext: Thread safety DISABLED (optimized for single-thread)")
#else
#pragma message("lager_ext: Thread safety ENABLED")
#endif

#if IMMER_TAGGED_NODE
#pragma message("lager_ext: Tagged nodes ENABLED (debug mode)")
#else
#pragma message("lager_ext: Tagged nodes DISABLED (optimized)")
#endif

#if ZUG_VARIANT_STD
#pragma message("lager_ext: Using std::variant for zug")
#endif

#if BOOST_ALL_NO_LIB
#pragma message("lager_ext: Boost auto-linking DISABLED")
#endif
#endif // LAGER_EXT_CONFIG_VERBOSE