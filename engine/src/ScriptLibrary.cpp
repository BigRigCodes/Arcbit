// The scripting basics every environment gets: print, Log, Vec2 and require.

#include "ScriptState.h"

#include <arcbit/core/Log.h>
#include <arcbit/core/Math.h>

#include <format>
#include <fstream>
#include <iterator>
#include <regex>

namespace Arcbit
{
namespace
{
    // "chest.lua:12: " - where the calling script is.
    std::string Where(lua_State* L)
    {
        luaL_where(L, 1);
        std::string where = lua_tostring(L, -1);
        lua_pop(L, 1);
        return where;
    }

    // "ai.patrol" -> letters, digits and underscores between dots; nothing
    // that could leave the script root.
    bool IsModuleName(const std::string& name)
    {
        static const std::regex pattern(R"([A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*)");
        return std::regex_match(name, pattern);
    }

    std::filesystem::path ModulePath(const std::filesystem::path& root, std::string name)
    {
        std::ranges::replace(name, '.', '/');
        return root / (name + ".lua");
    }
} // namespace

// ---------------------------------------------------------------------------
// print and Log
// ---------------------------------------------------------------------------

void ScriptEngine::State::BindLog()
{
    const auto join = [this](const sol::variadic_args& args, const char* separator) {
        std::string text;
        for (const sol::object arg : args) {
            if (!text.empty()) text += separator;
            text += ToString(arg);
        }
        return text;
    };

    AddGlobal("print", sol::make_object(Lua, [this, join](const sol::variadic_args args) { Print(join(args, "\t")); }));

    sol::table log = Lua.create_table();
    log.set_function("Info", [this, join](const sol::this_state L, const sol::variadic_args args) {
        Print(Where(L) + join(args, " "));
    });
    log.set_function("Warn", [this, join](const sol::this_state L, const sol::variadic_args args) {
        const std::string line = Where(L) + join(args, " ");
        LOG_WARN(Scripting, "{}", line);
        if (Output) Output("[warn] " + line);
    });
    log.set_function("Error", [this, join](const sol::this_state L, const sol::variadic_args args) {
        const std::string line = Where(L) + join(args, " ");
        LOG_ERROR(Scripting, "{}", line);
        if (Output) Output("[error] " + line);
    });
    AddGlobal("Log", log);
}

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------

// A value type: Vec2(x, y), .X / .Y, + - * / and unary minus, ==, and
// Length / LengthSquared / Normalized / Dot / Distance.
void ScriptEngine::State::BindVec2()
{
    Lua.new_usertype<Vec2>(
        "Vec2", sol::call_constructor, sol::constructors<Vec2(), Vec2(f32, f32)>(), "X", &Vec2::X, "Y", &Vec2::Y,
        sol::meta_function::addition, [](const Vec2& a, const Vec2& b) { return a + b; },
        sol::meta_function::subtraction, [](const Vec2& a, const Vec2& b) { return a - b; },
        sol::meta_function::multiplication,
        sol::overload([](const Vec2& v, const f32 s) { return v * s; }, [](const f32 s, const Vec2& v) { return v * s; }),
        sol::meta_function::division, [](const Vec2& v, const f32 s) { return v / s; },
        sol::meta_function::unary_minus, [](const Vec2& v) { return Vec2{ -v.X, -v.Y }; },
        sol::meta_function::equal_to, [](const Vec2& a, const Vec2& b) { return a == b; },
        sol::meta_function::to_string, [](const Vec2& v) { return std::format("Vec2({}, {})", v.X, v.Y); },
        "Length", [](const Vec2& v) { return Length(v); },
        "LengthSquared", [](const Vec2& v) { return LengthSquared(v); },
        "Normalized", [](const Vec2& v) { const f32 l = Length(v); return l > 0.0f ? v / l : Vec2{}; },
        "Dot", [](const Vec2& a, const Vec2& b) { return Dot(a, b); },
        "Distance", [](const Vec2& a, const Vec2& b) { return Length(a - b); });
    AddGlobal("Vec2", Lua["Vec2"]);
}

// ---------------------------------------------------------------------------
// require
// ---------------------------------------------------------------------------

void ScriptEngine::State::BindRequire()
{
    AddGlobal("require", sol::make_object(Lua, [this](const std::string& name) { return RequireModule(name); }));
}

// Each module runs once, in its own environment; what it returns is cached
// (true if it returns nothing), so every require of it shares one table.
sol::object ScriptEngine::State::RequireModule(const std::string& name)
{
    if (const auto it = Modules.find(name); it != Modules.end()) return it->second;
    if (!IsModuleName(name)) throw std::runtime_error(std::format("require: '{}' isn't a module name", name));
    if (Loading.contains(name)) throw std::runtime_error(std::format("require: '{}' requires itself", name));

    const std::filesystem::path path = ModulePath(Root, name);
    std::ifstream               file(path, std::ios::binary);
    if (!file) throw std::runtime_error(std::format("require: no module '{}' ({})", name, path.generic_string()));
    const std::string code{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

    sol::load_result chunk = Lua.load(code, "@" + path.generic_string(), sol::load_mode::text);
    if (!chunk.valid()) throw std::runtime_error(chunk.get<sol::error>().what());
    sol::protected_function fn = chunk;
    NewEnvironment().set_on(fn);

    Loading.insert(name);
    sol::protected_function_result result = Call(fn);
    Loading.erase(name);
    if (!result.valid()) throw std::runtime_error(result.get<sol::error>().what());

    sol::object value = result.return_count() > 0 ? result.get<sol::object>(0) : sol::object{};
    if (value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none) value = sol::make_object(Lua, true);
    return Modules[name] = value;
}
} // namespace Arcbit
