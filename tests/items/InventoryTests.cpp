// Inventory, list kinds (docs/items.md#inventories): stacking, slots,
// pockets, carry weight, arranging, sorting, filtering, quick slots, saving.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/items/Inventory.h>
#include <arcbit/items/ItemLibrary.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
// A library of a few items, its English in Loc for the life of the fixture.
struct Items
{
    ArcbitTests::TempDir dir;
    ItemLibrary          library;

    Items()
    {
        Loc::Clear();
        (void)dir.Write("a.arcitems", R"({
            "potion": { "name": "Potion", "type": "consumable", "category": "medicine", "stack": 10, "weight": 0.5, "value": 25 },
            "ether":  { "name": "Ether", "type": "consumable", "category": "medicine", "stack": 10, "weight": 0.5, "value": 60 },
            "sword":  { "name": "Sword", "type": "equipment", "slot": "weapon", "stack": 1, "weight": 6, "value": 120 },
            "boot":   { "name": "Old Boot", "tags": ["junk"], "stack": 5, "weight": 1.5, "value": 1 },
            "arrow":  { "name": "Arrow", "category": "ammo", "stack": 50, "weight": 0.1 } })");
        library.LogIssues = false;
        REQUIRE(library.AddFolder(dir.File("")) == 5);
    }
    ~Items() { Loc::Clear(); }
};

std::vector<std::string> Order(const Inventory& bag)
{
    std::vector<std::string> items;
    for (const ItemStack& stack : bag.GetStacks()) items.push_back(stack.Item + ":" + std::to_string(stack.Count));
    return items;
}
} // namespace

TEST_CASE("A plain list stacks items up to their size, then starts another stack", "[items][inventory]")
{
    Items     items;
    Inventory bag(items.library, { .Slots = 3 });
    CHECK(bag.Add("potion", 14) == 14);
    CHECK(Order(bag) == std::vector<std::string>{ "potion:10", "potion:4" });
    CHECK(bag.Add("sword") == 1);
    CHECK(bag.Add("potion", 20) == 6); // fills the second stack; no slot left for a third
    CHECK(bag.Count("potion") == 20);
    CHECK(bag.Room("ether", 1) == 0);
    CHECK(bag.Add("nothing") == 0);    // not an item

    CHECK(bag.Remove("potion", 12) == 12); // newest stacks first
    CHECK(Order(bag) == std::vector<std::string>{ "potion:8", "sword:1" });
    CHECK(bag.Has("potion", 8));
    CHECK_FALSE(bag.Has("potion", 9));
}

TEST_CASE("Pockets keep one entry per item, up to its stack size", "[items][inventory]")
{
    Items     items;
    Inventory bag(items.library, InventoryRules::Pockets());
    CHECK(bag.Add("potion", 7) == 7);
    CHECK(bag.Add("potion", 7) == 3); // capped at 10, no second entry
    CHECK(bag.Add("arrow", 60) == 50);
    CHECK(Order(bag) == std::vector<std::string>{ "potion:10", "arrow:50" });
    CHECK_FALSE(bag.Split(0, 2)); // one entry per item

    // ...for what stacks: swords don't, so each has an entry of its own.
    CHECK(bag.Add("sword", 2) == 2);
    CHECK(Order(bag) == std::vector<std::string>{ "potion:10", "arrow:50", "sword:1", "sword:1" });
    CHECK(bag.Remove("sword") == 1);

    // A list per category: the screen filters by it.
    CHECK(bag.Filter({ .Category = "medicine" }) == std::vector<u32>{ 0 });
    CHECK(bag.Filter({ .Category = "ammo" }) == std::vector<u32>{ 1 });
}

TEST_CASE("A weight bag takes any count, until the carry weight is reached", "[items][inventory]")
{
    Items     items;
    Inventory bag(items.library, InventoryRules::Weight(10.0f));
    CHECK(bag.Add("arrow", 80) == 80); // past arrows' stack size of 50: one entry
    CHECK(Order(bag) == std::vector<std::string>{ "arrow:80" });
    CHECK(bag.GetWeight() == 8.0f);
    CHECK(bag.Add("sword") == 0);       // 6 more won't fit (refused by default)
    CHECK(bag.Add("boot", 3) == 1);     // 1.5 fits once
    CHECK_FALSE(bag.IsOverWeight());

    InventoryRules rules = InventoryRules::Weight(10.0f);
    rules.OverWeight     = OverWeightRule::Allow; // a project that lets the player be encumbered
    Inventory heavy(items.library, rules);
    CHECK(heavy.Add("sword", 2) == 2);
    CHECK(heavy.IsOverWeight());
}

TEST_CASE("Entries move, split and merge", "[items][inventory]")
{
    Items     items;
    Inventory bag(items.library, {});
    (void)bag.Add("potion", 8);
    (void)bag.Add("sword");
    CHECK(bag.Split(0, 3));
    CHECK(Order(bag) == std::vector<std::string>{ "potion:5", "potion:3", "sword:1" });
    CHECK(bag.Move(2, 0));
    CHECK(Order(bag) == std::vector<std::string>{ "sword:1", "potion:5", "potion:3" });
    CHECK(bag.Merge(2, 1) == 3);
    CHECK(Order(bag) == std::vector<std::string>{ "sword:1", "potion:8" });
    CHECK(bag.Merge(0, 1) == 0); // different items
    CHECK(bag.RemoveAt(1, 2) == 2);
    CHECK(bag.Count("potion") == 6);
}

TEST_CASE("Sorting orders by any key, junk always last", "[items][inventory]")
{
    Items     items;
    Inventory bag(items.library, {});
    for (const char* item : { "sword", "boot", "potion", "ether", "arrow" }) (void)bag.Add(item, 1);

    bag.Sort(InventorySort::Name);
    CHECK(Order(bag) == std::vector<std::string>{ "arrow:1", "ether:1", "potion:1", "sword:1", "boot:1" });
    bag.Sort(InventorySort::Value, true);
    CHECK(Order(bag) == std::vector<std::string>{ "sword:1", "ether:1", "potion:1", "arrow:1", "boot:1" });
    bag.Sort(InventorySort::Recent, true); // newest first
    CHECK(Order(bag) == std::vector<std::string>{ "arrow:1", "ether:1", "potion:1", "sword:1", "boot:1" });
    bag.Sort(InventorySort::Category);
    CHECK(Order(bag).front() == "arrow:1"); // "ammo"
}

TEST_CASE("Filters keep entries by category, type, tag or name", "[items][inventory]")
{
    Items     items;
    Inventory bag(items.library, {});
    for (const char* item : { "sword", "boot", "potion", "ether" }) (void)bag.Add(item, 1);
    CHECK(bag.Filter({ .Type = "consumable" }) == std::vector<u32>{ 2, 3 });
    CHECK(bag.Filter({ .Tag = "junk" }) == std::vector<u32>{ 1 });
    CHECK(bag.Filter({ .Text = "OT" }) == std::vector<u32>{ 1, 2 }); // "Old Boot", "Potion": any case
    CHECK(bag.Filter({ .Type = "consumable", .Text = "eth" }) == std::vector<u32>{ 3 });
}

TEST_CASE("Quick slots refer to items, and everything saves", "[items][inventory]")
{
    Items     items;
    Inventory bag(items.library, { .Kind = "list", .Slots = 8, .QuickSlots = 3 });
    std::vector<std::string> changes;
    bag.OnChange = [&](const std::string& item, const i64 delta) { changes.push_back(item + std::to_string(delta)); };
    (void)bag.Add("potion", 4);
    (void)bag.Add("ether", 2);
    CHECK(bag.SetQuickSlot(0, "potion"));
    CHECK_FALSE(bag.SetQuickSlot(3, "ether"));   // only 3 slots
    CHECK_FALSE(bag.SetQuickSlot(1, "nothing")); // not an item
    (void)bag.Remove("potion", 4);
    CHECK(bag.GetQuickSlots()[0] == "potion"); // kept with none left: it shows 0
    CHECK(changes == std::vector<std::string>{ "potion4", "ether2", "potion-4" });

    Inventory loaded(items.library, {});
    loaded.FromJson(bag.ToJson());
    CHECK(loaded.GetRules().Slots == 8); // the bag's own rules come back with it
    CHECK(loaded.GetQuickSlots() == std::vector<std::string>{ "potion", "", "" });
    CHECK(Order(loaded) == Order(bag));
    CHECK(loaded.Add("potion") == 1);
    CHECK(loaded.GetStacks().back().Acquired > loaded.GetStacks().front().Acquired); // the clock carries on

    nlohmann::json json = bag.ToJson();
    json["stacks"].push_back({ { "item", "removed_in_an_update" }, { "count", 2 } });
    loaded.FromJson(json);
    CHECK(loaded.Count("removed_in_an_update") == 0); // dropped, logged
}
