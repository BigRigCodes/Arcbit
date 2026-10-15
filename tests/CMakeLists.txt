find_package(Catch2 3 CONFIG REQUIRED) # via vcpkg

# One executable for every engine test. Links the static engine library
# directly, so tests exercise exactly the code the game ships — no window,
# render device, or assets are created.
add_executable(arcbit-tests
    main.cpp
    ecs/WorldTests.cpp
    physics/ContactTrackerTests.cpp
    physics/DirectionArcTests.cpp
    physics/NarrowphaseTests.cpp
    physics/PhysicsWorldTests.cpp
    physics/SpatialHashTests.cpp
    physics/SweepTests.cpp
)

target_link_libraries(arcbit-tests
    PRIVATE
        arcbit-engine
        arcbit-vulkan  # engine TUs the tests pull in (TileMap → TextureManager) reference RenderDevice,
                       # which the backend DLL exports; no device is ever created
        Catch2::Catch2 # not Catch2WithMain — main.cpp initializes engine logging first
)

# Register each TEST_CASE with CTest. PRE_TEST discovery lists the tests when
# ctest runs rather than as a post-build step, so it never races vcpkg's
# copying of runtime DLLs (SDL3, spdlog, fmt) into bin/.
include(Catch)
catch_discover_tests(arcbit-tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
