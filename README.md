# Arcbit

---
A specialized 2D tile based RPG game engine.

For a road map and current progress, see the [Blueprint](./BLUEPRINT.md). For how it's built, start with [docs/architecture.md](./docs/architecture.md); each system has its own page in [docs/](./docs).

---

## Development

This project is designed with cmake and vcpkg in mind. Tested via CLion on windows only at this stage.

It needs two things installed, found through environment variables:

- **vcpkg**: [CMakePresets](./CMakePresets.json) uses `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`. Set `VCPKG_ROOT` to your vcpkg checkout. [vcpkg.json](./vcpkg.json) pins the vcpkg baseline, so library versions match on every machine.
- **Vulkan SDK**: its installer sets `VULKAN_SDK`, which CMake's FindVulkan reads (headers, loader, and `glslc` for shaders).

For anything machine-specific beyond that, add presets of your own in a `CMakeUserPresets.json` (gitignored), inheriting from the project's.

vcpkg dependencies are built with the overlay triplet in [cmake/triplets](./cmake/triplets), which pins them to the Visual Studio 2022 (v143) toolset the project compiles with. Update it alongside any compiler upgrade.

### Tests

[CI](./.github/workflows/ci.yml) builds and runs them on every pull request into `master` and every push to it.

Unit tests live in [tests/](./tests) (Catch2 v3) and build as `arcbit-tests` alongside the game. They are headless — no window or GPU device. Run them from the build directory with `ctest --output-on-failure`, run `bin/arcbit-tests` directly, or use the IDE's test runner. Set `-DARCBIT_BUILD_TESTS=OFF` to skip them.

Keep `TEST_CASE` / `SECTION` names ASCII — CTest on Windows passes them back to the test runner as filters and mangles non-ASCII characters, so such a test reports as failed. Tests tagged `[shipped]` validate the real files under `engine/assets` and `game/assets` in place.

---

## Credits

Nothing could be possible without the following.

### Libraries

- SDL3
- Vulkan 1.3, Vulkan Memory Allocator
- spdlog
- nlohmann-json
- stb
- miniaudio
- Lua 5.4, sol2
- libsodium, zstd (sealed saves)
- Catch2 (tests)

### Fonts

- Roboto (Regular, Bold, Italic, Bold Italic) - Apache 2.0
- Noto Sans JP - SIL Open Font License

#### Documentation
- Ahem, thanks to AI for helping write all the comments documenting code...
- When this comment is removed, it will mean I have finished verifying accuracy of the comments/documentation.