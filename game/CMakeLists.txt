add_executable(arcbit-game
    src/main.cpp
    src/Common.cpp
    src/DemoSelectScreen.cpp
    src/ArcDemo.cpp
    src/ObbDemo.cpp
    src/TriggerDemo.cpp
    src/PhysicsDemo.cpp
    src/TileMoveDemo.cpp
    src/TileMoveArcDemo.cpp
    src/WorldDemo.cpp
    src/WidgetTestScreen.cpp
)

target_link_libraries(arcbit-game PRIVATE
    arcbit-engine      # pulls in arcbit-core transitively 
    arcbit-vulkan      # renderer backend DLL (implicit link)
    SDL3::SDL3
)

# ARCBIT_DEBUG propagates from arcbit-core as a PUBLIC define.

# ---------------------------------------------------------------------------
# Shader compilation — .vert / .frag → .spv via glslc (Vulkan SDK)
# ---------------------------------------------------------------------------

# Vulkan's FindVulkan module exposes Vulkan_GLSLC_EXECUTABLE on CMake 3.21+.
# Fall back to a manual find_program if it isn't set.
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
endif()

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")

set(SHADER_SOURCES
    triangle.vert
    triangle.frag
    quad.vert
    quad.frag
    forward.vert
    forward.frag
)

set(SHADER_OUTPUTS)
foreach(SRC IN LISTS SHADER_SOURCES)
    set(IN_FILE  "${SHADER_SRC_DIR}/${SRC}")
    set(OUT_FILE "${SHADER_OUT_DIR}/${SRC}.spv")

    add_custom_command(
        OUTPUT  "${OUT_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUT_DIR}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${IN_FILE}" -o "${OUT_FILE}"
        DEPENDS "${IN_FILE}"
        COMMENT "Compiling shader: ${SRC}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${OUT_FILE}")
endforeach()

add_custom_target(arcbit-shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(arcbit-game arcbit-shaders)

# ---------------------------------------------------------------------------
# Asset sync — mirror game/assets/ into bin/assets/ on every build.
#
# Delegates to cmake/CopyAssets.cmake which uses file(COPY), giving us
# timestamp-based diffing for free: unchanged files are skipped, new files
# are picked up without any CMake reconfigure.
# ---------------------------------------------------------------------------
add_custom_target(arcbit-assets ALL
    COMMAND ${CMAKE_COMMAND}
        -D "ASSETS_SRC=${CMAKE_CURRENT_SOURCE_DIR}/assets"
        -D "ASSETS_DST=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyAssets.cmake"
    COMMENT "Syncing assets to bin/"
    VERBATIM
)
add_dependencies(arcbit-game arcbit-assets)

# arcbit-engine-shaders and arcbit-engine-fonts are defined in engine/CMakeLists.txt.
add_dependencies(arcbit-game arcbit-engine-shaders arcbit-engine-fonts arcbit-engine-skins arcbit-engine-locale-assets arcbit-engine-ui-assets)

# Windows: suppress the console window in Release builds.
if(WIN32)
    set_target_properties(arcbit-game PROPERTIES
        $<$<CONFIG:Release>:WIN32_EXECUTABLE TRUE>
    )
endif()
