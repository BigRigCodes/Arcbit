#include "SceneScripts.h"

#include "ScriptState.h"

#include <arcbit/core/Log.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/script/Script.h>

#include <format>
#include <map>
#include <optional>
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

    Impl(ScriptEngine::State& lua, Scene& owner, std::string sceneModule)
        : Lua(lua), Owner(owner), Serial(lua.NextSceneSerial++), SceneModule(std::move(sceneModule))
    {
        Lua.Scenes[Serial] = &Owner;
    }
    ~Impl() { Lua.Scenes.erase(Serial); }

    std::optional<sol::table> LoadModule(const std::string& name, const std::string& who);
    sol::table                NewSelf(const sol::table& module, const sol::object& defaults, const nlohmann::json& overrides,
                                      const nlohmann::json& saved);
    void                      StartEntity(Entity e);
    void                      StartNew();

    // Call module[hook](args...) if the module has it. False if it failed (logged).
    template<typename... Args>
    bool CallHook(const ScriptInstance& instance, const char* hook, Args&&... args)
    {
        const sol::object fn = instance.Module[hook];
        if (fn.get_type() != sol::type::function) return true;
        const sol::protected_function_result result = Lua.Call(fn.as<sol::protected_function>(), std::forward<Args>(args)...);
        if (result.valid()) return true;
        LOG_ERROR(Scripting, "{} {}: {}", instance.Name, hook, result.get<sol::error>().what());
        return false;
    }
};

std::optional<sol::table> SceneScripts::Impl::LoadModule(const std::string& name, const std::string& who)
{
    try {
        const sol::object module = Lua.RequireModule(name);
        if (module.get_type() == sol::type::table) return module.as<sol::table>();
        LOG_ERROR(Scripting, "{}: script '{}' must return a table of hooks", who, name);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Scripting, "{}: {}", who, e.what());
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
    instance.Name = std::format("{} (Entity {}:{})", script->Module, e.Index, e.Generation);

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
        instance.Name = std::format("scene '{}' ({})", s.Owner.GetName(), s.SceneModule);
        if (const std::optional<sol::table> module = s.LoadModule(s.SceneModule, instance.Name)) {
            instance.Module = *module;
            instance.Self   = s.NewSelf(*module, (*module)["Properties"], {}, {});
            s.SceneScript   = std::move(instance);
            s.CallHook(*s.SceneScript, "OnLoad", s.SceneScript->Self);
        }
    }
    s.StartNew();
}

// An OnUpdate that fails is turned off for that instance, so one broken
// script logs once instead of every tick and the rest carry on.
void SceneScripts::Update(const f32 dt)
{
    Impl& s = *_impl;
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
}

void SceneScripts::Removed(const Entity entity)
{
    Impl&      s  = *_impl;
    const auto it = s.Instances.find(InstanceKey(entity));
    if (it == s.Instances.end()) return;
    const ScriptInstance instance = std::move(it->second);
    s.Instances.erase(it);
    if (!instance.Broken) s.CallHook(instance, "OnDestroy", instance.Self);
}

void SceneScripts::End()
{
    Impl& s = *_impl;
    while (!s.Instances.empty()) { // OnDestroy may destroy others: take one at a time
        const auto it = s.Instances.begin();
        const ScriptInstance instance = std::move(it->second);
        s.Instances.erase(it);
        if (!instance.Broken) s.CallHook(instance, "OnDestroy", instance.Self);
    }
    if (s.SceneScript) s.CallHook(*s.SceneScript, "OnUnload", s.SceneScript->Self);
    s.SceneScript.reset();
}
} // namespace Arcbit
