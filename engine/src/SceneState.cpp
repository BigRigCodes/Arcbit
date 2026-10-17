#include <arcbit/save/SceneState.h>

#include <arcbit/core/Log.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneRegistry.h>

#include <algorithm>
#include <ranges>
#include <set>

namespace Arcbit
{
namespace
{
    using json = nlohmann::json;

    constexpr std::string_view PersistentName = "Persistent";

    // The scene's live, enabled persistent entities by id (the first of a
    // duplicated id wins, logged).
    std::map<std::string, Entity> LivePersistent(Scene& scene)
    {
        std::map<std::string, Entity> live;
        scene.GetWorld().Query<Persistent>().ForEach([&](const Entity e, const Persistent& p) {
            if (!p.Enabled || p.Id.empty()) return;
            if (!live.emplace(p.Id, e).second)
                LOG_WARN(Engine, "SceneState: two persistent entities share the id '{}' in '{}'", p.Id, scene.GetName());
        });
        return live;
    }

    // The components of `e` that persist: its Persistent's list, or every
    // registered one it has (Persistent itself is the identity, not state).
    std::vector<const SceneRegistry::ComponentEntry*> Kept(World& world, const Entity e, const SceneRegistry& registry)
    {
        const std::vector<std::string>& only = world.GetComponent<Persistent>(e)->Components;
        std::vector<const SceneRegistry::ComponentEntry*> kept;
        for (const SceneRegistry::ComponentEntry& entry : registry.Components()) {
            if (entry.Name == PersistentName) continue;
            if (!only.empty() && std::ranges::find(only, entry.Name) == only.end()) continue;
            kept.push_back(&entry);
        }
        return kept;
    }

    // The fields of `now` that differ from `before` (same keys: WriteAll).
    json ChangedFields(const json& now, const json& before)
    {
        json changed = json::object();
        for (const auto& [key, value] : now.items())
            if (!before.contains(key) || before[key] != value) changed[key] = value;
        return changed;
    }

    json CaptureEntity(World& world, const Entity e, const std::map<std::string, json>& authored,
                       const SceneRegistry& registry, const SceneAssets& assets)
    {
        json changes = json::object();
        for (const SceneRegistry::ComponentEntry* entry : Kept(world, e, registry)) {
            const auto before = authored.find(entry->Name);
            if (!entry->Has(world, e)) {
                if (before != authored.end()) changes[entry->Name] = nullptr; // removed
                continue;
            }
            const json now = entry->WriteAll(world, e, assets);
            if (before == authored.end()) changes[entry->Name] = now; // added
            else if (json diff = ChangedFields(now, before->second); !diff.empty()) changes[entry->Name] = std::move(diff);
        }
        return changes;
    }

    // Every registered component, Persistent included - enough to rebuild it.
    json CaptureWhole(World& world, const Entity e, const SceneRegistry& registry, const SceneAssets& assets)
    {
        json components = json::object();
        for (const SceneRegistry::ComponentEntry& entry : registry.Components())
            if (entry.Has(world, e)) components[entry.Name] = entry.WriteAll(world, e, assets);
        return components;
    }

    void ReadComponents(World& world, const Entity e, const json& components, const SceneRegistry& registry,
                        SceneAssets& assets)
    {
        for (const auto& [name, fields] : components.items()) {
            const SceneRegistry::ComponentEntry* entry = registry.FindComponent(name);
            if (!entry) {
                LOG_WARN(Engine, "SceneState: unknown component '{}' - skipped", name);
                continue;
            }
            if (fields.is_null()) entry->Remove(world, e);
            else entry->Read(world, e, fields, assets);
        }
    }

    // A changed or rebuilt entity's collider follows its new transform.
    void SyncCollider(Scene& scene, const Entity e)
    {
        World&            world = scene.GetWorld();
        const Collider2D* col   = world.GetComponent<Collider2D>(e);
        if (!col) return;
        PhysicsWorld&                  physics  = scene.GetPhysics();
        const Vec2                     position = world.GetTransform(e)->Position;
        const PhysicsWorld::ColliderId id       = physics.FindColliderForEntity(e);
        if (id != PhysicsWorld::InvalidId) physics.UpdateCollider(id, *col, position);
        else (void)physics.RegisterCollider(e, *col, position);
    }
} // namespace

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

json SceneState::ToJson() const
{
    json tiles = json::array();
    for (const TileMap::TileChange& t : Tiles) tiles.push_back({ t.X, t.Y, t.Layer, t.Id });
    return { { "entities", Entities }, { "destroyed", Destroyed }, { "spawned", Spawned }, { "tiles", tiles } };
}

SceneState SceneState::FromJson(const json& doc)
{
    SceneState state;
    if (!doc.is_object()) return state;
    state.Entities  = doc.value("entities", json::object());
    state.Destroyed = doc.value("destroyed", std::vector<std::string>{});
    state.Spawned   = doc.value("spawned", json::object());
    for (const auto& t : doc.value("tiles", json::array()))
        if (t.is_array() && t.size() == 4)
            state.Tiles.push_back({ t[0].get<i32>(), t[1].get<i32>(), t[2].get<u32>(), t[3].get<u32>() });
    return state;
}

json WorldState::ToJson() const
{
    json scenes = json::object();
    for (const auto& [name, state] : Scenes)
        if (!state.Empty()) scenes[name] = state.ToJson();
    return { { "scenes", scenes } };
}

void WorldState::FromJson(const json& doc)
{
    Scenes.clear();
    if (!doc.is_object()) return;
    for (const auto& [name, state] : doc.value("scenes", json::object()).items()) Scenes[name] = SceneState::FromJson(state);
}

// ---------------------------------------------------------------------------
// Snapshot / Capture / Apply
// ---------------------------------------------------------------------------

AuthoredState SceneStates::Snapshot(Scene& scene, const SceneRegistry& registry, const SceneAssets& assets)
{
    AuthoredState authored;
    World&        world = scene.GetWorld();
    for (const auto& [id, e] : LivePersistent(scene)) {
        std::map<std::string, json>& components = authored.Entities[id];
        for (const SceneRegistry::ComponentEntry* entry : Kept(world, e, registry))
            if (entry->Has(world, e)) components[entry->Name] = entry->WriteAll(world, e, assets);
    }
    return authored;
}

SceneState SceneStates::Capture(Scene& scene, const AuthoredState& authored, const SceneRegistry& registry,
                                const SceneAssets& assets)
{
    SceneState                          state;
    World&                              world = scene.GetWorld();
    const std::map<std::string, Entity> live  = LivePersistent(scene);

    for (const auto& [id, components] : authored.Entities) {
        const auto it = live.find(id);
        if (it == live.end()) {
            state.Destroyed.push_back(id);
            continue;
        }
        if (json changes = CaptureEntity(world, it->second, components, registry, assets); !changes.empty())
            state.Entities[id] = std::move(changes);
    }
    for (const auto& [id, e] : live)
        if (!authored.Entities.contains(id)) state.Spawned[id] = CaptureWhole(world, e, registry, assets);

    state.Tiles = scene.GetTileMap().GetChanges();
    return state;
}

void SceneStates::Apply(Scene& scene, const SceneState& state, const SceneRegistry& registry, SceneAssets& assets)
{
    World&                              world = scene.GetWorld();
    const std::map<std::string, Entity> live  = LivePersistent(scene);

    for (const std::string& id : state.Destroyed)
        if (const auto it = live.find(id); it != live.end()) world.DestroyEntity(it->second);

    for (const auto& [id, components] : state.Entities.items()) {
        const auto it = live.find(id);
        if (it == live.end()) continue; // no longer in the content
        ReadComponents(world, it->second, components, registry, assets);
        SyncCollider(scene, it->second);
    }
    for (const auto& [id, components] : state.Spawned.items()) {
        if (live.contains(id)) {
            LOG_WARN(Engine, "SceneState: '{}' was spawned, but the scene now authors it - kept as authored", id);
            continue;
        }
        const Entity e = world.CreateEntity();
        ReadComponents(world, e, components, registry, assets);
        SyncCollider(scene, e);
    }
    for (const TileMap::TileChange& t : state.Tiles) scene.GetTileMap().SetTile(t.X, t.Y, t.Layer, t.Id);
}
} // namespace Arcbit
