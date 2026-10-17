#pragma once

#include <arcbit/tilemap/TileMap.h>

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace Arcbit
{
class Scene;
class SceneAssets;
class SceneRegistry;

// ---------------------------------------------------------------------------
// Scene state: what the player changed in a scene, against how it was
// authored - the "save the changes, not the world" half of docs/saves.md.
// Only Persistent entities are tracked; everything else comes back as
// authored, so content updates still reach old saves.
//
// The cycle, run by SceneManager:
//   OnLoad          the scene builds itself as authored
//   Snapshot        remember every persistent entity's components
//   Apply           put the saved changes back on top (and track tiles)
//   ... play ...
//   Capture         diff against the snapshot - kept when the scene unloads
// ---------------------------------------------------------------------------

// How a scene's persistent entities were authored: id -> component name ->
// every field (SceneRegistry::WriteAll). Taken right after OnLoad.
struct AuthoredState
{
    std::map<std::string, std::map<std::string, nlohmann::json>> Entities;
};

// One scene's changes.
struct SceneState
{
    // Authored entities that changed: id -> component -> the fields that
    // differ from authored (a component added at runtime: all its fields;
    // removed: null). A field the player never changed isn't stored, so a
    // content update to it still shows.
    nlohmann::json Entities = nlohmann::json::object();

    // Authored entities that are gone (an emptied chest, a beaten boss).
    std::vector<std::string> Destroyed;

    // Persistent entities created at runtime (a dropped item): id -> every
    // component, rebuilt through the registry.
    nlohmann::json Spawned = nlohmann::json::object();

    // Tiles set since the scene loaded.
    std::vector<TileMap::TileChange> Tiles;

    [[nodiscard]] bool Empty() const { return Entities.empty() && Destroyed.empty() && Spawned.empty() && Tiles.empty(); }

    [[nodiscard]] nlohmann::json ToJson() const;
    [[nodiscard]] static SceneState FromJson(const nlohmann::json& json);

    bool operator==(const SceneState& other) const = default;
};

// Every visited scene's changes, by scene name - kept between scenes (a
// scene's state outlives it) and saved as the "world" save section.
struct WorldState
{
    std::map<std::string, SceneState> Scenes;

    [[nodiscard]] nlohmann::json ToJson() const;
    void                         FromJson(const nlohmann::json& json);
};

namespace SceneStates
{
    // The scene's persistent entities as they are now - call right after
    // OnLoad, before Apply.
    [[nodiscard]] AuthoredState Snapshot(Scene& scene, const SceneRegistry& registry, const SceneAssets& assets);

    // What changed since `authored`.
    [[nodiscard]] SceneState Capture(Scene& scene, const AuthoredState& authored, const SceneRegistry& registry,
                                     const SceneAssets& assets);

    // Put `state` back on a scene loaded as authored: destroy what was
    // destroyed, set changed fields, add / remove components, rebuild spawned
    // entities, set tiles; keeps the physics broadphase in step. Unknown ids
    // (content that no longer exists) are skipped.
    void Apply(Scene& scene, const SceneState& state, const SceneRegistry& registry, SceneAssets& assets);
} // namespace SceneStates
} // namespace Arcbit
