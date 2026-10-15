# Arcbit

---
A specialized 2D tile based RPG game engine.

For a road map and current progress, see the [Blueprint](./BLUEPRINT.md).

---

## Development

This project is designed with cmake and vcpkg in mind. Tested via CLion on windows only at this stage.
Currently, the [CMakePresets](./CMakePresets.json) file has the path to my installed vcpkg hardcoded, as with the Vulkan SDK. Bare that in mind if you clone this repo.

vcpkg dependencies are built with the overlay triplet in [cmake/triplets](./cmake/triplets), which pins them to the Visual Studio 2022 (v143) toolset the project compiles with. Update it alongside any compiler upgrade.

### Tests

Unit tests live in [tests/](./tests) (Catch2 v3) and build as `arcbit-tests` alongside the game. They are headless — no window or GPU device. Run them from the build directory with `ctest --output-on-failure`, run `bin/arcbit-tests` directly, or use the IDE's test runner. Set `-DARCBIT_BUILD_TESTS=OFF` to skip them.

---

## Credits

Nothing could be possible without the following.

### Libraries

- SDL3
- Vulkan 1.3
- spdlog
- nlohmann-json
- stb
- miniaudio
- Catch2 (tests)

#### Documentation
- Ahem, thanks to AI for helping write all the comments documenting code...
- When this comment is removed, it will mean I have finished verifying accuracy of the comments/documentation.