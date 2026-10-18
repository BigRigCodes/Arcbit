// Loc for scripts: the player-visible strings in the player's language
// (docs/localization.md).

#include "ScriptState.h"

#include <arcbit/core/Loc.h>

namespace Arcbit
{
namespace
{
    // { index = 3, name = "Ana" } -> named placeholder values; whole numbers
    // stay whole ("3", not "3.0"), and a number named count picks the plural.
    std::vector<LocArg> ToArgs(const sol::optional<sol::table>& table)
    {
        std::vector<LocArg> args;
        if (!table) return args;
        for (const auto& [key, value] : *table) {
            if (key.get_type() != sol::type::string) continue;
            const std::string name = key.as<std::string>();
            switch (value.get_type()) {
                case sol::type::number: {
                    lua_State* L = value.lua_state();
                    value.push(L);
                    const bool integer = lua_isinteger(L, -1);
                    lua_pop(L, 1);
                    if (integer) args.emplace_back(name, value.as<i64>());
                    else args.emplace_back(name, value.as<f64>());
                    break;
                }
                case sol::type::boolean: args.emplace_back(name, value.as<bool>() ? "true" : "false"); break;
                case sol::type::string: args.emplace_back(name, value.as<std::string>()); break;
                default: break;
            }
        }
        return args;
    }
} // namespace

// Loc.Get / Format / Interpolate / Has, and the language: Language,
// Languages, SetLanguage (through the application, so the choice is saved,
// fonts load and engine.locale.changed fires).
void ScriptEngine::State::BindLoc()
{
    sol::table loc = Lua.create_table();
    loc.set_function("Get", [](const std::string& key) { return Loc::Get(key); });
    loc.set_function("Has", [](const std::string& key) { return Loc::Has(key); });
    loc.set_function("Format", [](const std::string& key, const sol::optional<sol::table> args) {
        return Loc::Format(key, ToArgs(args));
    });
    loc.set_function("Interpolate", [](const std::string& text, const sol::optional<sol::table> args) {
        return Loc::Interpolate(text, ToArgs(args));
    });
    loc.set_function("FormatNumber", [](const f64 value, const sol::optional<i32> decimals) {
        return Loc::FormatNumber(value, decimals.value_or(-1));
    });
    loc.set_function("FormatDate", [](const i64 unixSeconds, const sol::optional<std::string> style) {
        return Loc::FormatDate(unixSeconds, style.value_or("date"));
    });
    loc.set_function("Language", [] { return Loc::GetLanguage(); });
    loc.set_function("Languages", [this] {
        sol::table out = Lua.create_table();
        for (const Loc::Locale& locale : Loc::GetLocales())
            out.add(Lua.create_table_with("Code", locale.Code, "Name", locale.Name));
        return out;
    });
    loc.set_function("SetLanguage", [this](const std::string& code) {
        return Services.SetLanguage ? Services.SetLanguage(code) : Loc::SetLanguage(code);
    });
    AddGlobal("Loc", loc);
}
} // namespace Arcbit
