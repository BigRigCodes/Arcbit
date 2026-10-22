// Items for scripts: what an item is (Items.Get), the game's bags
// (Inventory.*) and currencies (Currency.*) - docs/items.md. Application
// gives them through ScriptServices.

#include "ScriptState.h"

#include <arcbit/core/Loc.h>
#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>

#include <format>
#include <stdexcept>

namespace Arcbit
{
namespace
{
// Items.Get's table: the definition, its name and description in the
// player's language.
sol::table ItemTable(sol::state& lua, const ItemDef& def)
{
    sol::table t     = lua.create_table();
    t["Id"]          = def.Id;
    t["Name"]        = Loc::Get(def.NameKey);
    t["Description"] = def.Description.empty() ? std::string{} : Loc::Get(def.DescriptionKey);
    t["Type"]        = def.Type;
    t["Category"]    = def.Category;
    t["Tags"]        = sol::as_table(def.Tags);
    t["Stack"]       = def.Stack;
    t["Weight"]      = def.Weight;
    t["Value"]       = def.Value;
    t["Slot"]        = def.Slot;
    return t;
}
} // namespace

void ScriptEngine::State::BindItems()
{
    sol::table items = Lua.create_table();
    items.set_function("Get", [this](const std::string& id) -> sol::object {
        const ItemDef* def = Services.Items ? Services.Items->Find(id) : nullptr;
        return def ? sol::object(ItemTable(Lua, *def)) : sol::make_object(Lua, sol::lua_nil);
    });
    AddGlobal("Items", items);
    BindInventory();
    BindCurrency();
    BindParty();
}

// Inventory.Add("potion", 2) -> how many fit; Remove, Count, Has; Get() ->
// { { Item = "potion", Count = 2 }, ... }; Drop -> how many went to the
// ground. Every call takes an optional last argument, the bag ("party" when
// left out).
void ScriptEngine::State::BindInventory()
{
    const auto bag = [this](const sol::optional<std::string>& name) -> Inventory& {
        const std::string which = name.value_or(std::string(Inventories::Party));
        Inventory*        found = Services.Bags ? Services.Bags->Find(which) : nullptr;
        if (!found) throw std::runtime_error(std::format("Inventory: no bag '{}'", which));
        return *found;
    };
    sol::table inventory = Lua.create_table();
    inventory.set_function("Add", [bag](const std::string& item, const sol::optional<u32> count, const sol::optional<std::string> name) {
        return bag(name).Add(item, count.value_or(1));
    });
    inventory.set_function("Remove", [bag](const std::string& item, const sol::optional<u32> count, const sol::optional<std::string> name) {
        return bag(name).Remove(item, count.value_or(1));
    });
    inventory.set_function("Count", [bag](const std::string& item, const sol::optional<std::string> name) { return bag(name).Count(item); });
    inventory.set_function("Has", [bag](const std::string& item, const sol::optional<u32> count, const sol::optional<std::string> name) {
        return bag(name).Has(item, count.value_or(1));
    });
    inventory.set_function("Drop", [this](const std::string& item, const sol::optional<u32> count, const sol::optional<std::string> name) {
        if (!Services.DropItem) throw std::runtime_error("Inventory.Drop: no scene to drop into");
        return Services.DropItem(item, count.value_or(1), name.value_or(std::string(Inventories::Party)));
    });
    inventory.set_function("Get", [this, bag](const sol::optional<std::string> name) {
        sol::table stacks = Lua.create_table();
        for (const ItemStack& stack : bag(name).GetStacks())
            stacks.add(Lua.create_table_with("Item", stack.Item, "Count", stack.Count));
        return stacks;
    });
    AddGlobal("Inventory", inventory);
}

// Currency.Get("gold"), Currency.Add("gold", -5) -> the change made,
// Currency.Spend("gold", 5) -> true if paid (all or nothing).
void ScriptEngine::State::BindCurrency()
{
    const auto money = [this]() -> Currencies& {
        if (!Services.Money) throw std::runtime_error("Currency: no currencies here");
        return *Services.Money;
    };
    sol::table currency = Lua.create_table();
    currency.set_function("Get", [money](const std::string& id) { return money().Get(id); });
    currency.set_function("Add", [money](const std::string& id, const i64 change) { return money().Add(id, change); });
    currency.set_function("Spend", [money](const std::string& id, const i64 amount) { return money().Spend(id, amount); });
    AddGlobal("Currency", currency);
}
} // namespace Arcbit
