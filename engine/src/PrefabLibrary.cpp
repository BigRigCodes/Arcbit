#include <arcbit/scene/PrefabLibrary.h>

#include <arcbit/core/Log.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneRegistry.h>

#include <format>
#include <fstream>
#include <ranges>
#include <set>

namespace Arcbit
{
namespace
{
using nlohmann::json;

constexpr std::string_view Extends = "extends";

// Field by field: objects merge, anything else is replaced.
void MergeFields(json& base, const json& over)
{
    for (const auto& [key, value] : over.items()) {
        if (value.is_object() && base.contains(key) && base[key].is_object()) MergeFields(base[key], value);
        else base[key] = value;
    }
}

// "<prefab>-<n>": the first n no Persistent in the scene uses.
std::string UnusedId(World& world, const std::string_view prefab)
{
    std::set<std::string> taken;
    world.Query<Persistent>().ForEach([&](const Entity, const Persistent& p) { taken.insert(p.Id); });
    const std::string base(prefab.substr(prefab.rfind('/') + 1)); // "npc/guard" -> "guard-1"
    std::string       id;
    for (u32 n = 1; taken.contains(id = std::format("{}-{}", base, n)); ++n) {}
    return id;
}
} // namespace

u32 PrefabLibrary::AddFolder(const std::filesystem::path& folder)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return 0;
    u32 count = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(folder, ec)) {
        if (!item.is_regular_file() || item.path().extension() != Extension) continue;
        const std::string id = std::filesystem::relative(item.path(), folder, ec).replace_extension().generic_string();
        std::ifstream     in(item.path());
        try {
            json prefab = json::parse(in, nullptr, true, true); // comments allowed
            if (!prefab.is_object()) throw std::runtime_error("a prefab must be a JSON object");
            _prefabs[id] = std::move(prefab);
            ++count;
        }
        catch (const std::exception& e) {
            LOG_ERROR(Engine, "Prefab '{}': {} - left out", item.path().string(), e.what());
        }
    }
    LOG_INFO(Engine, "Prefab: {} prefab(s) in '{}'", count, folder.string());
    return count;
}

void PrefabLibrary::Add(std::string id, json prefab) { _prefabs[std::move(id)] = std::move(prefab); }

std::optional<json> PrefabLibrary::Resolve(const std::string_view id) const
{
    std::vector<const json*> chain; // this prefab, then its parent, ...
    std::set<std::string>    seen;
    for (std::string next(id); !next.empty();) {
        const auto it = _prefabs.find(next);
        if (it == _prefabs.end()) {
            LOG_WARN(Engine, "Prefab '{}': there's no prefab '{}'", id, next);
            return std::nullopt;
        }
        if (!seen.insert(next).second) {
            LOG_WARN(Engine, "Prefab '{}': extends itself through '{}'", id, next);
            return std::nullopt;
        }
        chain.push_back(&it->second);
        next = it->second.value(std::string(Extends), std::string{});
    }
    json components = json::object();
    for (const json* prefab : chain | std::views::reverse) components = Merge(components, *prefab);
    components.erase(std::string(Extends));
    return components;
}

std::vector<std::string> PrefabLibrary::GetIds() const
{
    std::vector<std::string> ids;
    for (const auto& id : _prefabs | std::views::keys) ids.push_back(id);
    return ids;
}

json PrefabLibrary::Merge(const json& base, const json& over)
{
    json merged = base;
    for (const auto& [component, fields] : over.items()) {
        if (fields.is_null()) merged.erase(component);
        else if (fields.is_object() && merged.contains(component) && merged[component].is_object())
            MergeFields(merged[component], fields);
        else merged[component] = fields;
    }
    return merged;
}

Entity SpawnPrefab(Scene& scene, const PrefabLibrary& prefabs, const std::string_view id, const Vec2 position,
                   const SceneRegistry& registry, SceneAssets& assets)
{
    std::optional<json> components = prefabs.Resolve(id);
    if (!components) return {};
    (*components)["Transform2D"]["position"] = { position.X, position.Y };
    if (const auto p = components->find("Persistent"); p != components->end() && p->value("id", std::string{}).empty())
        (*p)["id"] = UnusedId(scene.GetWorld(), id);

    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    for (const auto& [name, fields] : components->items()) {
        if (const SceneRegistry::ComponentEntry* entry = registry.FindComponent(name)) entry->Read(world, e, fields, assets);
        else LOG_WARN(Engine, "Prefab '{}': unknown component '{}' - skipped", id, name);
    }
    if (const Collider2D* col = world.GetComponent<Collider2D>(e))
        (void)scene.GetPhysics().RegisterCollider(e, *col, world.GetTransform(e)->Position);
    return e;
}
} // namespace Arcbit
