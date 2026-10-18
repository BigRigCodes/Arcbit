// The Lua state: allocation limits, the instruction budget, the sandbox, and
// the console / require entry points. The engine's API is bound in the
// Script*.cpp files next to this one.

#include "ScriptState.h"

#include <arcbit/app/DebugConsole.h>
#include <arcbit/ui/ScriptedScreen.h>

#include <arcbit/core/Log.h>

#include <array>
#include <ranges>
#include <cstdlib>

namespace Arcbit
{
namespace
{
    // lua_Alloc with a cap: past MaxMemory an allocation fails, which Lua
    // raises as a "not enough memory" error in the script that asked.
    void* Allocate(void* ud, void* ptr, const size_t oldSize, const size_t newSize)
    {
        auto*        runtime = static_cast<ScriptRuntime*>(ud);
        const size_t old     = ptr ? oldSize : 0; // with no block, oldSize is a type tag
        if (newSize == 0) {
            std::free(ptr);
            runtime->MemoryUsed -= old;
            return nullptr;
        }
        if (newSize > old && runtime->MemoryUsed - old + newSize > runtime->Limits.MaxMemory) return nullptr;
        void* block = std::realloc(ptr, newSize);
        if (block) runtime->MemoryUsed = runtime->MemoryUsed - old + newSize;
        return block;
    }

    // Count hook: every HookStep instructions, spend one step of the budget.
    // Once it's gone the hook fires on every instruction and raises each time,
    // so code that catches the error with pcall hits it again straight away
    // and the call unwinds all the way out. The next call's budget puts the
    // step back (hooks are per thread, so each coroutine resets its own).
    void CountInstructions(lua_State* L, lua_Debug*)
    {
        void* ud = nullptr;
        lua_getallocf(L, &ud);
        auto* runtime = static_cast<ScriptRuntime*>(ud);
        if (runtime->CallDepth == 0) return;
        if (runtime->StepsLeft > 0) {
            --runtime->StepsLeft;
            if (lua_gethookcount(L) != HookStep) lua_sethook(L, &CountInstructions, LUA_MASKCOUNT, HookStep);
            return;
        }
        lua_sethook(L, &CountInstructions, LUA_MASKCOUNT, 1);
        luaL_error(L, "script ran too long (over %I instructions) - stopped",
                   static_cast<lua_Integer>(runtime->Limits.MaxInstructions));
    }

    // Error handler for protected calls: the message plus a stack traceback.
    int AddTraceback(lua_State* L)
    {
        const char* message = lua_tostring(L, 1);
        luaL_traceback(L, L, message ? message : "(error object is not a string)", 1);
        return 1;
    }

    // The base library functions scripts keep. Not here: load, loadfile,
    // dofile (code from anywhere), collectgarbage, and print / require (the
    // engine's own).
    constexpr std::array SafeBase = { "assert", "error",  "getmetatable", "ipairs",       "next",     "pairs",
                                      "pcall",  "rawequal", "rawget",     "rawlen",       "rawset",   "select",
                                      "setmetatable", "tonumber", "tostring", "type",     "xpcall",   "_VERSION" };
    constexpr std::array SafeLibraries = { "string", "table", "math", "utf8", "coroutine" };
    constexpr std::array SafeOs        = { "time", "clock", "date", "difftime" };

    [[nodiscard]] bool HasMetatable(const sol::object& value)
    {
        if (value.get_type() != sol::type::table) return false;
        const sol::table table = value;
        return table[sol::metatable_key].get_type() == sol::type::table;
    }

    [[nodiscard]] sol::table Copy(sol::state_view lua, const sol::table& from)
    {
        sol::table to = lua.create_table();
        for (const auto& [key, value] : from) to[key] = value;
        return to;
    }

    ScriptResult Fail(const ScriptEngine::State& s, std::string error)
    {
        s.ReportError(error);
        return ScriptResult{ .Ok = false, .Error = std::move(error) };
    }
} // namespace

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

ScriptEngine::State::State(std::filesystem::path root, const ScriptLimits limits)
    : Root(std::move(root)), Runtime{ .Limits = limits }, Lua(sol::default_at_panic, &Allocate, &Runtime)
{
    lua_State* L = Lua.lua_state();
    lua_sethook(L, &CountInstructions, LUA_MASKCOUNT, HookStep);
    lua_pushcfunction(L, &AddTraceback);
    Traceback = sol::reference(L, -1);
    lua_pop(L, 1);

    OpenSandbox();
    BindLog();
    BindVec2();
    BindEntity();
    BindScene();
    BindTilemap();
    BindInput();
    BindAudio();
    BindSave();
    BindEvents();
    BindTasks();
    BindUI();
    BindConsole();
    BindRequire();
}

// Opens the safe libraries into the state's own globals - which scripts never
// see - and lists what each environment gets.
void ScriptEngine::State::OpenSandbox()
{
    Lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8,
                       sol::lib::coroutine, sol::lib::os);

    // ("x"):upper() goes through the real string table: no bytecode dumping
    // there, and its metatable stays out of reach.
    Lua["string"]["dump"] = sol::lua_nil;
    Lua.script("getmetatable('').__metatable = false");

    for (const char* name : SafeBase) AddGlobal(name, Lua[name]);
    for (const char* name : SafeLibraries) AddGlobal(name, Lua[name]);
    sol::table os = Lua.create_table();
    for (const char* name : SafeOs) os[name] = Lua["os"][name];
    AddGlobal("os", os);
}

// Plain tables (the libraries, the engine's API tables) are copied, so a
// script changing `string.upper` changes only its own; values with a
// metatable (Vec2 and other bound types) and functions are shared.
sol::environment ScriptEngine::State::NewEnvironment()
{
    sol::environment env(Lua, sol::create);
    for (const auto& [name, value] : Globals) {
        if (value.get_type() == sol::type::table && !HasMetatable(value)) env[name] = Copy(Lua, value);
        else env[name] = value;
    }
    env["_G"] = env;
    return env;
}

ScriptEngine::State::CallScope::CallScope(ScriptRuntime& runtime) : Runtime(runtime)
{
    if (Runtime.CallDepth++ == 0) Runtime.StepsLeft = Runtime.Limits.MaxInstructions / HookStep;
}

ScriptEngine::State::CallScope::~CallScope() { --Runtime.CallDepth; }

std::string ScriptEngine::State::ToString(const sol::object& value)
{
    const sol::protected_function tostring = Lua["tostring"];
    sol::protected_function_result result  = Call(tostring, value);
    if (!result.valid()) {
        const sol::error error = result;
        throw std::runtime_error(error.what());
    }
    return result.get<std::string>();
}

void ScriptEngine::State::Print(const std::string_view line) const
{
    LOG_INFO(Scripting, "{}", line);
    if (Output) Output(line);
}

void ScriptEngine::State::ReportError(const std::string& error) const
{
    LOG_ERROR(Scripting, "{}", error);
    if (OnError) OnError(error);
}

// ---------------------------------------------------------------------------
// ScriptEngine
// ---------------------------------------------------------------------------

ScriptEngine::ScriptEngine(std::filesystem::path root, const ScriptLimits limits)
    : _state(std::make_unique<State>(std::move(root), limits))
{
    _state->Engine = this;
}

// Screens still open (Lua UI) and console commands scripts registered hold
// references into the state: they let go first, so nothing touches it once
// it's closed.
ScriptEngine::~ScriptEngine()
{
    while (!_state->Screens.empty()) _state->Screens.begin()->second->DetachScripts();
    if (_state->Services.Console)
        for (const std::string& name : _state->ConsoleOwners | std::views::keys) _state->Services.Console->Unregister(name);
    _state->ConsoleOwners.clear();
}

const std::filesystem::path& ScriptEngine::GetRoot() const { return _state->Root; }

void ScriptEngine::SetOutput(std::function<void(std::string_view)> output) { _state->Output = std::move(output); }

void ScriptEngine::SetServices(ScriptServices services) { _state->Services = std::move(services); }

void ScriptEngine::SetErrorHandler(std::function<void(std::string_view)> handler) { _state->OnError = std::move(handler); }

size_t ScriptEngine::GetMemoryUsed() const { return _state->Runtime.MemoryUsed; }

// Tried as an expression first ("return 1 + 2"), so the console shows values
// the way the Lua prompt does.
ScriptResult ScriptEngine::Run(const std::string_view code, const std::string_view chunkName)
{
    State& s = *_state;
    if (!s.Console.valid()) s.Console = s.NewEnvironment();
    const std::string name  = "=" + std::string(chunkName);
    sol::load_result  chunk = s.Lua.load("return " + std::string(code), name, sol::load_mode::text);
    if (!chunk.valid()) chunk = s.Lua.load(code, name, sol::load_mode::text);
    if (!chunk.valid()) return Fail(s, chunk.get<sol::error>().what());

    sol::protected_function fn = chunk;
    s.Console.set_on(fn);
    try {
        sol::protected_function_result result = s.Call(fn);
        if (!result.valid()) return Fail(s, result.get<sol::error>().what());
        ScriptResult out;
        for (int i = 0; i < result.return_count(); ++i) out.Values.push_back(s.ToString(result.get<sol::object>(i)));
        return out;
    }
    catch (const std::exception& e) {
        return Fail(s, e.what());
    }
}

ScriptResult ScriptEngine::Require(const std::string_view module)
{
    try {
        (void)_state->RequireModule(std::string(module));
        return {};
    }
    catch (const std::exception& e) {
        return Fail(*_state, e.what());
    }
}
} // namespace Arcbit
