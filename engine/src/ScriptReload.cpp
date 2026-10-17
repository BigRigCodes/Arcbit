// Hot reload: a script edited while the game runs takes effect without a
// restart. The module's table is patched in place - new and changed
// functions replace the old ones, data the module already holds is kept - so
// every instance (whose metatable points at that table) keeps its own state
// and runs the new code from its next hook.

#include "ScriptState.h"

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Arcbit
{
namespace
{
    // The new code's functions closed over the new chunk's own module table
    // (its `local M`). Point any upvalue holding `from` at `to` instead, so
    // they read and write the module data the game already has. Closures of
    // one chunk share the upvalue, so this reaches functions made later too.
    void Retarget(lua_State* L, const sol::object& fn, const sol::table& from, const sol::table& to)
    {
        const int base = lua_gettop(L);
        fn.push(L);
        from.push(L);
        to.push(L);
        for (int i = 1; lua_getupvalue(L, base + 1, i) != nullptr; ++i) {
            const bool match = lua_rawequal(L, -1, base + 2);
            lua_pop(L, 1);
            if (!match) continue;
            lua_pushvalue(L, base + 3);
            lua_setupvalue(L, base + 1, i);
        }
        lua_settop(L, base);
    }
} // namespace

bool ScriptEngine::State::ReloadModule(const std::string& name, ModuleFile& file)
{
    std::ifstream in(file.Path, std::ios::binary);
    if (!in) return false;
    const std::string code{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

    sol::load_result chunk = Lua.load(code, "@" + file.Path.generic_string(), sol::load_mode::text);
    if (!chunk.valid()) {
        ReportError(std::format("Reloading '{}': {}", name, chunk.get<sol::error>().what()));
        return false;
    }
    sol::protected_function fn = chunk;
    NewEnvironment().set_on(fn);
    sol::protected_function_result result = Call(fn);
    if (!result.valid()) {
        ReportError(std::format("Reloading '{}': {}", name, result.get<sol::error>().what()));
        return false;
    }

    const sol::object fresh = result.return_count() > 0 ? result.get<sol::object>(0) : sol::object{};
    sol::object&      kept  = Modules[name];
    if (kept.get_type() != sol::type::table || fresh.get_type() != sol::type::table) {
        kept = fresh; // not a table of hooks: nothing to patch, just replace
        return true;
    }
    sol::table       target = kept;
    const sol::table source = fresh;
    for (const auto& [key, value] : source) {
        if (value.get_type() == sol::type::function) Retarget(Lua.lua_state(), value, source, target);
        if (value.get_type() == sol::type::function || target[key].get_type() == sol::type::lua_nil) target[key] = value;
    }
    return true;
}

// A file whose save fails to load keeps its old code (logged) and isn't
// retried until it changes again.
u32 ScriptEngine::CheckReloads()
{
    State& s        = *_state;
    u32    reloaded = 0;
    for (auto& [name, file] : s.ModuleFiles) {
        std::error_code ec;
        const auto      changed = std::filesystem::last_write_time(file.Path, ec);
        if (ec || changed == file.Changed) continue;
        file.Changed = changed;
        if (!s.ReloadModule(name, file)) continue;
        ++reloaded;
        s.Print(std::format("Reloaded script '{}'", name));
    }
    if (reloaded > 0) ++s.ReloadGeneration;
    return reloaded;
}
} // namespace Arcbit
