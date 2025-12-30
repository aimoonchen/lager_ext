# lager_ext Examples
# Copyright (c) 2024 chenmou. All rights reserved.

# ============================================================
# Qt6 Configuration
# ============================================================

# Qt6 configuration - Update this path to your Qt6 installation
set(Qt6_DIR "D:/QT/6.10.1/msvc2022_64/lib/cmake/Qt6" CACHE PATH "Qt6 cmake directory")

# Find Qt6
find_package(Qt6 COMPONENTS Core Concurrent Gui Widgets Qml Quick QuickControls2 QUIET)
if(Qt6_FOUND)
    message(STATUS "Found Qt6: ${Qt6Core_VERSION}")
endif()

# ============================================================
# Common settings for all examples
# ============================================================

# Helper function to create example executable
function(add_lager_ext_example TARGET_NAME)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(EXAMPLE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    add_executable(${TARGET_NAME} ${EXAMPLE_SOURCES})
    
    # Link to lager_ext library
    target_link_libraries(${TARGET_NAME} PRIVATE lager_ext::lager_ext)
    
    # Compiler options
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W3
            /Zc:preprocessor
            /bigobj
            /utf-8
            /wd4267
        )
        # Enable LTCG for Release builds (matches /GL in lager_ext)
        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:/LTCG>
            $<$<CONFIG:RelWithDebInfo>:/LTCG>
            $<$<CONFIG:MinSizeRel>:/LTCG>
        )
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall
            -Wextra
        )
    endif()
    
    message(STATUS "  Adding example: ${TARGET_NAME}")
endfunction()

message(STATUS "Configuring examples:")

# ============================================================
# Example 1: Base (Basic lager_ext usage demo)
# ============================================================

add_lager_ext_example(base_example
    SOURCES
        base/main.cpp
)

# ============================================================
# Example 2: Editor (Editor process with Qt support)
# ============================================================

if(Qt6_FOUND)
    add_executable(editor_example
        editor/editor_main.cpp
    )
    
    set_target_properties(editor_example PROPERTIES AUTOMOC ON)
    
    target_link_libraries(editor_example PRIVATE
        lager_ext::lager_ext
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
    )
    
    target_compile_definitions(editor_example PRIVATE
        LAGER_EXT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
    )
    
    if(MSVC)
        target_compile_options(editor_example PRIVATE
            /W3
            /Zc:preprocessor
            /bigobj
            /utf-8
            /wd4267
        )
        # Enable LTCG for Release builds (matches /GL in lager_ext)
        target_link_options(editor_example PRIVATE
            $<$<CONFIG:Release>:/LTCG>
            $<$<CONFIG:RelWithDebInfo>:/LTCG>
            $<$<CONFIG:MinSizeRel>:/LTCG>
        )
    endif()
    
    message(STATUS "  Adding example: editor_example (with Qt6)")
else()
    message(STATUS "  Skipping editor_example: Qt6 not found")
endif()

# ============================================================
# Example 3: Engine (Engine/Renderer process)
# ============================================================

add_lager_ext_example(engine_example
    SOURCES
        engine/shared_value_demo.cpp
)

message(STATUS "")