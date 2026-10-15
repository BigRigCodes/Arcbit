cmake_minimum_required(VERSION 3.25)

project(Arcbit
    VERSION 0.1.0
    DESCRIPTION "Arcbit RPG Engine"
    LANGUAGES CXX C
)

# ---------------------------------------------------------------------------
# Global compiler settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All runtime outputs (exe + dlls) land in the same bin dir so that
# LoadLibrary/dlopen can find renderer backends without any PATH tricks.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)       # Vulkan SDK — headers + vulkan-1.lib
find_package(SDL3 CONFIG REQUIRED)  # via vcpkg
find_package(spdlog CONFIG REQUIRED)# via vcpkg

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(core)      # logging, asserts, profiler — used by engine + backends
add_subdirectory(engine)
add_subdirectory(game)

# Renderer backends — each is a standalone DLL loaded at runtime.
add_subdirectory(backends/vulkan)

# Unit tests — headless (no window or render device). Run with `ctest` from
# the build directory, or the Rider / CTest test runner.
option(ARCBIT_BUILD_TESTS "Build the arcbit-tests unit test executable" ON)
if(ARCBIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
