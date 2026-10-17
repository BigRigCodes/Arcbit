#pragma once

// Engine-internal: the Lua state behind ScriptEngine, for the Script*.cpp
// files that bind the engine's API. Never include this from a public header -
// sol2 is template-heavy and would slow every build that saw it.

#include <arcbit/script/ScriptEngine.h>

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
};

// Instruction-count hook granularity: the budget is checked every HookStep
// instructions.
inline constexpr int HookStep = 1000;
} // namespace Arcbit
