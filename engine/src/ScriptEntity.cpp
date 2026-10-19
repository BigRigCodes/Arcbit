// The Entity API: handles, finding and creating entities, and components by
// name through the SceneRegistry - so a game's own components work in Lua
// with no extra binding.

#include "ScriptState.h"

#include <arcbit/core/Log.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/FileScene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>

#include <format>
#include <optional>

namespace Arcbit
{
namespace
{
    u64 EntityKey(const Entity e) { return (static_cast<u64>(e.Index) << 32) | e.Generation; }

    // A moved or reshaped entity's collider follows it into the broadphase.
    void SyncCollider(Scene& scene, const Entity e)
    {
        World&            world = scene.GetWorld();
        const Collider2D* col   = world.GetComponent<Collider2D>(e);
        if (!col) return;
        PhysicsWorld&                  physics = scene.GetPhysics();
        const PhysicsWorld::ColliderId id      = physics.FindColliderForEntity(e);
        if (id != PhysicsWorld::InvalidId) physics.UpdateCollider(id, *col, world.GetTransform(e)->Position);
        else (void)physics.RegisterCollider(e, *col, world.GetTransform(e)->Position);
    }

    // Creating, destroying, adding and removing reshape the World: not while
    // a system is looping over it.
    void RequireReshapable(const World& world)
    {
        if (world.IsIterating())
            throw std::runtime_error("entities can't be created, destroyed or given / lose components here "
                                     "(inside a system's loop)");
    }
} // namespace

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

Scene* ScriptEngine::State::Resolve(const ScriptEntity& handle) const
{
    const auto it = Scenes.find(handle.SceneSerial);
    if (it == Scenes.end()) return nullptr; // its scene unloaded
    return it->second->GetWorld().IsValid(handle.Id) ? it->second : nullptr;
}

Scene* ScriptEngine::State::ContextScene() const
{
    if (Current.Scene) return Current.Scene;
    return Services.Scenes ? Services.Scenes->GetActive() : nullptr;
}

Scene& ScriptEngine::State::RequireScene() const
{
    Scene* scene = ContextScene();
    if (!scene) throw std::runtime_error("no scene is running");
    return *scene;
}

u64 ScriptEngine::State::SerialOf(const Scene* scene) const
{
    if (scene && scene == Current.Scene) return Current.Serial;
    for (const auto& [serial, running] : Scenes)
        if (running == scene) return serial;
    return 0;
}

// ---------------------------------------------------------------------------
// Entity
// ---------------------------------------------------------------------------

// A stale handle (entity or scene gone) does nothing: reads give nil.
void ScriptEngine::State::BindEntity()
{
    sol::usertype<ScriptEntity> type = Lua.new_usertype<ScriptEntity>(
        "Entity", sol::no_constructor,
        "IsValid", [this](const ScriptEntity& e) { return Resolve(e) != nullptr; },
        // Held in a conversation (or by anything else): stand still.
        "IsHeld", [this](const ScriptEntity& e) {
            Scene* scene = Resolve(e);
            return scene && IsHeld(scene->GetWorld(), e.Id);
        },
        sol::meta_function::equal_to,
        [](const ScriptEntity& a, const ScriptEntity& b) { return a.Id == b.Id && a.SceneSerial == b.SceneSerial; },
        sol::meta_function::to_string, [this](const ScriptEntity& e) {
            return std::format("Entity({}:{}{})", e.Id.Index, e.Id.Generation, Resolve(e) ? "" : ", gone");
        });
    BindEntityStatics(type);
    BindEntityComponents(type);
    AddGlobal("Entity", Lua["Entity"]);
}

// Entity.Find / FindAll / With / Create, in the context scene.
void ScriptEngine::State::BindEntityStatics(sol::usertype<ScriptEntity>& type)
{
    // A scene file's entity id, else a Persistent id, else a Name.
    type["Find"] = [this](const std::string& id) -> std::optional<ScriptEntity> {
        Scene& scene = RequireScene();
        if (const auto* file = dynamic_cast<FileScene*>(&scene))
            if (const Entity e = file->FindEntity(id); e.IsValid()) return Handle(scene, e);
        std::optional<ScriptEntity> found;
        scene.GetWorld().Query<Persistent>().ForEach([&](const Entity e, const Persistent& p) {
            if (!found && p.Id == id) found = Handle(scene, e);
        });
        if (!found)
            scene.GetWorld().Query<Name>().ForEach([&](const Entity e, const Name& n) {
                if (!found && n.Value == id) found = Handle(scene, e);
            });
        return found;
    };
    type["FindAll"] = [this](const std::string& tag) {
        Scene&                    scene = RequireScene();
        std::vector<ScriptEntity> found;
        scene.GetWorld().Query<Tag>().ForEach([&](const Entity e, const Tag& t) {
            if (t.Value == tag) found.push_back(Handle(scene, e));
        });
        return sol::as_table(found);
    };
    // Every entity with all the named components: Entity.With("Chest", "Collider2D").
    type["With"] = [this](const sol::variadic_args names) {
        Scene&                                            scene = RequireScene();
        std::vector<const SceneRegistry::ComponentEntry*> entries;
        for (const std::string name : names) {
            const SceneRegistry::ComponentEntry* entry = Services.Registry ? Services.Registry->FindComponent(name) : nullptr;
            if (!entry) throw std::runtime_error(std::format("Entity.With: no component '{}'", name));
            entries.push_back(entry);
        }
        std::vector<Entity> all;
        scene.GetWorld().Query<Transform2D>().ForEach([&](const Entity e, const Transform2D&) { all.push_back(e); });
        std::vector<ScriptEntity> found;
        for (const Entity e : all)
            if (std::ranges::all_of(entries, [&](const auto* entry) { return entry->Has(scene.GetWorld(), e); }))
                found.push_back(Handle(scene, e));
        return sol::as_table(found);
    };
    // Entity.Create({ Transform2D = { position = { 0, 0 } }, Tag = { value = "Drop" } }).
    type["Create"] = [this](const sol::optional<sol::table> components) {
        Scene& scene = RequireScene();
        RequireReshapable(scene.GetWorld());
        if (!Services.Registry || !Services.Assets) throw std::runtime_error("Entity.Create: no scene registry");
        const Entity                       e      = scene.GetWorld().CreateEntity();
        const std::unique_ptr<SceneAssets> assets = Services.Assets(scene);
        if (components)
            for (const auto& [key, fields] : *components) {
                const std::string                    name  = key.as<std::string>();
                const SceneRegistry::ComponentEntry* entry = Services.Registry->FindComponent(name);
                if (!entry) throw std::runtime_error(std::format("Entity.Create: no component '{}'", name));
                entry->Read(scene.GetWorld(), e, LuaToJson(fields), *assets);
            }
        SyncCollider(scene, e);
        return Handle(scene, e);
    };
}

// e:Get / Set / Has / Add / Remove by component name, e:Destroy, and the
// properties scripts use most: Position, Tag, Name.
void ScriptEngine::State::BindEntityComponents(sol::usertype<ScriptEntity>& type)
{
    // The registry entry for `name`, the scene and its assets - or nothing
    // for a stale handle (logged).
    struct Target
    {
        Scene*                               Scene;
        const SceneRegistry::ComponentEntry* Entry;
        std::unique_ptr<SceneAssets>         Assets;
    };
    const auto target = [this](const ScriptEntity& e, const std::string& name, const char* call) -> std::optional<Target> {
        Scene* scene = Resolve(e);
        if (!scene) {
            LOG_WARN(Scripting, "{}('{}') on an entity that's gone - ignored", call, name);
            return std::nullopt;
        }
        const SceneRegistry::ComponentEntry* entry = Services.Registry ? Services.Registry->FindComponent(name) : nullptr;
        if (!entry) throw std::runtime_error(std::format("{}: no component '{}'", call, name));
        if (!Services.Assets) throw std::runtime_error(std::format("{}: no scene assets", call));
        return Target{ scene, entry, Services.Assets(*scene) };
    };

    type["Has"] = [target](const ScriptEntity& e, const std::string& name) {
        const std::optional<Target> t = target(e, name, "Has");
        return t && t->Entry->Has(t->Scene->GetWorld(), e.Id);
    };
    type["Get"] = [this, target](const ScriptEntity& e, const std::string& name) -> sol::object {
        const std::optional<Target> t = target(e, name, "Get");
        if (!t || !t->Entry->Has(t->Scene->GetWorld(), e.Id)) return sol::make_object(Lua, sol::lua_nil);
        return JsonToLua(Lua, t->Entry->WriteAll(t->Scene->GetWorld(), e.Id, *t->Assets));
    };
    // Only the fields given change: e:Set("Chest", { open = true }).
    type["Set"] = [target](const ScriptEntity& e, const std::string& name, const sol::table& fields) {
        const std::optional<Target> t = target(e, name, "Set");
        if (!t) return;
        if (!t->Entry->Has(t->Scene->GetWorld(), e.Id))
            throw std::runtime_error(std::format("Set: the entity has no '{}' (Add it first)", name));
        t->Entry->Read(t->Scene->GetWorld(), e.Id, LuaToJson(fields), *t->Assets);
        SyncCollider(*t->Scene, e.Id);
    };
    type["Add"] = [this, target](const ScriptEntity& e, const std::string& name, const sol::optional<sol::table> fields) {
        const std::optional<Target> t = target(e, name, "Add");
        if (!t) return;
        RequireReshapable(t->Scene->GetWorld());
        if (Dying.contains({ e.SceneSerial, EntityKey(e.Id) })) throw std::runtime_error("Add: the entity is being destroyed");
        if (t->Entry->Has(t->Scene->GetWorld(), e.Id)) throw std::runtime_error(std::format("Add: already has '{}'", name));
        const nlohmann::json json = fields ? LuaToJson(*fields) : nlohmann::json::object();
        t->Entry->Read(t->Scene->GetWorld(), e.Id, json.is_object() ? json : nlohmann::json::object(), *t->Assets);
        SyncCollider(*t->Scene, e.Id);
    };
    type["Remove"] = [this, target](const ScriptEntity& e, const std::string& name) {
        const std::optional<Target> t = target(e, name, "Remove");
        if (!t) return;
        RequireReshapable(t->Scene->GetWorld());
        if (Dying.contains({ e.SceneSerial, EntityKey(e.Id) })) throw std::runtime_error("Remove: the entity is being destroyed");
        t->Entry->Remove(t->Scene->GetWorld(), e.Id);
    };
    // Destroying an entity whose OnDestroy is running (it's on its way out) does nothing.
    type["Destroy"] = [this](const ScriptEntity& e) {
        Scene* scene = Resolve(e);
        if (!scene || Dying.contains({ e.SceneSerial, EntityKey(e.Id) })) return;
        RequireReshapable(scene->GetWorld());
        scene->GetWorld().DestroyEntity(e.Id);
    };

    type["Position"] = sol::property(
        [this](const ScriptEntity& e) -> std::optional<Vec2> {
            Scene* scene = Resolve(e);
            if (!scene) return std::nullopt;
            return scene->GetWorld().GetTransform(e.Id)->Position;
        },
        [this](const ScriptEntity& e, const Vec2 position) {
            Scene* scene = Resolve(e);
            if (!scene) return;
            scene->GetWorld().GetTransform(e.Id)->Position = position;
            SyncCollider(*scene, e.Id);
        });
    type["Tag"] = sol::property(
        [this](const ScriptEntity& e) -> std::optional<std::string> {
            Scene* scene = Resolve(e);
            if (!scene) return std::nullopt;
            return scene->GetWorld().GetComponent<Tag>(e.Id)->Value;
        },
        [this](const ScriptEntity& e, const std::string& tag) {
            if (Scene* scene = Resolve(e)) scene->GetWorld().GetComponent<Tag>(e.Id)->Value = tag;
        });
    type["Name"] = sol::property([this](const ScriptEntity& e) -> std::optional<std::string> {
        Scene* scene = Resolve(e);
        if (!scene) return std::nullopt;
        const Name* name = scene->GetWorld().GetComponent<Name>(e.Id);
        return name ? std::optional(name->Value) : std::nullopt;
    });
}
} // namespace Arcbit
