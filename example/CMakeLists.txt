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
        # Note: /LTCG removed because lager_ext static library no longer uses /GL
        # /LTCG only benefits when linking /GL-compiled objects
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
# Example 1: Demo (Basic lager_ext usage demo)
# ============================================================

add_lager_ext_example(demo
    SOURCES
        demo/main.cpp
        demo/demo.cpp
)

# ============================================================
# Example 2: Editor (Editor process with Qt support)
# ============================================================

if(Qt6_FOUND)
    add_executable(editor
        editor/editor_main.cpp
    )
    
    set_target_properties(editor PROPERTIES AUTOMOC ON)
    
    target_link_libraries(editor PRIVATE
        lager_ext::lager_ext
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
    )
    
    target_compile_definitions(editor PRIVATE
        LAGER_EXT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
    )
    
    if(MSVC)
        target_compile_options(editor PRIVATE
            /W3
            /Zc:preprocessor
            /bigobj
            /utf-8
            /wd4267
        )
        # Note: /LTCG removed because lager_ext static library no longer uses /GL
    endif()
    
    message(STATUS "  Adding example: editor (with Qt6)")
else()
    message(STATUS "  Skipping editor_example: Qt6 not found")
endif()

# ============================================================
# Example 3: Engine (Engine/Renderer process)
# ============================================================

add_lager_ext_example(engine
    SOURCES
        engine/shared_value_demo.cpp
)

# ============================================================
# Example 4: IPC Benchmark (Channel vs Windows Messages)
# ============================================================

if(LAGER_EXT_ENABLE_IPC)
    add_lager_ext_example(ipc_benchmark
        SOURCES
            ipc_benchmark/main.cpp
    )
    message(STATUS "  Adding example: ipc_benchmark (Channel vs Windows Messages)")
    
    add_lager_ext_example(ipc_demo
        SOURCES
            ipc_demo/main.cpp
    )
    message(STATUS "  Adding example: ipc_demo (Basic IPC API usage)")
else()
    message(STATUS "  Skipping ipc_benchmark: IPC module not enabled")
    message(STATUS "  Skipping ipc_demo: IPC module not enabled")
endif()

# ============================================================
# Example 5: Event Demo (EventBus pub/sub system)
# ============================================================

add_lager_ext_example(event_demo
    SOURCES
        event_demo.cpp
)
message(STATUS "  Adding example: event_demo (EventBus pub/sub demo)")

message(STATUS "")
