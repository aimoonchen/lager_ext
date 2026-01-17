# lager_ext Test Suite
# Using Catch2 v3 via FetchContent

cmake_minimum_required(VERSION 3.16)

# ============================================================
# Catch2 Setup via FetchContent
# ============================================================

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)

FetchContent_MakeAvailable(Catch2)

# ============================================================
# Test Executable
# ============================================================

set(TEST_SOURCES
    test_immer_value.cpp
    test_mutable_value.cpp
    test_event_bus.cpp
    test_shared_value.cpp
    test_path.cpp
    test_lager_lens.cpp
    test_diff.cpp
)

# Add IPC tests only if IPC is enabled
if(LAGER_EXT_ENABLE_IPC)
    list(APPEND TEST_SOURCES test_event_bus_ipc.cpp)
endif()

add_executable(lager_ext_tests ${TEST_SOURCES})

target_link_libraries(lager_ext_tests 
    PRIVATE 
        lager_ext::lager_ext
        Catch2::Catch2WithMain  # Includes main() automatically
)

# C++23 standard (match main library)
target_compile_features(lager_ext_tests PRIVATE cxx_std_23)

# ============================================================
# CTest Integration
# ============================================================

include(CTest)
include(Catch)
catch_discover_tests(lager_ext_tests)

# ============================================================
# Convenience target: run tests
# ============================================================

add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS lager_ext_tests
    COMMENT "Running lager_ext tests..."
)