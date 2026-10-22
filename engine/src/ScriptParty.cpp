// The party for scripts: who's in it, their stats and equipment, joining
// and leaving, using items (docs/items.md#the-party).

#include "ScriptState.h"

#include <arcbit/core/Loc.h>
#include <arcbit/items/Party.h>

#include <stdexcept>

namespace Arcbit
{
// Party.Members() -> { "hero", "ayla" } (the leader first); Party.Get(id) ->
// { Id, Name, Bag, Equipment = { weapon = "iron_sword" } }; Party.Stats(id)
// -> { HP = 100, ATK = 15, ... } (with equipment); Join, Leave, Equip(id,
// item, slot), Unequip(id, slot); Use(item, member, bag) -> true, or false
// and why ("not_in_bag", "not_usable", "no_member", "no_effect").
void ScriptEngine::State::BindParty()
{
    const auto party = [this]() -> Party& {
        if (!Services.Members) throw std::runtime_error("Party: no party here");
        return *Services.Members;
    };
    sol::table api = Lua.create_table();
    api.set_function("Members", [this, party] {
        sol::table ids = Lua.create_table();
        for (const PartyMember& member : party().GetMembers()) ids.add(member.Id);
        return ids;
    });
    api.set_function("Leader", [party]() -> std::string {
        const PartyMember* leader = party().GetLeader();
        return leader ? leader->Id : std::string{};
    });
    api.set_function("Get", [this, party](const std::string& id) -> sol::object {
        const PartyMember* member = party().Find(id);
        if (!member) return sol::make_object(Lua, sol::lua_nil);
        const CharacterDef* def = party().FindCharacter(id);
        return sol::object(Lua.create_table_with("Id", member->Id, "Name", def ? Loc::Get(def->NameKey) : member->Id, "Bag",
                                                 member->Bag, "Equipment", sol::as_table(member->Equipment)));
    });
    api.set_function("Stats", [party](const std::string& id) { return sol::as_table(party().GetStats(id)); });
    api.set_function("Join", [party](const std::string& id) { return party().Join(id); });
    api.set_function("Leave", [party](const std::string& id) { return party().Leave(id); });
    api.set_function("Equip", [party](const std::string& id, const std::string& item, const sol::optional<std::string> slot) {
        return party().Equip(id, item, slot.value_or(""));
    });
    api.set_function("Unequip", [party](const std::string& id, const std::string& slot) { return party().Unequip(id, slot); });
    api.set_function("Use", [this](const std::string& item, const sol::optional<std::string> member, const sol::optional<std::string> bag) {
        const std::string   result = Services.UseItem ? Services.UseItem(item, member.value_or(""), bag.value_or("party")) : "no_member";
        sol::variadic_results out;
        out.push_back(sol::make_object(Lua, result == "used"));
        if (result != "used") out.push_back(sol::make_object(Lua, result));
        return out;
    });
    AddGlobal("Party", api);
}
} // namespace Arcbit
