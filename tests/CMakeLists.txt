find_package(Catch2 3 CONFIG REQUIRED)        # via vcpkg
find_package(nlohmann_json CONFIG REQUIRED) # JSON format tests build documents directly

# One executable for every engine test. Links the static engine library
# directly, so tests exercise exactly the code the game ships — no window,
# render device, or assets are created.
add_executable(arcbit-tests
    main.cpp
    app/DebugConsoleTests.cpp
    assets/SpriteSheetTests.cpp
    audio/VolumeTests.cpp
    core/LocCheckTests.cpp
    core/LocFormatTests.cpp
    core/LocTests.cpp
    core/MathTests.cpp
    core/Utf8Tests.cpp
    dialog/DemoDialogTests.cpp
    dialog/DialogRunnerTests.cpp
    dialog/DialogTests.cpp
    ecs/AnimatorStateMachineTests.cpp
    ecs/InteractionTests.cpp
    ecs/MovementTests.cpp
    ecs/PhysicsResponseTests.cpp
    ecs/SystemsTests.cpp
    ecs/WorldTests.cpp
    ecs/YSortTests.cpp
    events/EntityEventsTests.cpp
    events/EventBusTests.cpp
    events/EventPayloadTests.cpp
    input/BindingUpdateTests.cpp
    input/InputManagerTests.cpp
    physics/ContactTrackerTests.cpp
    physics/DirectionArcTests.cpp
    physics/GeometryTests.cpp
    physics/NarrowphaseTests.cpp
    physics/PhysicsWorldTests.cpp
    physics/SpatialHashTests.cpp
    physics/SweepTests.cpp
    render/Camera2DTests.cpp
    render/FontSdfTests.cpp
    render/FontTests.cpp
    render/RichTextTests.cpp
    render/TextRevealTests.cpp
    save/SaveSealTests.cpp
    save/SaveTests.cpp
    save/SceneStateTests.cpp
    scene/SceneFileTests.cpp
    scene/SceneManagerTests.cpp
    scene/ScenePreloaderTests.cpp
    script/DemoScriptTests.cpp
    script/SceneScriptTests.cpp
    script/ScriptConsoleTests.cpp
    script/ScriptDialogTests.cpp
    script/ScriptApiTests.cpp
    script/ScriptHookTests.cpp
    script/ScriptLocTests.cpp
    script/ScriptTaskTests.cpp
    script/ScriptUITests.cpp
    script/ScriptEngineTests.cpp
    settings/SettingsTests.cpp
    tilemap/TileMapTests.cpp
    ui/AudioSettingsTests.cpp
    ui/ConsoleScreenTests.cpp
    ui/DialogScreenTests.cpp
    ui/UILayoutTests.cpp
    ui/UIManagerTests.cpp
    ui/DropdownTests.cpp
    ui/LocaleUITests.cpp
    ui/TextInputTests.cpp
    ui/TextStyleTests.cpp
    ui/UISkinTests.cpp
)

target_include_directories(arcbit-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}) # TestSupport.h
target_include_directories(arcbit-tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/src) # white-box tests of private helpers (FontSdf.h)

# Lets tests validate the assets that actually ship (engine/assets, game/assets)
# in place, rather than copies.
target_compile_definitions(arcbit-tests PRIVATE ARCBIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(arcbit-tests
    PRIVATE
        arcbit-engine
        arcbit-vulkan  # engine TUs the tests pull in (TileMap → TextureManager) reference RenderDevice,
                       # which the backend DLL exports; no device is ever created
        Catch2::Catch2 # not Catch2WithMain — main.cpp initializes engine logging first
        SDL3::SDL3     # scancode constants for input-injection tests
        nlohmann_json::nlohmann_json
)

# Register each TEST_CASE with CTest. PRE_TEST discovery lists the tests when
# ctest runs rather than as a post-build step, so it never races vcpkg's
# copying of runtime DLLs (SDL3, spdlog, fmt) into bin/.
include(Catch)
catch_discover_tests(arcbit-tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
