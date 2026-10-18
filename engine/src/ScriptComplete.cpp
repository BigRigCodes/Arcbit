// Predictive completion for the console's Lua: the name being typed at the
// end of a line ("Entity.Fi", "pos:Len", "Sav"), completed from what's
// really there - the console's globals, a table's keys, a bound type's
// methods.

#include "ScriptState.h"

#include <algorithm>
#include <cctype>

namespace Arcbit
{
namespace
{
    bool IsNameChar(const char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

    // A plain identifier, not one of sol2's or Lua's internals ("__index").
    bool IsListed(const sol::object& key)
    {
        if (key.get_type() != sol::type::string) return false;
        const std::string name = key.as<std::string>();
        return !name.empty() && !name.starts_with("__") && !name.starts_with("class_") &&
               std::ranges::all_of(name, IsNameChar);
    }

    // The listable keys of `table`.
    void AddKeys(const sol::table& table, std::vector<std::string>& names)
    {
        for (const auto& [key, value] : table)
            if (IsListed(key)) names.push_back(key.as<std::string>());
    }

    // A bound type's table (Entity, Vec2) keeps its members in sol2's own
    // storage; its metatable's __type.name leads to the instance metatable
    // in the registry ("sol." + the C++ name), which lists them.
    void AddBoundTypeKeys(const sol::table& meta, std::vector<std::string>& names)
    {
        const sol::object type = meta["__type"];
        if (type.get_type() != sol::type::table) return;
        const sol::object name = type.as<sol::table>()["name"];
        if (name.get_type() != sol::type::string) return;
        const sol::object instance = sol::state_view(meta.lua_state()).registry()["sol." + name.as<std::string>()];
        if (instance.get_type() == sol::type::table) AddKeys(instance.as<sol::table>(), names);
    }

    std::vector<std::string> MembersOf(const sol::object& scope)
    {
        std::vector<std::string> names;
        lua_State*               L = scope.lua_state();
        if (scope.get_type() == sol::type::table) AddKeys(scope.as<sol::table>(), names);
        scope.push(L);
        if (lua_getmetatable(L, -1)) { // pushes the metatable
            const sol::table meta(L, -1);
            AddKeys(meta, names);
            AddBoundTypeKeys(meta, names);
            if (const sol::object index = meta["__index"]; index.get_type() == sol::type::table) AddKeys(index.as<sol::table>(), names);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return names;
    }

    // `scope.name`, through tables only (no getters run for a completion).
    sol::object Member(const sol::object& scope, const std::string& name)
    {
        if (scope.get_type() != sol::type::table) return sol::object{};
        return scope.as<sol::table>().raw_get<sol::object>(name);
    }
} // namespace

// The remainders that would complete the trailing name, sorted: "Save.Ge"
// gives { "t" }, "Ent" gives { "ity" }.
std::vector<std::string> ScriptEngine::Complete(const std::string_view line)
{
    State& s = *_state;
    if (!s.Console.valid()) s.Console = s.NewEnvironment();

    size_t start = line.size();
    while (start > 0 && (IsNameChar(line[start - 1]) || line[start - 1] == '.' || line[start - 1] == ':')) --start;
    const std::string_view token = line.substr(start);
    if (token.empty() || std::isdigit(static_cast<unsigned char>(token.front()))) return {};

    const size_t     split   = token.find_last_of(".:");
    const std::string partial(split == std::string_view::npos ? token : token.substr(split + 1));
    sol::object       scope = s.Console;
    if (split != std::string_view::npos) {
        std::string path(token.substr(0, split));
        std::ranges::replace(path, ':', '.');
        for (size_t at = 0; at <= path.size() && scope.valid();) {
            const size_t dot = path.find('.', at);
            scope            = Member(scope, path.substr(at, dot == std::string::npos ? std::string::npos : dot - at));
            at               = dot == std::string::npos ? path.size() + 1 : dot + 1;
        }
        if (!scope.valid() || scope.get_type() == sol::type::lua_nil) return {};
    }

    std::vector<std::string> rest;
    for (const std::string& name : MembersOf(scope))
        if (name.size() > partial.size() && name.starts_with(partial)) rest.push_back(name.substr(partial.size()));
    std::ranges::sort(rest);
    rest.erase(std::ranges::unique(rest).begin(), rest.end());
    return rest;
}
} // namespace Arcbit
