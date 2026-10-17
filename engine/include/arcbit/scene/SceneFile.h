#pragma once

#include <arcbit/ecs/Entity.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Arcbit
{
class Scene;
class SceneAssets;
class SceneRegistry;

// ---------------------------------------------------------------------------
// SceneFile - .arcscene documents to and from a Scene (format: docs/scenes.md).
//
// These work on parsed JSON and a SceneAssets, so they run without a render
// device; FileScene wraps them with file I/O and the application's assets.
// ---------------------------------------------------------------------------
namespace SceneFile
{
    // The format version this build reads and writes. Newer files are rejected.
    constexpr int Version = 1;

    // File entity id -> runtime entity.
    using IdMap = std::unordered_map<std::string, Entity>;

    // Build `doc` into an empty scene: config, camera, tilemap, tile
    // overrides, physics settings, named systems, then the entities (and
    // their colliders, registered once all components are in place). Fills
    // `ids`. Returns false and logs on the first error - unsupported
    // version, unknown system, malformed component - leaving the scene
    // partly built. Unknown component names are skipped with a warning.
    [[nodiscard]] bool Load(Scene& scene, const nlohmann::json& doc, const SceneRegistry& registry,
                            SceneAssets& assets, IdMap& ids);

    // The document for the scene's current state. Keys the scene doesn't own
    // (tilemap, tile_overrides, systems, render, physics) are copied from
    // `base`; config and camera come from the scene. Each entity is written
    // under its id in `ids`, or one made from its Tag ("rock", "rock-2").
    [[nodiscard]] nlohmann::json Save(Scene& scene, const SceneRegistry& registry, const SceneAssets& assets,
                                      const nlohmann::json& base, const IdMap& ids);

    // Parse a file; nullopt (logged) when it can't be read or parsed.
    [[nodiscard]] std::optional<nlohmann::json> ReadFile(std::string_view path);
    [[nodiscard]] bool                          WriteFile(std::string_view path, const nlohmann::json& doc);
} // namespace SceneFile
} // namespace Arcbit
