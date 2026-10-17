#pragma once

#include <arcbit/core/Types.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
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

    // Where require looks for modules.
    [[nodiscard]] const std::filesystem::path& GetRoot() const;

    // Run a line or chunk of Lua, the console's way in: an expression's value
    // comes back in Values ("1 + 2" gives "3"), and globals persist between
    // calls. Errors are logged and returned, never thrown.
    ScriptResult Run(std::string_view code, std::string_view chunkName = "console");

    // Load a module as require would (cached after the first time).
    ScriptResult Require(std::string_view module);

    // Where print and Log output goes, besides the log - the console.
    void SetOutput(std::function<void(std::string_view line)> output);

    // Bytes the Lua state is using.
    [[nodiscard]] size_t GetMemoryUsed() const;

    // Engine-internal: the state behind it (ScriptState.h).
    struct State;
    [[nodiscard]] State& GetState() { return *_state; }

private:
    std::unique_ptr<State> _state;
};
} // namespace Arcbit
