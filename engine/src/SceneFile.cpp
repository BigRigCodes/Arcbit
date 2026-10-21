#include <arcbit/scene/SceneFile.h>

#include <arcbit/core/Log.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/PrefabLibrary.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/tilemap/TileMap.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Arcbit::SceneFile
{
namespace
{
    using nlohmann::json;

    Vec2 ReadVec2(const json& j, const Vec2 fallback)
    {
        return j.is_array() && j.size() == 2 ? Vec2{ j[0].get<f32>(), j[1].get<f32>() } : fallback;
    }

    void LoadConfigAndCamera(Scene& scene, const json& doc)
    {
        if (const auto config = doc.find("config"); config != doc.end() && config->contains("tile_size")) {
            const f32 tileSize         = config->at("tile_size").get<f32>();
            scene.GetConfig().TileSize = tileSize;
            scene.GetTileMap().SetTileSize(tileSize);
        }
        if (const auto camera = doc.find("camera"); camera != doc.end()) {
            scene.GetCamera().Zoom     = camera->value("zoom", scene.GetCamera().Zoom);
            scene.GetCamera().Position = ReadVec2(camera->value("position", json()), scene.GetCamera().Position);
        }
    }

    // Tile defs the scene sets on top of what the map's atlases define.
    void ApplyTileOverrides(TileMap& tilemap, const json& overrides)
    {
        for (const json& entry : overrides)
            tilemap.RegisterTile(entry.at("id").get<u32>(), TileMap::ParseTileDef(entry, 0, 0));
    }

    bool LoadWorld(Scene& scene, const json& doc, SceneAssets& assets)
    {
        LoadConfigAndCamera(scene, doc);
        if (const auto path = doc.find("tilemap"); path != doc.end()) {
            if (!assets.LoadTileMap(scene.GetTileMap(), path->get<std::string>())) {
                LOG_ERROR(Engine, "SceneFile: failed to load tilemap '{}'", path->get<std::string>());
                return false;
            }
        }
        ApplyTileOverrides(scene.GetTileMap(), doc.value("tile_overrides", json::array()));
        if (const auto physics = doc.find("physics"); physics != doc.end())
            scene.GetPhysics().SetDebugDraw(physics->value("debug_draw", false));
        return true;
    }

    bool InstallSystems(Scene& scene, const json& doc, const SceneRegistry& registry)
    {
        for (const json& name : doc.value("systems", json::array())) {
            const SceneRegistry::InstallFn* install = registry.FindSystem(name.get<std::string>());
            if (!install) {
                LOG_ERROR(Engine, "SceneFile: no system registered as '{}'", name.get<std::string>());
                return false;
            }
            (*install)(scene);
        }
        return true;
    }

    // Fields onto the entity's components (adding them); null removes one.
    void ReadComponents(World& world, const Entity e, const std::string& id, const json& components,
                        const SceneRegistry& registry, SceneAssets& assets)
    {
        for (const auto& [name, fields] : components.items()) {
            const SceneRegistry::ComponentEntry* entry = registry.FindComponent(name);
            if (!entry) {
                LOG_WARN(Engine, "SceneFile: entity '{}' has unknown component '{}' - skipped", id, name);
                continue;
            }
            if (fields.is_null()) entry->Remove(world, e);
            else entry->Read(world, e, fields, assets);
        }
    }

    // Every component the entity has, every field (what a prefab copy diffs against).
    json WriteAllComponents(World& world, const Entity e, const SceneRegistry& registry, const SceneAssets& assets)
    {
        json all = json::object();
        for (const SceneRegistry::ComponentEntry& entry : registry.Components())
            if (entry.Has(world, e)) all[entry.Name] = entry.WriteAll(world, e, assets);
        return all;
    }

    // A prefab copy: the prefab first, remembered, then the entity's own fields.
    void LoadEntity(Scene& scene, const std::string& id, const json& components, const SceneRegistry& registry,
                    SceneAssets& assets, IdMap& ids, const PrefabLibrary* prefabs, PrefabLinks* links)
    {
        World&       world = scene.GetWorld();
        const Entity e     = world.CreateEntity();
        ids[id]            = e;
        json own           = components;
        if (const auto prefab = components.find("prefab"); prefab != components.end()) {
            own.erase("prefab");
            const std::string name = prefab->get<std::string>();
            const auto        base = prefabs ? prefabs->Resolve(name) : std::nullopt;
            if (!prefabs) LOG_WARN(Engine, "SceneFile: entity '{}' uses prefab '{}', but there are no prefabs", id, name);
            if (base) {
                ReadComponents(world, e, id, *base, registry, assets);
                if (links) (*links)[id] = { name, WriteAllComponents(world, e, registry, assets) };
                for (auto& [component, fields] : own.items()) // nested fields merge as a prefab's do ("properties")
                    if (fields.is_object() && base->contains(component))
                        fields = PrefabLibrary::Merge({ { component, (*base)[component] } }, { { component, fields } })[component];
            }
        }
        ReadComponents(world, e, id, own, registry, assets);
    }

    // persist_all: every entity without a Persistent gets one; a Persistent
    // without an id takes the entity's file id (opted-out ones keep theirs).
    void ApplyPersistence(Scene& scene, const json& doc, const IdMap& ids)
    {
        World&     world = scene.GetWorld();
        const bool all   = doc.value("persist_all", false);
        for (const auto& [id, e] : ids) {
            if (Persistent* p = world.GetComponent<Persistent>(e)) {
                if (p->Id.empty()) p->Id = id;
            }
            else if (all) {
                world.AddComponent<Persistent>(e, Persistent{ .Id = id });
            }
        }
    }

    // The reverse for saving: a Persistent's id is implied by the file id,
    // and under persist_all a plain enabled one is implied entirely - so a
    // saved file stays as it was authored.
    void DropImpliedPersistence(json& components, const std::string& id, const bool persistAll)
    {
        const auto it = components.find("Persistent");
        if (it == components.end()) return;
        if (it->value("id", std::string{}) == id) it->erase("id");
        if (persistAll && it->empty()) components.erase(it);
    }

    void RegisterColliders(Scene& scene, const IdMap& ids)
    {
        World& world = scene.GetWorld();
        for (const Entity e : ids | std::views::values)
            if (const Collider2D* col = world.GetComponent<Collider2D>(e))
                (void)scene.GetPhysics().RegisterCollider(e, *col, world.GetTransform(e)->Position);
    }

    // ---- Save ---------------------------------------------------------------

    // Components store f32; JSON numbers are doubles, so 44.8f would print
    // as 44.79999923706055. Rewrite every float as the double nearest its
    // shortest f32 spelling, so saved files read like hand-written ones.
    void TidyFloats(json& j)
    {
        if (j.is_number_float()) {
            j = std::stod(std::format("{}", static_cast<f32>(j.get<f64>())));
            return;
        }
        if (j.is_structured())
            for (json& child : j) TidyFloats(child);
    }

    std::vector<Entity> AllEntities(World& world)
    {
        std::vector<Entity> all;
        world.Query<Transform2D>().ForEach([&](const Entity e, Transform2D&) { all.push_back(e); });
        std::ranges::sort(all, {}, &Entity::Index); // stable output order
        return all;
    }

    // An id from the entity's Tag: lowercase, spaces to dashes, numbered
    // when taken ("rock", "rock-2"). The new id is added to `taken`.
    std::string MakeId(World& world, const Entity e, std::unordered_set<std::string>& taken)
    {
        std::string base = world.GetComponent<Tag>(e)->Value;
        std::ranges::transform(base, base.begin(), [](const unsigned char c) {
            return c == ' ' ? '-' : static_cast<char>(std::tolower(c));
        });
        std::string id = base;
        for (int n = 2; taken.contains(id); ++n) id = base + "-" + std::to_string(n);
        taken.insert(id);
        return id;
    }

    // The file id for every live entity: the one it was loaded with, else a
    // new one from its Tag. Kept ids are reserved first, so a new entity can
    // never take an id an existing one still uses (and overwrite it on save).
    std::vector<std::pair<Entity, std::string>> AssignIds(World& world, const IdMap& ids)
    {
        const auto key = [](const Entity e) { return (static_cast<u64>(e.Generation) << 32) | e.Index; };
        std::unordered_map<u64, std::string> idOf;
        for (const auto& [id, e] : ids) idOf[key(e)] = id;

        const std::vector<Entity>       live = AllEntities(world);
        std::unordered_set<std::string> taken;
        for (const Entity e : live)
            if (const auto known = idOf.find(key(e)); known != idOf.end()) taken.insert(known->second);

        std::vector<std::pair<Entity, std::string>> assigned;
        for (const Entity e : live) {
            const auto known = idOf.find(key(e));
            assigned.emplace_back(e, known != idOf.end() ? known->second : MakeId(world, e, taken));
        }
        return assigned;
    }

    json SaveEntity(World& world, const Entity e, const SceneRegistry& registry, const SceneAssets& assets)
    {
        json components = json::object();
        for (const SceneRegistry::ComponentEntry& entry : registry.Components()) {
            if (!entry.Has(world, e)) continue;
            json fields = entry.Write(world, e, assets);
            // Every entity has these two; leave them out when they're all defaults.
            if (fields.empty() && (entry.Name == "Transform2D" || entry.Name == "Tag")) continue;
            components[entry.Name] = std::move(fields);
        }
        return components;
    }

    // A prefab copy as written: its prefab, and only the fields that differ
    // from it (a component it added: its non-default fields; one it removed: null).
    json SavePrefabCopy(World& world, const Entity e, const SceneRegistry& registry, const SceneAssets& assets,
                        const PrefabLink& link)
    {
        json out = { { "prefab", link.Prefab } };
        for (const SceneRegistry::ComponentEntry& entry : registry.Components()) {
            const bool has  = entry.Has(world, e);
            const auto base = link.Base.find(entry.Name);
            if (!has) {
                if (base != link.Base.end()) out[entry.Name] = nullptr;
                continue;
            }
            if (base == link.Base.end()) {
                out[entry.Name] = entry.Write(world, e, assets);
                continue;
            }
            json changed = json::object();
            for (const auto& [field, value] : entry.WriteAll(world, e, assets).items())
                if (!base->contains(field) || (*base)[field] != value) changed[field] = value;
            if (!changed.empty()) out[entry.Name] = std::move(changed);
        }
        return out;
    }
} // namespace

bool Load(Scene& scene, const json& doc, const SceneRegistry& registry, SceneAssets& assets, IdMap& ids,
          const PrefabLibrary* prefabs, PrefabLinks* links)
{
    try {
        if (!doc.is_object()) {
            LOG_ERROR(Engine, "SceneFile: a scene file must be a JSON object");
            return false;
        }
        const int version = doc.value("version", 0);
        if (version < 1 || version > Version) {
            LOG_ERROR(Engine, "SceneFile: unsupported version {} (this build reads up to {})", version, Version);
            return false;
        }
        if (!LoadWorld(scene, doc, assets) || !InstallSystems(scene, doc, registry)) return false;
        for (const auto& [id, components] : doc.value("entities", json::object()).items())
            LoadEntity(scene, id, components, registry, assets, ids, prefabs, links);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Engine, "SceneFile: malformed scene - {}", e.what());
        return false;
    }
    ApplyPersistence(scene, doc, ids);
    RegisterColliders(scene, ids);
    return true;
}

json Save(Scene& scene, const SceneRegistry& registry, const SceneAssets& assets, const json& base, const IdMap& ids,
          const PrefabLinks* links)
{
    json doc = base.is_object() ? base : json::object();
    doc["version"]             = Version;
    doc["config"]["tile_size"] = scene.GetConfig().TileSize;
    doc["camera"]              = { { "zoom", scene.GetCamera().Zoom },
                                   { "position", { scene.GetCamera().Position.X, scene.GetCamera().Position.Y } } };

    World& world    = scene.GetWorld();
    json   entities = json::object();
    const bool persistAll = doc.value("persist_all", false);
    for (const auto& [e, id] : AssignIds(world, ids)) {
        const PrefabLink* link = nullptr;
        if (links)
            if (const auto it = links->find(id); it != links->end()) link = &it->second;
        json components = link ? SavePrefabCopy(world, e, registry, assets, *link) : SaveEntity(world, e, registry, assets);
        DropImpliedPersistence(components, id, persistAll);
        if (link && components.contains("Persistent") && components["Persistent"].empty() && link->Base.contains("Persistent"))
            components.erase("Persistent"); // only its implied id differed from the prefab's
        entities[id] = std::move(components);
    }
    doc["entities"] = std::move(entities);
    TidyFloats(doc);
    return doc;
}

std::optional<json> ReadFile(const std::string_view path)
{
    std::ifstream f{ std::string(path) };
    if (!f) {
        LOG_ERROR(Engine, "SceneFile: cannot open '{}'", path);
        return std::nullopt;
    }
    try {
        return json::parse(f);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Engine, "SceneFile: '{}' is not valid JSON - {}", path, e.what());
        return std::nullopt;
    }
}

bool WriteFile(const std::string_view path, const json& doc)
{
    std::ofstream f{ std::string(path) };
    if (!f) {
        LOG_ERROR(Engine, "SceneFile: cannot write '{}'", path);
        return false;
    }
    f << doc.dump(4) << '\n';
    return static_cast<bool>(f);
}
} // namespace Arcbit::SceneFile
