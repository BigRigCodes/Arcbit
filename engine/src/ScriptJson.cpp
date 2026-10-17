// JSON <-> Lua values: script properties and saved state come from and go to
// JSON (scene files, saves).

#include "ScriptState.h"

#include <arcbit/core/Math.h>

#include <cmath>

namespace Arcbit
{
sol::object JsonToLua(sol::state_view lua, const nlohmann::json& value)
{
    switch (value.type()) {
        case nlohmann::json::value_t::boolean: return sol::make_object(lua, value.get<bool>());
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned: return sol::make_object(lua, value.get<i64>());
        case nlohmann::json::value_t::number_float: return sol::make_object(lua, value.get<f64>());
        case nlohmann::json::value_t::string: return sol::make_object(lua, value.get<std::string>());
        case nlohmann::json::value_t::array: {
            sol::table table = lua.create_table(static_cast<int>(value.size()), 0);
            for (size_t i = 0; i < value.size(); ++i) table[i + 1] = JsonToLua(lua, value[i]);
            return table;
        }
        case nlohmann::json::value_t::object: {
            sol::table table = lua.create_table(0, static_cast<int>(value.size()));
            for (const auto& [key, item] : value.items()) table[key] = JsonToLua(lua, item);
            return table;
        }
        default: return sol::make_object(lua, sol::lua_nil);
    }
}

namespace
{
    // Keys 1..n and nothing else: a JSON array.
    bool IsSequence(const sol::table& table)
    {
        size_t count = 0;
        for (const auto& [key, item] : table) {
            if (key.get_type() != sol::type::number) return false;
            ++count;
        }
        return count == table.size();
    }

    nlohmann::json NumberToJson(const sol::object& value)
    {
        const f64 number = value.as<f64>();
        if (std::floor(number) == number && std::abs(number) < 9.0e15) return static_cast<i64>(number);
        return number;
    }
} // namespace

nlohmann::json LuaToJson(const sol::object& value)
{
    switch (value.get_type()) {
        case sol::type::boolean: return value.as<bool>();
        case sol::type::number: return NumberToJson(value);
        case sol::type::string: return value.as<std::string>();
        case sol::type::table: {
            const sol::table table = value;
            if (table.size() > 0 && IsSequence(table)) {
                nlohmann::json array = nlohmann::json::array();
                for (size_t i = 1; i <= table.size(); ++i) array.push_back(LuaToJson(table[i]));
                return array;
            }
            nlohmann::json object = nlohmann::json::object();
            for (const auto& [key, item] : table)
                if (key.get_type() == sol::type::string) object[key.as<std::string>()] = LuaToJson(item);
                else if (key.get_type() == sol::type::number) object[std::to_string(key.as<i64>())] = LuaToJson(item);
            return object;
        }
        default:
            if (value.is<Vec2>()) { // a position in a component's fields
                const Vec2 v = value.as<Vec2>();
                return nlohmann::json::array({ v.X, v.Y });
            }
            return nullptr;
    }
}
} // namespace Arcbit
