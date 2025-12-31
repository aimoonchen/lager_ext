# lager_ext - Immutable Data Structure Extensions for Lager
# Copyright (c) 2024 chenmou. All rights reserved.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.16)
project(lager_ext 
    VERSION 1.0.0 
    DESCRIPTION "Immutable data structure extensions for lager state management"
    HOMEPAGE_URL "https://github.com/aimoonchen/lager_ext"
    LANGUAGES CXX
)

# ============================================================
# CMake Policies and Module Path
# ============================================================

# Ensure CMake uses new behavior for policies
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)  # option() honors normal variables
endif()

# ============================================================
# Build Options
# ============================================================

option(LAGER_EXT_BUILD_SHARED "Build lager_ext as shared library" OFF)
option(LAGER_EXT_BUILD_EXAMPLES "Build example programs" ON)
option(LAGER_EXT_BUILD_TESTS "Build unit tests" OFF)
option(LAGER_EXT_INSTALL "Generate install target" OFF)

# ============================================================
# Determine if this is a top-level project
# ============================================================

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(LAGER_EXT_IS_TOP_LEVEL TRUE)
else()
    set(LAGER_EXT_IS_TOP_LEVEL FALSE)
endif()

# ============================================================
# Library Source Files (explicit list recommended over GLOB)
# ============================================================

set(LAGER_EXT_SOURCES
    source/delta_undo.cpp
    source/editor_engine.cpp
    source/lager_lens.cpp
    source/multi_store.cpp
    source/path_utils.cpp
    source/shared_state.cpp
    source/shared_value_region.cpp
    source/string_path.cpp
    source/value.cpp
    source/value_diff.cpp
)

set(LAGER_EXT_HEADERS
    include/lager_ext/api.h
    include/lager_ext/concepts.h
    include/lager_ext/delta_undo.h
    include/lager_ext/editor_engine.h
    include/lager_ext/fast_shared_value.h
    include/lager_ext/lager_lens.h
    include/lager_ext/multi_store.h
    include/lager_ext/path_utils.h
    include/lager_ext/scene_types.h
    include/lager_ext/shared_state.h
    include/lager_ext/shared_value.h
    include/lager_ext/static_path.h
    include/lager_ext/string_path.h
    include/lager_ext/value.h
    include/lager_ext/value_diff.h
)

# ============================================================
# Create Library Target
# ============================================================

if(LAGER_EXT_BUILD_SHARED)
    add_library(lager_ext SHARED ${LAGER_EXT_SOURCES} ${LAGER_EXT_HEADERS})
    target_compile_definitions(lager_ext 
        PRIVATE LAGER_EXT_EXPORTS
        PUBLIC LAGER_EXT_SHARED
    )
    
    # ========================================
    # Shared Library Properties (Industry Standard)
    # ========================================
    set_target_properties(lager_ext PROPERTIES
        # Position Independent Code (required for shared libraries)
        POSITION_INDEPENDENT_CODE ON
        
        # Version information for shared libraries
        # - VERSION: full version (libfoo.so.1.0.0)
        # - SOVERSION: API version for symlinks (libfoo.so.1)
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        
        # Windows DLL settings
        # - Place DLL next to executables in bin directory
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        
        # Debug postfix for Windows (foo.dll vs food.dll)
        DEBUG_POSTFIX "d"
    )
    
    # Windows-specific: Generate .lib import library alongside .dll
    if(WIN32)
        set_target_properties(lager_ext PROPERTIES
            # Ensure import library is generated
            WINDOWS_EXPORT_ALL_SYMBOLS OFF  # We use explicit LAGER_EXT_API
        )
    endif()
    
    message(STATUS "Building lager_ext as SHARED library")
else()
    add_library(lager_ext STATIC ${LAGER_EXT_SOURCES} ${LAGER_EXT_HEADERS})
    message(STATUS "Building lager_ext as STATIC library")
endif()

# Library alias for modern CMake usage (supports both add_subdirectory and find_package)
add_library(lager_ext::lager_ext ALIAS lager_ext)

# ============================================================
# C++ Standard (use target_compile_features for proper propagation)
# ============================================================

target_compile_features(lager_ext PUBLIC cxx_std_20)

# ============================================================
# Include Directories
# ============================================================

target_include_directories(lager_ext
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Private dependency: boost (isolated from public API)
        # Users won't see this path - prevents conflict with user's own boost
        ${CMAKE_CURRENT_SOURCE_DIR}/source/deps
)

# ============================================================
# Compile Definitions
# ============================================================

target_compile_definitions(lager_ext 
    PUBLIC
        # zug uses std::variant instead of Boost.Variant
        ZUG_VARIANT_STD
        # Boost.Interprocess needs this macro to avoid linking date_time library
        BOOST_DATE_TIME_NO_LIB
    PRIVATE
        # Disable debug checks in release mode
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:MinSizeRel>:NDEBUG>
)

# ============================================================
# Compiler Options
# ============================================================

if(MSVC)
    target_compile_options(lager_ext PRIVATE
        # Basic options (all configurations)
        /W3              # Warning level 3
        /Zc:preprocessor # Standards-conforming preprocessor
        /bigobj          # Support large object files (needed for heavy template usage)
        /utf-8           # UTF-8 source and execution character sets
        /EHsc            # Standard C++ exception handling
        /permissive-     # Standards conformance mode
        
        # Disable specific warnings
        /wd4267          # conversion from 'size_t' to 'type'
        /wd4244          # conversion from 'type1' to 'type2', possible loss of data
        
        # Debug configuration
        $<$<CONFIG:Debug>:/Od>          # Disable optimization
        $<$<CONFIG:Debug>:/Zi>          # Debug information format
        $<$<CONFIG:Debug>:/RTC1>        # Runtime checks
        
        # Release optimizations
        $<$<CONFIG:Release>:/O2>        # Optimize for speed
        $<$<CONFIG:Release>:/Ob2>       # Inline function expansion
        $<$<CONFIG:Release>:/Oi>        # Enable intrinsic functions
        $<$<CONFIG:Release>:/Gy>        # Enable function-level linking (enables /OPT:REF)
        $<$<CONFIG:Release>:/Gw>        # Optimize global data
    )
    
    # ========================================
    # LTO / Whole Program Optimization
    # ========================================
    # Static vs Shared library optimization strategy:
    #
    # STATIC LIBRARIES: Do NOT use /GL (LTCG)
    #   - /GL creates non-standard LTCG IR instead of standard COFF objects
    #   - Different MSVC versions may fail to link /GL-compiled .lib files
    #   - Forces consumers to use /LTCG, reducing library portability
    #   - Industry standard: Boost, Qt, OpenCV, vcpkg packages avoid /GL
    #   - Dead code elimination achieved via /Gy + linker /OPT:REF,ICF
    #
    # SHARED LIBRARIES (DLLs): CAN use /GL + /LTCG
    #   - DLLs are self-contained binaries, no ABI concerns with consumers
    #   - /GL enables aggressive cross-TU inlining and optimization
    #   - Industry standard: Qt, OpenCV DLLs use LTO for release builds
    #
    if(LAGER_EXT_BUILD_SHARED)
        target_compile_options(lager_ext PRIVATE
            $<$<CONFIG:Release>:/GL>    # Whole program optimization for DLLs
        )
        target_link_options(lager_ext PRIVATE
            $<$<CONFIG:Release>:/LTCG>  # Link-time code generation
        )
    endif()
    
    target_compile_options(lager_ext PRIVATE
        # RelWithDebInfo - optimized but debuggable
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Ob1>
        $<$<CONFIG:RelWithDebInfo>:/Zi>
        $<$<CONFIG:RelWithDebInfo>:/Gy>
        
        # MinSizeRel optimizations
        $<$<CONFIG:MinSizeRel>:/O1>     # Optimize for size
        $<$<CONFIG:MinSizeRel>:/Ob1>
        $<$<CONFIG:MinSizeRel>:/Gy>
        $<$<CONFIG:MinSizeRel>:/Gw>
    )
    
    # MSVC runtime library configuration
    # Use dynamic CRT (DLL) by default - this is the modern recommended approach
    # and matches Qt's default configuration
    set_property(TARGET lager_ext PROPERTY 
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    # GCC / Clang options
    target_compile_options(lager_ext PRIVATE
        # Warnings (all configurations)
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        
        # Debug configuration
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g3>
        
        # Symbol visibility (Release only - hide by default)
        $<$<CONFIG:Release>:-fvisibility=hidden>
        $<$<CONFIG:Release>:-fvisibility-inlines-hidden>
        
        # Release optimizations
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-DNDEBUG>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
        
        # RelWithDebInfo - optimized but debuggable
        $<$<CONFIG:RelWithDebInfo>:-O2>
        $<$<CONFIG:RelWithDebInfo>:-g>
        $<$<CONFIG:RelWithDebInfo>:-ffunction-sections>
        $<$<CONFIG:RelWithDebInfo>:-fdata-sections>
        
        # MinSizeRel optimizations
        $<$<CONFIG:MinSizeRel>:-Os>
        $<$<CONFIG:MinSizeRel>:-DNDEBUG>
        $<$<CONFIG:MinSizeRel>:-ffunction-sections>
        $<$<CONFIG:MinSizeRel>:-fdata-sections>
    )
    
    # LTO for shared libraries (same rationale as MSVC /GL)
    if(LAGER_EXT_BUILD_SHARED)
        target_compile_options(lager_ext PRIVATE
            $<$<CONFIG:Release>:-flto>
        )
        target_link_options(lager_ext PRIVATE
            $<$<CONFIG:Release>:-flto>
        )
    endif()
    
    # Linker options for dead code elimination (Release configurations)
    if(NOT APPLE)
        target_link_options(lager_ext PRIVATE
            $<$<CONFIG:Release>:-Wl,--gc-sections>
            $<$<CONFIG:RelWithDebInfo>:-Wl,--gc-sections>
            $<$<CONFIG:MinSizeRel>:-Wl,--gc-sections>
        )
    else()
        # macOS uses different linker flags
        target_link_options(lager_ext PRIVATE
            $<$<CONFIG:Release>:-Wl,-dead_strip>
            $<$<CONFIG:RelWithDebInfo>:-Wl,-dead_strip>
            $<$<CONFIG:MinSizeRel>:-Wl,-dead_strip>
        )
    endif()
endif()

# ============================================================
# Build Examples (only if top-level project or explicitly requested)
# ============================================================

if(LAGER_EXT_BUILD_EXAMPLES AND LAGER_EXT_IS_TOP_LEVEL)
    add_subdirectory(example)
endif()

# ============================================================
# Installation
# ============================================================

if(LAGER_EXT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    
    # Install library
    install(TARGETS lager_ext
        EXPORT lager_ext-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    
    # Install headers
    install(DIRECTORY include/lager_ext
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
    
    # Install dependency headers (immer, lager, zug, boost)
    # Note: These are bundled with the project
    foreach(_dep_dir immer lager zug boost)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/${_dep_dir}")
            install(DIRECTORY "include/${_dep_dir}"
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
            )
        endif()
    endforeach()
    
    # Generate and install package config files
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/lager_ext-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/lager_ext-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/lager_ext-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lager_ext
    )
    
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/lager_ext-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/lager_ext-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lager_ext
    )
    
    install(EXPORT lager_ext-targets
        FILE lager_ext-targets.cmake
        NAMESPACE lager_ext::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lager_ext
    )
endif()

# ============================================================
# Output Configuration Summary
# ============================================================

if(LAGER_EXT_IS_TOP_LEVEL)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS "lager_ext ${PROJECT_VERSION} Configuration")
    message(STATUS "========================================")
    message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
    if(LAGER_EXT_BUILD_SHARED)
        message(STATUS "  Library type:    SHARED")
    else()
        message(STATUS "  Library type:    STATIC")
    endif()
    message(STATUS "  C++ Standard:    C++20")
    message(STATUS "  Build examples:  ${LAGER_EXT_BUILD_EXAMPLES}")
    message(STATUS "  Build tests:     ${LAGER_EXT_BUILD_TESTS}")
    message(STATUS "  Install:         ${LAGER_EXT_INSTALL}")
    message(STATUS "  Install prefix:  ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "========================================")
    message(STATUS "")
endif()