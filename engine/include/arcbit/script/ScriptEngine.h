#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/dialog/DialogRequest.h>
#include <arcbit/save/SaveManager.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class DebugConsole;
class EventBus;
class InputManager;
class PrefabLibrary;
class Scene;
class SceneAssets;
class SceneManager;
class SceneRegistry;
class UIManager;

// What the engine API scripts call reaches (Application wires all of it;
// tests wire what they need). Anything left empty makes the matching calls
// do nothing, with a warning.
struct ScriptServices
{
    SceneManager*        Scenes   = nullptr; // Scene.*, and the scene the console acts on
    const InputManager*  Input    = nullptr; // Input.*
    EventBus*            Events   = nullptr; // Event.*
    SaveManager*         Saves    = nullptr; // Save.Get / Set
    const SceneRegistry* Registry = nullptr; // entity:Get / Set / Add / Remove, Entity.Create
    const PrefabLibrary* Prefabs  = nullptr; // Entity.Spawn
    UIManager*           UI       = nullptr; // UI.Open / Close, scripted screens
    DebugConsole*        Console  = nullptr; // Console.Register / Print

    std::function<std::unique_ptr<SceneAssets>(Scene&)> Assets; // component reads / writes
    std::function<void(std::string_view path, f32 volume, std::string_view category)> PlaySound;
    std::function<bool(SaveSlot)> SaveTo;
    std::function<bool(SaveSlot)> LoadFrom;
    std::function<bool()>         AutoSave;
    std::function<bool(std::string_view code)> SetLanguage; // Loc.SetLanguage: Application::SetLanguage

    // Dialog.Start / IsActive / Stop: Application's conversations.
    std::function<bool(const DialogRequest&)> StartDialog;
    std::function<bool()>                     DialogActive;
    std::function<void()>                     StopDialog;
};

// Limits on what one call into Lua may use - a runaway script becomes a
// logged error instead of a frozen or exhausted game.
struct ScriptLimits
{
    u64    MaxInstructions = 50'000'000; // per call from C++ into Lua (nested calls share it)
    size_t MaxMemory       = 256u << 20; // bytes, for the whole Lua state
};

// What a call into Lua gave back: its results as text (the console prints
// them), or the error with its traceback.
struct ScriptResult
{
    bool                     Ok = true;
    std::string              Error;
    std::vector<std::string> Values;
};

// ---------------------------------------------------------------------------
// ScriptEngine - the Lua 5.4 state every script runs in (docs/scripting.md).
//
// Sandboxed: scripts get the safe parts of Lua - base functions, string,
// table, math, utf8, coroutine, os.time / clock / date - and the engine's API,
// but no io, os.execute, debug, package or loading of files or bytecode.
// `require("ai.patrol")` loads only from the script root
// (assets/scripts/ai/patrol.lua). Every module gets its own environment, with
// its own copies of the libraries, so scripts can't tamper with each other.
//
// Contained: every call from C++ into Lua is protected. An error is logged
// with its traceback and the call abandoned; a call that runs past
// MaxInstructions is stopped (a frozen loop becomes an error, and pcall can't
// swallow it); the state can't grow past MaxMemory.
//
// Lua and sol2 never appear in engine headers: the rest of the scripting
// layer reaches the state through State (ScriptState.h, engine-internal).
// ---------------------------------------------------------------------------
class ScriptEngine
{
public:
    explicit ScriptEngine(std::filesystem::path root = "assets/scripts", ScriptLimits limits = {});
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&)            = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    // Where require looks for modules. Set it before any script loads (a
    // debug build can point it at the source folder, so edits hot-reload).
    [[nodiscard]] const std::filesystem::path& GetRoot() const;
    void                                       SetRoot(std::filesystem::path root);

    // Run a line or chunk of Lua, the console's way in: an expression's value
    // comes back in Values ("1 + 2" gives "3"), and globals persist between
    // calls. Errors are logged and returned, never thrown.
    ScriptResult Run(std::string_view code, std::string_view chunkName = "console");

    // Dialogs' Lua ("lua" in an .arcdialog): an expression tested for truth
    // (nil and false are false), and a statement run. They share an
    // environment of their own - the engine API, with globals that last
    // between dialogs. An error is reported (logged, SetErrorHandler) and
    // makes a test false.
    [[nodiscard]] bool Test(std::string_view expression, std::string_view chunkName = "dialog");
    bool               Execute(std::string_view code, std::string_view chunkName = "dialog");

    // Load a module as require would (cached after the first time).
    ScriptResult Require(std::string_view module);

    // The console's predictive completion: for the name being typed at the
    // end of `line` ("Entity.Fi", "pos:Len", "Sav"), what would complete it,
    // sorted ("nd" for "Entity.Fi") - from the console's globals, a table's
    // keys and a bound type's methods.
    [[nodiscard]] std::vector<std::string> Complete(std::string_view line);

    // Where print and Log output goes, besides the log - the console.
    void SetOutput(std::function<void(std::string_view line)> output);

    // What the engine API reaches (see ScriptServices).
    void SetServices(ScriptServices services);

    // Every script error (a failed hook, task, handler or console line), as
    // logged with its traceback - for the console and debug builds' toasts.
    void SetErrorHandler(std::function<void(std::string_view error)> handler);

    // Debug builds: reload scripts whose files changed (Application, once a
    // second). Running instances keep their state and pick up the new
    // functions; an OnUpdate turned off by an error comes back on. Returns
    // how many modules reloaded.
    u32 CheckReloads();

    // Bytes the Lua state is using.
    [[nodiscard]] size_t GetMemoryUsed() const;

    // Engine-internal: the state behind it (ScriptState.h).
    struct State;
    [[nodiscard]] State& GetState() { return *_state; }

private:
    std::unique_ptr<State> _state;
};
} // namespace Arcbit
