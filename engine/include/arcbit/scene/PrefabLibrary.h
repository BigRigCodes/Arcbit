#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/ecs/Entity.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Scene;
class SceneAssets;
class SceneRegistry;

// ---------------------------------------------------------------------------
// PrefabLibrary - every .arcprefab in a folder and below, by id: its path
// there without the extension ("chest", "npc/guard"). Shared by every scene
// (docs/scenes.md#prefabs).
//
// A prefab is an entity's components in their scene-file form, and may
// build on another:
//
//   { "extends": "npc",
//     "SpriteRenderer": { "tint": [0.7, 0.8, 1, 1] },
//     "Script": { "script": "demo.guard" } }
//
// Its own fields go over the one it extends, field by field; a component
// set to null drops the parent's.
// ---------------------------------------------------------------------------
class PrefabLibrary
{
public:
    static constexpr std::string_view Extension = ".arcprefab";

    // Returns how many prefabs were read (a missing folder is fine: 0).
    // A file that doesn't parse is logged and left out.
    u32 AddFolder(const std::filesystem::path& folder);

    // A prefab from code (tests, a game's generated ones).
    void Add(std::string id, nlohmann::json prefab);

    // The prefab's components, every "extends" merged in; nullopt (logged)
    // for an unknown id, a missing parent or a loop.
    [[nodiscard]] std::optional<nlohmann::json> Resolve(std::string_view id) const;

    [[nodiscard]] bool                     Has(std::string_view id) const { return _prefabs.contains(id); }
    [[nodiscard]] std::vector<std::string> GetIds() const; // sorted

    // Components over a base, field by field: `over`'s fields win, a
    // component set to null is dropped.
    [[nodiscard]] static nlohmann::json Merge(const nlohmann::json& base, const nlohmann::json& over);

private:
    std::map<std::string, nlohmann::json, std::less<>> _prefabs; // as written, "extends" included
};

// A new entity from a prefab, at `position`, its colliders registered - a
// one-time stamp: from then on it's an ordinary entity. A prefab with a
// Persistent and no id gets "<prefab>-<n>", unused in the scene, so it
// saves as a spawned entity. An invalid Entity (logged) if the prefab can't
// be resolved.
[[nodiscard]] Entity SpawnPrefab(Scene& scene, const PrefabLibrary& prefabs, std::string_view id, Vec2 position,
                                 const SceneRegistry& registry, SceneAssets& assets);
} // namespace Arcbit
