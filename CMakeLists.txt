# lager_ext - Immutable Data Structure Extensions for Lager
# Copyright (c) 2024 chenmou. All rights reserved.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.16)
project(lager_ext VERSION 1.0.0 LANGUAGES CXX)

# ============================================================
# Build Options
# ============================================================

option(LAGER_EXT_BUILD_SHARED "Build lager_ext as shared library" OFF)
option(LAGER_EXT_BUILD_EXAMPLES "Build example programs" ON)

# ============================================================
# C++ Standard
# ============================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Library Source Files
# ============================================================

# Collect all source files from source directory
file(GLOB LAGER_EXT_SOURCES "source/*.cpp")

set(LAGER_EXT_HEADERS
    include/lager_ext/api.h
    include/lager_ext/at_lens.h
    include/lager_ext/concepts.h
    include/lager_ext/delta_undo.h
    include/lager_ext/value_diff.h
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
)

# ============================================================
# Build Library (Static or Shared)
# ============================================================

if(LAGER_EXT_BUILD_SHARED)
    add_library(lager_ext SHARED ${LAGER_EXT_SOURCES} ${LAGER_EXT_HEADERS})
    target_compile_definitions(lager_ext 
        PRIVATE LAGER_EXT_EXPORTS
        PUBLIC LAGER_EXT_SHARED
    )
    message(STATUS "Building lager_ext as SHARED library")
else()
    add_library(lager_ext STATIC ${LAGER_EXT_SOURCES} ${LAGER_EXT_HEADERS})
    message(STATUS "Building lager_ext as STATIC library")
endif()

# Library alias for modern CMake usage
add_library(lager_ext::lager_ext ALIAS lager_ext)

# ============================================================
# Include Directories
# ============================================================

target_include_directories(lager_ext
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ============================================================
# Compile Definitions
# ============================================================

target_compile_definitions(lager_ext PUBLIC
    # zug uses std::variant instead of Boost.Variant
    ZUG_VARIANT_STD
    # Boost.Interprocess needs this macro to avoid linking date_time library
    BOOST_DATE_TIME_NO_LIB
)

# ============================================================
# Compiler Options
# ============================================================

if(MSVC)
    target_compile_options(lager_ext PRIVATE
        /W3              # Warning level 3
        /Zc:preprocessor # Standards-conforming preprocessor
        /bigobj          # Support large object files
        /utf-8           # UTF-8 source and execution character sets
        /wd4267          # Disable warning C4267: conversion from 'size_t' to 'type'
    )
else()
    target_compile_options(lager_ext PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# ============================================================
# Build Examples
# ============================================================

if(LAGER_EXT_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

# ============================================================
# Installation (Optional)
# ============================================================

include(GNUInstallDirs)

install(TARGETS lager_ext
    EXPORT lager_ext-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/lager_ext
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT lager_ext-targets
    FILE lager_ext-targets.cmake
    NAMESPACE lager_ext::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lager_ext
)

# ============================================================
# Output Configuration Summary
# ============================================================

message(STATUS "")
message(STATUS "lager_ext Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Library type:   $<IF:$<BOOL:${LAGER_EXT_BUILD_SHARED}>,SHARED,STATIC>")
message(STATUS "  Build examples: ${LAGER_EXT_BUILD_EXAMPLES}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
