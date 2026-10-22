find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED) # header-only; exposes ${Stb_INCLUDE_DIR}
find_package(unofficial-sodium CONFIG REQUIRED) # libsodium: sealed saves (SaveSeal)
find_package(zstd CONFIG REQUIRED)              # compression for sealed saves
find_package(Lua REQUIRED)                      # Lua 5.4: scripting (LUA_INCLUDE_DIR / LUA_LIBRARIES)
find_path(SOL2_INCLUDE_DIRS "sol/abort.hpp" REQUIRED) # sol2: header-only C++ binding for Lua
# miniaudio is header-only; vcpkg adds its include path automatically — no find_package needed.

# arcbit-engine is a STATIC library that provides the engine's public API:
# graphics abstraction, render thread, window, input, and (in later phases) ECS, audio, etc.
# Game and tool targets link against this; they never see Vulkan or SDL headers directly.
add_library(arcbit-engine STATIC
    src/Application.cpp
    src/ApplicationBoot.cpp
    src/ApplicationDialog.cpp
    src/ApplicationItems.cpp
    src/ApplicationLocale.cpp
    src/ApplicationSaves.cpp
    src/DebugConsole.cpp
    src/UserFolder.cpp
    src/Camera2D.cpp
    src/RenderThread.cpp
    src/Window.cpp
    src/Clipboard.cpp
    src/InputManager.cpp
    src/BindingUpdate.cpp
    src/Settings.cpp
    src/TextureManager.cpp
    src/SpriteSheet.cpp
    src/World.cpp
    src/Systems.cpp
    src/Movement.cpp
    src/Scene.cpp
    src/PrefabLibrary.cpp
    src/Currencies.cpp
    src/Inventories.cpp
    src/Inventory.cpp
    src/Party.cpp
    src/Stats.cpp
    src/InventoryGrid.cpp
    src/ItemLibrary.cpp
    src/SceneManager.cpp
    src/ScenePreloader.cpp
    src/SceneRegistry.cpp
    src/Dialog.cpp
    src/DialogLibrary.cpp
    src/DialogRunner.cpp
    src/DialogScreen.cpp
    src/SaveData.cpp
    src/SaveGame.cpp
    src/SaveManager.cpp
    src/SaveThumbnail.cpp
    src/ScriptEngine.cpp
    src/ScriptLibrary.cpp
    src/ScriptLoc.cpp
    src/ScriptDialog.cpp
    src/ScriptEntity.cpp
    src/ScriptItems.cpp
    src/ScriptParty.cpp
    src/ScenePickups.cpp
    src/ScriptGame.cpp
    src/ScriptTasks.cpp
    src/ScriptScreens.cpp
    src/ScriptReload.cpp
    src/ScriptComplete.cpp
    src/ScriptConsole.cpp
    src/ConsoleScreen.cpp
    src/ApplicationConsole.cpp
    src/ScriptWorld.cpp
    src/ScriptJson.cpp
    src/SceneScripts.cpp
    src/Interaction.cpp
    src/SaveSeal.cpp
    src/SceneState.cpp
    src/SceneComponents.cpp
    src/SceneFile.cpp
    src/FileScene.cpp
    src/AnimatorStateMachine.cpp
    src/AudioCategories.cpp
    src/AudioManager.cpp
    src/TileAtlas.cpp
    src/TileMap.cpp
    src/Font.cpp
    src/RichText.cpp
    src/TextReveal.cpp
    src/FontSdf.cpp
    src/UIWidget.cpp
    src/UIScreen.cpp
    src/Widgets.cpp
    src/UIManager.cpp
    src/UISkin.cpp
    src/HudScreen.cpp
    src/PauseMenuScreen.cpp
    src/InputRebindScreen.cpp
    src/AudioSettingsScreen.cpp
    src/GraphicsSettingsScreen.cpp
    src/SplashScreen.cpp
    src/CreditsScreen.cpp
    src/LoadingScreen.cpp
    src/MainMenuScreen.cpp
    src/NoticeScreen.cpp
    src/ConfirmScreen.cpp
    src/GameSettingsScreen.cpp
    src/SaveSlotScreen.cpp
    src/SavingIndicator.cpp
    src/Screenshot.cpp
    src/UILoader.cpp
    src/InputWidgets.cpp
    src/Loc.cpp
    src/LocCheck.cpp
    src/LocFormat.cpp
    src/LocPlural.cpp
    src/LocPseudo.cpp
    src/Utf8.cpp
    src/SpatialHash.cpp
    src/PhysicsWorld.cpp
    src/Narrowphase.cpp
    src/Sweep.cpp
    src/DirectionArc.cpp
    src/ContactTracker.cpp
    src/EventBus.cpp
    src/EventPayload.cpp
)

target_include_directories(arcbit-engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arcbit-engine
    PUBLIC
        arcbit-core
        nlohmann_json::nlohmann_json  # public headers use it: SceneRegistry / SceneFile take JSON, SpriteSheet::FromJson
    PRIVATE
        SDL3::SDL3                    # used in Window.cpp, InputManager.cpp and Clipboard.cpp; not exposed through public headers
        unofficial-sodium::sodium     # SaveSeal.cpp only
        zstd::libzstd                 # SaveSeal.cpp only
        ${LUA_LIBRARIES}
)

# stb is header-only; vcpkg exposes it as an include directory, not a CMake target.
# TextureManager.cpp uses STB_IMAGE_IMPLEMENTATION; Font.cpp uses STB_TRUETYPE_IMPLEMENTATION.
target_include_directories(arcbit-engine PRIVATE ${Stb_INCLUDE_DIR})
# Lua and sol2 stay out of public headers: sol2 is template-heavy, so only the
# Script*.cpp files include it and the rest of the engine builds as fast as before.
target_include_directories(arcbit-engine PRIVATE ${LUA_INCLUDE_DIR} ${SOL2_INCLUDE_DIRS})

# Engine does NOT link Vulkan directly — its render interfaces are
# graphics-agnostic. Backends link Vulkan; the game links the backend DLL.

# ---------------------------------------------------------------------------
# Engine shader compilation — sprite and SDF shaders are owned by the engine
# (loaded by RenderThread) and must ship alongside any game built on Arcbit.
# ---------------------------------------------------------------------------

if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
endif()

set(ENGINE_SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(ENGINE_SHADER_OUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")

set(ENGINE_SHADER_SOURCES
    sprite.vert
    sprite.frag
    sdf.vert
    sdf.frag
    ui.vert
    ui.frag
)

set(ENGINE_SHADER_OUTPUTS)
foreach(SRC IN LISTS ENGINE_SHADER_SOURCES)
    set(IN_FILE  "${ENGINE_SHADER_SRC_DIR}/${SRC}")
    set(OUT_FILE "${ENGINE_SHADER_OUT_DIR}/${SRC}.spv")

    add_custom_command(
        OUTPUT  "${OUT_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ENGINE_SHADER_OUT_DIR}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${IN_FILE}" -o "${OUT_FILE}"
        DEPENDS "${IN_FILE}"
        COMMENT "Compiling engine shader: ${SRC}"
        VERBATIM
    )
    list(APPEND ENGINE_SHADER_OUTPUTS "${OUT_FILE}")
endforeach()

add_custom_target(arcbit-engine-shaders ALL DEPENDS ${ENGINE_SHADER_OUTPUTS})

# ---------------------------------------------------------------------------
# Engine fonts — Roboto-Regular.ttf ships with the engine for the debug overlay.
# ---------------------------------------------------------------------------

add_custom_target(arcbit-engine-ui-assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/ArcbitA_withbackground.png"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/ui/ArcbitA_withbackground.png"
    COMMENT "Copying engine UI assets to bin/assets/engine/ui/"
    VERBATIM
)

add_custom_target(arcbit-engine-locale-assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/locale"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/locale"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/locale"
    COMMENT "Copying engine locale assets to bin/assets/engine/locale/"
    VERBATIM
)

add_custom_target(arcbit-engine-fonts ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/fonts"
    COMMENT "Copying engine fonts to bin/assets/engine/fonts/"
    VERBATIM
)

add_custom_target(arcbit-engine-skins ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/skins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/skins"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/engine/skins"
    COMMENT "Copying engine skins to bin/assets/engine/skins/"
    VERBATIM
)
