// api.h - DLL export/import macros for lager_ext

#pragma once

/// @file api.h
/// @brief Cross-platform DLL export/import macros for lager_ext library.
///
/// Usage:
/// - When building lager_ext as a SHARED library:
///   - CMake automatically defines LAGER_EXT_EXPORTS (private) and LAGER_EXT_SHARED (public)
///   - Functions/classes marked with LAGER_EXT_API will be exported
///
/// - When using lager_ext as a SHARED library:
///   - Link against lager_ext target (CMake propagates LAGER_EXT_SHARED)
///   - Functions/classes marked with LAGER_EXT_API will be imported
///
/// - When building/using as a STATIC library:
///   - No macros defined, LAGER_EXT_API expands to nothing
///
/// Example:
/// @code
/// class LAGER_EXT_API MyClass { ... };           // Export entire class
/// LAGER_EXT_API void my_function();              // Export free function
/// @endcode

// ============================================================
// Platform Detection and Export Macro Definition
// ============================================================

#if defined(_WIN32) || defined(_WIN64)
    // Windows platform (MSVC, MinGW, Clang-CL)
    #ifdef LAGER_EXT_SHARED
        #ifdef LAGER_EXT_EXPORTS
            // Building the DLL: export symbols
            #define LAGER_EXT_API __declspec(dllexport)
        #else
            // Using the DLL: import symbols
            #define LAGER_EXT_API __declspec(dllimport)
        #endif
    #else
        // Static library: no decoration needed
        #define LAGER_EXT_API
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang on Unix-like platforms
    #if defined(LAGER_EXT_SHARED) && defined(LAGER_EXT_EXPORTS)
        // Building shared library: set default visibility
        #define LAGER_EXT_API __attribute__((visibility("default")))
    #else
        // Static library or using shared library
        #define LAGER_EXT_API
    #endif
#else
    // Unknown compiler: no decoration
    #define LAGER_EXT_API
#endif

// ============================================================
// Helper Macros
// ============================================================

/// Mark a class for export (use in class declaration)
/// @example class LAGER_EXT_CLASS MyPublicClass { ... };
#define LAGER_EXT_CLASS LAGER_EXT_API

/// Mark a function for export (use before return type)
/// @example LAGER_EXT_FUNC void my_public_function();
#define LAGER_EXT_FUNC LAGER_EXT_API

// ============================================================
// Template Export Helpers (Advanced)
// ============================================================

// For explicit template instantiation in shared libraries
// Usage in header:  LAGER_EXT_EXTERN_TEMPLATE class MyTemplate<int>;
// Usage in source:  LAGER_EXT_EXPORT_TEMPLATE class MyTemplate<int>;

#ifdef _MSC_VER
    // MSVC: extern template prevents instantiation, explicit instantiation exports
    #define LAGER_EXT_EXTERN_TEMPLATE extern template
    #define LAGER_EXT_EXPORT_TEMPLATE template class LAGER_EXT_API
#else
    // GCC/Clang
    #define LAGER_EXT_EXTERN_TEMPLATE extern template
    #define LAGER_EXT_EXPORT_TEMPLATE template class
#endif

// ============================================================
// Deprecation Warnings
// ============================================================

#if defined(__GNUC__) || defined(__clang__)
    #define LAGER_EXT_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
    #define LAGER_EXT_DEPRECATED(msg) __declspec(deprecated(msg))
#else
    #define LAGER_EXT_DEPRECATED(msg)
#endif
