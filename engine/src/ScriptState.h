#pragma once

// Engine-internal: the Lua state behind ScriptEngine, for the Script*.cpp
// files that bind the engine's API. Never include this from a public header -
// sol2 is template-heavy and would slow every build that saw it.

#include <arcbit/ecs/Entity.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/script/ScriptEngine.h>

#include <nlohmann/json.hpp>

#define SOL_ALL_SAFETIES_ON 1 // check argument types: a script's mistake is an error, not a crash
#define SOL_PRINT_ERRORS 0    // errors are logged by ScriptEngine, with the script's traceback
#include <sol/sol.hpp>

#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Arcbit
{
class Scene;

// A script's handle to an entity: the entity plus the scene it lives in (by
// serial, not pointer), so a handle kept past its entity or its scene is
// recognised as stale instead of reaching freed memory.
struct ScriptEntity
{
    Entity Id;
    u64    SceneSerial = 0;
};

// What a script call is acting on: the scene (for Entity.Find, Tilemap,
// Scene.GetName...), and the instance running, which owns the event
// subscriptions it makes. Empty outside a hook: the console acts on the
// active scene.
struct ScriptContext
{
    Scene*     Scene  = nullptr;
    u64        Serial = 0; // the scene's, for the handles it hands out
    u64        Owner  = 0; // instance id; 0 = the console (owned by nobody, ended by hand)
    sol::table Self;       // the instance, for OnEvent
};

// Accounting shared with the allocator and the instruction hook, which reach
// it through lua_getallocf - so it must outlive the sol::state.
struct ScriptRuntime
{
    ScriptLimits Limits;
    size_t       MemoryUsed = 0;
    u64          StepsLeft  = 0; // instruction budget left, in hook steps
    u32          CallDepth  = 0; // nested C++ -> Lua calls; the budget resets at the outermost
};

struct ScriptEngine::State
{
    std::filesystem::path Root;
    ScriptRuntime         Runtime;
    sol::state            Lua; // after Runtime: it allocates through it until destroyed

    // What every environment starts with: name -> value (plain tables are
    // copied per environment, everything else shared).
    std::vector<std::pair<std::string, sol::object>> Globals;

    std::unordered_map<std::string, sol::object> Modules; // require's cache
    std::set<std::string>                        Loading; // modules mid-load (circular require)

    // Scenes running scripts, by serial (SceneScripts registers itself) -
    // what entity handles resolve against.
    std::unordered_map<u64, Scene*> Scenes;
    u64                             NextSceneSerial = 1;

    ScriptServices Services;
    ScriptContext  Current;
    u64            NextOwnerId = 1; // instance ids (subscription owners)

    // Entities whose OnDestroy is running, as (scene serial, entity): they
    // can't be destroyed again or reshaped while the World removes them.
    std::set<std::pair<u64, u64>> Dying;

    // Event.Subscribe: by id, with the instance that owns each.
    struct LuaSubscription
    {
        u64          Owner = 0;
        Subscription Handle;
    };
    std::unordered_map<u64, LuaSubscription> Subscriptions;
    u64                                      NextSubscriptionId = 1;

    sol::environment                      Console;   // Run's globals, created on first use
    sol::reference                        Traceback; // error handler for Call: adds the stack traceback
    std::function<void(std::string_view)> Output;

    explicit State(std::filesystem::path root, ScriptLimits limits);

    // A fresh sandboxed environment for a module or the console.
    [[nodiscard]] sol::environment NewEnvironment();

    // Add something every environment created from now on gets.
    void AddGlobal(std::string name, sol::object value) { Globals.emplace_back(std::move(name), std::move(value)); }

    // Call `fn` protected: the instruction budget applies, and an error comes
    // back with its traceback (and is logged) instead of unwinding.
    template<typename... Args>
    sol::protected_function_result Call(sol::protected_function fn, Args&&... args)
    {
        const CallScope scope(Runtime);
        fn.set_error_handler(Traceback);
        return fn(std::forward<Args>(args)...);
    }

    // require: the module from the cache, or loaded from the root into its own
    // environment. Throws std::runtime_error (a Lua error, when called from Lua).
    sol::object RequireModule(const std::string& name);

    // print / Log output: the log and, if set, Output.
    void Print(std::string_view line) const;

    // tostring, protected - a failing __tostring becomes a Lua error.
    [[nodiscard]] std::string ToString(const sol::object& value);

    // The scene a handle points into, if it's still running scripts and the
    // entity is still alive; null otherwise.
    [[nodiscard]] Scene* Resolve(const ScriptEntity& handle) const;

    // The scene calls act on: the hook's, else the active one (console).
    [[nodiscard]] Scene* ContextScene() const;
    [[nodiscard]] Scene& RequireScene() const; // throws if there's none
    [[nodiscard]] u64    SerialOf(const Scene* scene) const; // 0 = not running scripts
    [[nodiscard]] ScriptEntity Handle(const Scene& scene, Entity entity) const { return { entity, SerialOf(&scene) }; }

    // End every subscription the instance made (it's going away).
    void EndSubscriptions(u64 owner);

    // Make `next` the context until the scope ends.
    class ContextScope
    {
    public:
        ContextScope(State& state, ScriptContext next) : _state(state), _saved(std::move(state.Current))
        {
            _state.Current = std::move(next);
        }
        ~ContextScope() { _state.Current = std::move(_saved); }
        ContextScope(const ContextScope&)            = delete;
        ContextScope& operator=(const ContextScope&) = delete;

    private:
        State&        _state;
        ScriptContext _saved;
    };

private:
    struct CallScope
    {
        explicit CallScope(ScriptRuntime& runtime);
        ~CallScope();
        ScriptRuntime& Runtime;
    };

    void OpenSandbox();
    void BindRequire();
    void BindLog();
    void BindVec2();
    void BindEntity();
    void BindEntityStatics(sol::usertype<ScriptEntity>& type);
    void BindEntityComponents(sol::usertype<ScriptEntity>& type);
    void BindScene();
    void BindTilemap();
    void BindInput();
    void BindAudio();
    void BindSave();
    void BindEvents();
};

// JSON <-> Lua: objects and arrays become tables, and back - a table with
// keys 1..n is an array, any other an object (string keys). Functions,
// userdata and the like have no JSON form: they become null.
[[nodiscard]] sol::object    JsonToLua(sol::state_view lua, const nlohmann::json& value);
[[nodiscard]] nlohmann::json LuaToJson(const sol::object& value);

// Instruction-count hook granularity: the budget is checked every HookStep
// instructions.
inline constexpr int HookStep = 1000;
} // namespace Arcbit
