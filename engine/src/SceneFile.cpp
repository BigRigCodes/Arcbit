#include <arcbit/scene/SceneFile.h>

#include <arcbit/core/Log.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/tilemap/TileMap.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <ranges>
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

    void LoadEntity(Scene& scene, const std::string& id, const json& components, const SceneRegistry& registry,
                    SceneAssets& assets, IdMap& ids)
    {
        const Entity e = scene.GetWorld().CreateEntity();
        ids[id]        = e;
        for (const auto& [name, fields] : components.items()) {
            const SceneRegistry::ComponentEntry* entry = registry.FindComponent(name);
            if (!entry) {
                LOG_WARN(Engine, "SceneFile: entity '{}' has unknown component '{}' - skipped", id, name);
                continue;
            }
            entry->Read(scene.GetWorld(), e, fields, assets);
        }
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
    // when taken ("rock", "rock-2").
    std::string MakeId(World& world, const Entity e, const json& taken)
    {
        std::string base = world.GetComponent<Tag>(e)->Value;
        std::ranges::transform(base, base.begin(), [](const unsigned char c) {
            return c == ' ' ? '-' : static_cast<char>(std::tolower(c));
        });
        std::string id = base;
        for (int n = 2; taken.contains(id); ++n) id = base + "-" + std::to_string(n);
        return id;
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
} // namespace

bool Load(Scene& scene, const json& doc, const SceneRegistry& registry, SceneAssets& assets, IdMap& ids)
{
    const int version = doc.value("version", 0);
    if (version < 1 || version > Version) {
        LOG_ERROR(Engine, "SceneFile: unsupported version {} (this build reads up to {})", version, Version);
        return false;
    }
    try {
        if (!LoadWorld(scene, doc, assets) || !InstallSystems(scene, doc, registry)) return false;
        for (const auto& [id, components] : doc.value("entities", json::object()).items())
            LoadEntity(scene, id, components, registry, assets, ids);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Engine, "SceneFile: malformed scene - {}", e.what());
        return false;
    }
    RegisterColliders(scene, ids);
    return true;
}

json Save(Scene& scene, const SceneRegistry& registry, const SceneAssets& assets, const json& base, const IdMap& ids)
{
    json doc = base.is_object() ? base : json::object();
    doc["version"]             = Version;
    doc["config"]["tile_size"] = scene.GetConfig().TileSize;
    doc["camera"]              = { { "zoom", scene.GetCamera().Zoom },
                                   { "position", { scene.GetCamera().Position.X, scene.GetCamera().Position.Y } } };

    std::unordered_map<u64, std::string> idOf;
    for (const auto& [id, e] : ids) idOf[(static_cast<u64>(e.Generation) << 32) | e.Index] = id;

    World& world    = scene.GetWorld();
    json   entities = json::object();
    for (const Entity e : AllEntities(world)) {
        const auto  known = idOf.find((static_cast<u64>(e.Generation) << 32) | e.Index);
        std::string id    = known != idOf.end() ? known->second : MakeId(world, e, entities);
        entities[id]      = SaveEntity(world, e, registry, assets);
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
