// Save and Event for scripts: the game's save data and saving / loading, and
// the event bus - payloads as plain tables, subscriptions owned by the script
// that made them.

#include "ScriptState.h"

#include <arcbit/core/Log.h>
#include <arcbit/scene/Scene.h>

#include <format>

namespace Arcbit
{
namespace
{
    // Lua 5.4 keeps integers apart from floats: 3 stays an integer, 3.0 doesn't.
    bool IsInteger(const sol::object& value)
    {
        lua_State* L = value.lua_state();
        value.push(L);
        const bool integer = lua_isinteger(L, -1);
        lua_pop(L, 1);
        return integer;
    }

    // 3 -> manual slot 3; "quick"; "auto" (with an index to load, else the
    // next autosave slot to write).
    SaveSlot ToSlot(const sol::object& slot, const sol::optional<u32> index, const SaveManager* saves)
    {
        if (slot.get_type() == sol::type::number) return SaveSlot::Manual(slot.as<u32>());
        const std::string kind = slot.get_type() == sol::type::string ? slot.as<std::string>() : "";
        if (kind == "quick") return SaveSlot::Quick();
        if (kind == "auto") return index ? SaveSlot::Auto(*index) : (saves ? saves->NextAutoSlot() : SaveSlot::Auto(1));
        throw std::runtime_error("save slots are a number, \"quick\" or \"auto\"");
    }
} // namespace

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

// Save.Get / Set / Has / Erase on SaveData, and Save.To / Load / Auto.
void ScriptEngine::State::BindSave()
{
    const auto data = [this]() -> SaveData& {
        if (!Services.Saves) throw std::runtime_error("Save: no save manager");
        return Services.Saves->Data();
    };
    sol::table save = Lua.create_table();
    save.set_function("Get", [this, data](const std::string& key, const sol::object fallback) -> sol::object {
        const SaveValue* value = data().Find(key);
        if (!value) return fallback;
        return std::visit([this](const auto& v) { return sol::make_object(Lua, v); }, *value);
    });
    // Set(key, nil) erases it.
    save.set_function("Set", [data](const std::string& key, const sol::object value) {
        switch (value.get_type()) {
            case sol::type::lua_nil: data().Erase(key); return;
            case sol::type::boolean: data().Set(key, value.as<bool>()); return;
            case sol::type::string: data().Set(key, value.as<std::string>()); return;
            case sol::type::number:
                if (IsInteger(value)) data().Set(key, value.as<i64>());
                else data().Set(key, value.as<f64>());
                return;
            default:
                if (value.is<Vec2>()) {
                    data().Set(key, value.as<Vec2>());
                    return;
                }
                throw std::runtime_error(std::format("Save.Set('{}'): saves keep booleans, numbers, text and Vec2", key));
        }
    });
    save.set_function("Has", [data](const std::string& key) { return data().Has(key); });
    save.set_function("Erase", [data](const std::string& key) { data().Erase(key); });
    save.set_function("To", [this](const sol::object slot, const sol::optional<u32> index) {
        return Services.SaveTo && Services.SaveTo(ToSlot(slot, index, Services.Saves));
    });
    save.set_function("Load", [this](const sol::object slot, const sol::optional<u32> index) {
        return Services.LoadFrom && Services.LoadFrom(ToSlot(slot, index, Services.Saves));
    });
    save.set_function("Auto", [this] { return Services.AutoSave && Services.AutoSave(); });
    AddGlobal("Save", save);
}

// ---------------------------------------------------------------------------
// Event
// ---------------------------------------------------------------------------

namespace
{
    // A handle for Event.Subscribe's return: sub:Unsubscribe().
    struct ScriptSubscriptionHandle
    {
        u64 Id = 0;
    };

    EventValue ToEventValue(const sol::object& value, const std::string& field)
    {
        switch (value.get_type()) {
            case sol::type::boolean: return value.as<bool>();
            case sol::type::string: return value.as<std::string>();
            case sol::type::number: return IsInteger(value) ? EventValue(value.as<i64>()) : EventValue(value.as<f64>());
            default:
                if (value.is<Vec2>()) return value.as<Vec2>();
                if (value.is<ScriptEntity>()) return value.as<ScriptEntity>().Id;
                throw std::runtime_error(std::format("Event.Publish: field '{}' isn't a boolean, number, text, Vec2 "
                                                     "or Entity", field));
        }
    }
} // namespace

void ScriptEngine::State::EndSubscriptions(const u64 owner)
{
    if (owner == 0) return;
    std::erase_if(Subscriptions, [owner](const auto& entry) { return entry.second.Owner == owner; });
    StopTasks(owner, 0);
}

// Event.Subscribe(name, fn) calls fn(payload, name); without fn, the
// subscribing instance's OnEvent(name, payload). Either ends with the script
// that subscribed (Unsubscribe ends it sooner).
void ScriptEngine::State::BindEvents()
{
    Lua.new_usertype<ScriptSubscriptionHandle>("Subscription", sol::no_constructor, "Unsubscribe",
                                               [this](const ScriptSubscriptionHandle& h) { Subscriptions.erase(h.Id); });

    sol::table event = Lua.create_table();
    event.set_function("Subscribe", [this](const std::string& name, const sol::optional<sol::protected_function> fn) {
        if (!Services.Events) throw std::runtime_error("Event.Subscribe: no event bus");
        if (!fn && !Current.Self.valid()) throw std::runtime_error("Event.Subscribe: give a function (no script to call OnEvent on)");
        const u64     id      = NextSubscriptionId++;
        ScriptContext context = Current; // the handler runs as the subscriber did
        Subscription  handle  = Services.Events->Subscribe(name, [this, context, handler = fn, name](const Event& e) {
            if (context.Scene && !Scenes.contains(context.Serial)) return; // its scene unloaded meanwhile
            const ContextScope scope(*this, context);
            sol::table         payload = Lua.create_table();
            for (const EventPayload::Field& field : e.Payload.Fields())
                payload[field.Name] = std::visit(
                    [&](const auto& v) -> sol::object {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, Entity>)
                            return sol::make_object(Lua, ScriptEntity{ v, context.Serial });
                        else return sol::make_object(Lua, v);
                    },
                    field.Value);
            const sol::protected_function_result result =
                handler ? Call(*handler, payload, name) : Call(context.Self["OnEvent"], context.Self, name, payload);
            if (!result.valid()) LOG_ERROR(Scripting, "Event handler for '{}': {}", name, result.get<sol::error>().what());
        });
        Subscriptions[id] = LuaSubscription{ .Owner = context.Owner, .Handle = std::move(handle) };
        return ScriptSubscriptionHandle{ id };
    });
    event.set_function("Publish", [this](const std::string& name, const sol::optional<sol::table> fields) {
        if (!Services.Events) throw std::runtime_error("Event.Publish: no event bus");
        EventPayload payload;
        if (fields)
            for (const auto& [key, value] : *fields) {
                const std::string field = key.as<std::string>();
                payload.Set(field, ToEventValue(value, field));
            }
        Services.Events->Publish(name, std::move(payload));
    });
    AddGlobal("Event", event);
}
} // namespace Arcbit
