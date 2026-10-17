#include "SceneScripts.h"

#include "ScriptState.h"

#include <arcbit/core/Log.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/script/Script.h>

#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <vector>

namespace Arcbit
{
namespace
{
    u64 InstanceKey(const Entity e) { return (static_cast<u64>(e.Index) << 32) | e.Generation; }
} // namespace

// One scripted entity's instance: `self`, and the module it reads hooks from.
struct ScriptInstance
{
    u64         Id = 0; // owns the event subscriptions it makes
    Entity      Owner;  // its entity (invalid for the scene script)
    sol::table  Self;
    sol::table  Module;
    std::string Name;               // "chest (Entity 3:1)", for logs
    bool        Broken      = false; // its module didn't load: no hooks
    bool        UpdateOff   = false; // OnUpdate failed once: turned off (until reloaded)
};

struct SceneScripts::Impl
{
    ScriptEngine::State& Lua;
    Scene&               Owner;
    u64                  Serial;
    std::string          SceneModule;
    std::optional<ScriptInstance>   SceneScript;
    std::map<u64, ScriptInstance>   Instances; // by entity; ordered, so hooks run in a stable order
    bool                            Listening = false; // the physics contact listener is ours
    u32                             Generation = 0;    // Lua.ReloadGeneration last seen

    Impl(ScriptEngine::State& lua, Scene& owner, std::string sceneModule)
        : Lua(lua), Owner(owner), Serial(lua.NextSceneSerial++), SceneModule(std::move(sceneModule))
    {
        Lua.Scenes[Serial] = &Owner;
    }
    ~Impl()
    {
        StopListening();
        Lua.StopTasks(0, Serial);
        Lua.Scenes.erase(Serial);
    }

    void StartListening();
    void StopListening();
    void Contact(PhysicsWorld::ContactEvent event, Entity self, Entity other, Vec2 normal);

    std::optional<sol::table> LoadModule(const std::string& name, const std::string& who);
    sol::table                NewSelf(const sol::table& module, const sol::object& defaults, const nlohmann::json& overrides,
                                      const nlohmann::json& saved);
    void                      StartEntity(Entity e);
    void                      StartNew();

    // Call module[hook](args...) if the module has it, acting on this scene
    // as this instance. False if it failed (logged).
    template<typename... Args>
    bool CallHook(const ScriptInstance& instance, const char* hook, Args&&... args)
    {
        const sol::object fn = instance.Module[hook];
        if (fn.get_type() != sol::type::function) return true;
        const ScriptEngine::State::ContextScope scope(
            Lua, ScriptContext{ .Scene = &Owner, .Serial = Serial, .Owner = instance.Id, .Self = instance.Self });
        const sol::protected_function_result result = Lua.Call(fn.as<sol::protected_function>(), std::forward<Args>(args)...);
        if (result.valid()) return true;
        Lua.ReportError(std::format("{} {}: {}", instance.Name, hook, result.get<sol::error>().what()));
        return false;
    }
};

std::optional<sol::table> SceneScripts::Impl::LoadModule(const std::string& name, const std::string& who)
{
    try {
        const sol::object module = Lua.RequireModule(name);
        if (module.get_type() == sol::type::table) return module.as<sol::table>();
        Lua.ReportError(std::format("{}: script '{}' must return a table of hooks", who, name));
    }
    catch (const std::exception& e) {
        Lua.ReportError(std::format("{}: {}", who, e.what()));
    }
    return std::nullopt;
}

// self: Properties (the module's, deep-copied, with the overrides on top),
// Saved, and the module behind it for every hook and shared function.
sol::table SceneScripts::Impl::NewSelf(const sol::table& module, const sol::object& defaults,
                                       const nlohmann::json& overrides, const nlohmann::json& saved)
{
    nlohmann::json properties = defaults.get_type() == sol::type::table ? LuaToJson(defaults) : nlohmann::json::object();
    if (!properties.is_object()) properties = nlohmann::json::object();
    if (overrides.is_object())
        for (const auto& [key, value] : overrides.items()) properties[key] = value;

    sol::table self       = Lua.Lua.create_table();
    self["Run"]           = Lua.SelfRun; // self:Run(fn, ...) - a task owned by this instance
    self["Properties"]    = JsonToLua(Lua.Lua, properties);
    self["Saved"]         = JsonToLua(Lua.Lua, saved.is_object() ? saved : nlohmann::json::object());
    sol::table meta       = Lua.Lua.create_table();
    meta["__index"]       = module;
    self[sol::metatable_key] = meta;
    return self;
}

// In the map before OnStart, so an OnStart that destroys its own entity
// finds itself and gets its OnDestroy.
void SceneScripts::Impl::StartEntity(const Entity e)
{
    const Script* script = Owner.GetWorld().GetComponent<Script>(e);
    if (!script) return; // gone since StartNew listed it
    ScriptInstance instance;
    instance.Id    = Lua.NextOwnerId++;
    instance.Owner = e;
    instance.Name  = std::format("{} (Entity {}:{})", script->Module, e.Index, e.Generation);

    const std::optional<sol::table> module = LoadModule(script->Module, instance.Name);
    if (!module) {
        instance.Broken = true;
        Instances[InstanceKey(e)] = std::move(instance);
        return;
    }
    instance.Module         = *module;
    instance.Self           = NewSelf(*module, (*module)["Properties"], script->Properties, script->Saved);
    instance.Self["Entity"] = ScriptEntity{ e, Serial };
    const ScriptInstance& started = Instances[InstanceKey(e)] = std::move(instance);
    CallHook(started, "OnStart", started.Self);
}

// Scripts not started yet - there at load, or added since. Listed first and
// started after: an OnStart may create or destroy entities.
void SceneScripts::Impl::StartNew()
{
    std::vector<Entity> fresh;
    Owner.GetWorld().Query<Script>().ForEach([&](const Entity e, const Script&) {
        if (!Instances.contains(InstanceKey(e))) fresh.push_back(e);
    });
    for (const Entity e : fresh) StartEntity(e);
}

// Trigger and collision events reach the scripts on either entity:
// OnTriggerEnter / Stay / Exit(other), OnCollisionEnter(other, normal),
// OnCollisionExit(other).
void SceneScripts::Impl::StartListening()
{
    Owner.GetPhysics().SetContactListener(
        [this](const PhysicsWorld::ContactEvent event, const Entity self, const Entity other, const Vec2 normal) {
            Contact(event, self, other, normal);
        });
    Listening = true;
}

void SceneScripts::Impl::StopListening()
{
    if (Listening) Owner.GetPhysics().SetContactListener({});
    Listening = false;
}

void SceneScripts::Impl::Contact(const PhysicsWorld::ContactEvent event, const Entity self, const Entity other,
                                 const Vec2 normal)
{
    const auto it = Instances.find(InstanceKey(self));
    if (it == Instances.end() || it->second.Broken) return;
    const ScriptInstance& instance = it->second;
    const ScriptEntity    handle{ other, Serial };
    switch (event) {
        case PhysicsWorld::ContactEvent::TriggerEnter: CallHook(instance, "OnTriggerEnter", instance.Self, handle); break;
        case PhysicsWorld::ContactEvent::TriggerStay: CallHook(instance, "OnTriggerStay", instance.Self, handle); break;
        case PhysicsWorld::ContactEvent::TriggerExit: CallHook(instance, "OnTriggerExit", instance.Self, handle); break;
        case PhysicsWorld::ContactEvent::CollisionEnter:
            CallHook(instance, "OnCollisionEnter", instance.Self, handle, normal);
            break;
        case PhysicsWorld::ContactEvent::CollisionExit: CallHook(instance, "OnCollisionExit", instance.Self, handle); break;
    }
}

// ---------------------------------------------------------------------------
// SceneScripts
// ---------------------------------------------------------------------------

SceneScripts::SceneScripts(ScriptEngine& engine, Scene& scene, std::string sceneModule)
    : _impl(std::make_unique<Impl>(engine.GetState(), scene, std::move(sceneModule)))
{
}

SceneScripts::~SceneScripts() = default;

void SceneScripts::Start()
{
    Impl& s = *_impl;
    if (!s.SceneModule.empty()) {
        ScriptInstance instance;
        instance.Id   = s.Lua.NextOwnerId++;
        instance.Name = std::format("scene '{}' ({})", s.Owner.GetName(), s.SceneModule);
        if (const std::optional<sol::table> module = s.LoadModule(s.SceneModule, instance.Name)) {
            instance.Module = *module;
            instance.Self   = s.NewSelf(*module, (*module)["Properties"], {}, {});
            s.SceneScript   = std::move(instance);
            s.CallHook(*s.SceneScript, "OnLoad", s.SceneScript->Self);
        }
    }
    s.StartListening();
    s.StartNew();
}

void SceneScripts::Interact(const Entity target, const Entity by)
{
    Impl&      s  = *_impl;
    const auto it = s.Instances.find(InstanceKey(target));
    if (it == s.Instances.end() || it->second.Broken) return;
    s.CallHook(it->second, "OnInteract", it->second.Self, ScriptEntity{ by, s.Serial });
}

// Only plain values save (LuaToJson): numbers, text, booleans, tables of
// them; a Vec2 becomes { x, y }.
void SceneScripts::SyncSaved()
{
    Impl& s = *_impl;
    for (const ScriptInstance& instance : s.Instances | std::views::values) {
        if (instance.Broken) continue;
        Script* script = s.Owner.GetWorld().GetComponent<Script>(instance.Owner);
        if (!script) continue;
        const nlohmann::json saved = LuaToJson(instance.Self["Saved"]);
        script->Saved              = saved.is_object() ? saved : nlohmann::json::object();
    }
}

// An OnUpdate that fails is turned off for that instance, so one broken
// script logs once instead of every tick and the rest carry on.
void SceneScripts::Update(const f32 dt)
{
    Impl& s = *_impl;
    if (s.Generation != s.Lua.ReloadGeneration) { // scripts reloaded: a fixed OnUpdate may run again
        s.Generation = s.Lua.ReloadGeneration;
        if (s.SceneScript) s.SceneScript->UpdateOff = false;
        for (ScriptInstance& instance : s.Instances | std::views::values) instance.UpdateOff = false;
    }
    s.StartNew();
    if (s.SceneScript && !s.SceneScript->UpdateOff && !s.CallHook(*s.SceneScript, "OnUpdate", s.SceneScript->Self, dt))
        s.SceneScript->UpdateOff = true;

    std::vector<u64> keys;
    keys.reserve(s.Instances.size());
    for (const auto& [key, instance] : s.Instances) keys.push_back(key);
    for (const u64 key : keys) {
        const auto it = s.Instances.find(key); // an earlier hook may have destroyed it
        if (it == s.Instances.end() || it->second.Broken || it->second.UpdateOff) continue;
        if (s.CallHook(it->second, "OnUpdate", it->second.Self, dt)) continue;
        it->second.UpdateOff = true;
        LOG_WARN(Scripting, "{}: OnUpdate turned off after its error", it->second.Name);
    }
    s.Lua.AdvanceTasks(s.Serial, dt);
}

// While its OnDestroy runs the entity is "dying": readable, but the script
// can't destroy it again or reshape it mid-removal.
void SceneScripts::Removed(const Entity entity)
{
    Impl&      s  = *_impl;
    const auto it = s.Instances.find(InstanceKey(entity));
    if (it == s.Instances.end()) return;
    const ScriptInstance instance = std::move(it->second);
    s.Instances.erase(it);
    const std::pair<u64, u64> dying{ s.Serial, InstanceKey(entity) };
    s.Lua.Dying.insert(dying);
    if (!instance.Broken) s.CallHook(instance, "OnDestroy", instance.Self);
    s.Lua.Dying.erase(dying);
    s.Lua.EndSubscriptions(instance.Id);
}

void SceneScripts::End()
{
    Impl& s = *_impl;
    s.StopListening();
    while (!s.Instances.empty()) { // OnDestroy may destroy others: take one at a time
        const auto it = s.Instances.begin();
        const ScriptInstance instance = std::move(it->second);
        s.Instances.erase(it);
        if (!instance.Broken) s.CallHook(instance, "OnDestroy", instance.Self);
        s.Lua.EndSubscriptions(instance.Id);
    }
    if (s.SceneScript) {
        s.CallHook(*s.SceneScript, "OnUnload", s.SceneScript->Self);
        s.Lua.EndSubscriptions(s.SceneScript->Id);
    }
    s.SceneScript.reset();
    s.Lua.StopTasks(0, s.Serial); // any left: started from the console
}
} // namespace Arcbit
