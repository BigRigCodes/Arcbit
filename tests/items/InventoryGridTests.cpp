// Inventory, grid kinds (docs/items.md#grids): sized items placed in cells
// and turned to fit, sections, moving and swapping, repacking sorts,
// changing a bag into another kind, saving.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/items/Inventory.h>
#include <arcbit/items/ItemLibrary.h>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
struct Items
{
    ArcbitTests::TempDir dir;
    ItemLibrary          library;

    Items()
    {
        Loc::Clear();
        (void)dir.Write("a.arcitems", R"({
            "potion": { "name": "Potion", "stack": 10, "weight": 0.5 },
            "gem":    { "name": "Gem", "stack": 1 },
            "sword":  { "name": "Sword", "type": "equipment", "slot": "weapon", "stack": 1, "size": [1, 2], "weight": 6 },
            "shield": { "name": "Shield", "type": "equipment", "slot": "offhand", "stack": 1, "size": [2, 2], "weight": 4 },
            "plank":  { "name": "Plank", "stack": 1, "size": [2, 1], "rotatable": false },
            "arrow":  { "name": "Arrow", "category": "ammo", "stack": 50 },
            "boot":   { "name": "Boot", "tags": ["junk"], "stack": 1 } })");
        library.LogIssues = false;
        REQUIRE(library.AddFolder(dir.File("")) == 7);
    }
    ~Items() { Loc::Clear(); }
};

// "item@section:x,y" (r for turned) for every stack, in stack order.
std::vector<std::string> Layout(const Inventory& bag)
{
    std::vector<std::string> out;
    for (const ItemStack& s : bag.GetStacks())
        out.push_back(std::format("{}@{}:{},{}{}", s.Item, s.Section, s.X, s.Y, s.Rotated ? "r" : ""));
    return out;
}
} // namespace

TEST_CASE("A grid places items by size, first space first, turned when that's how they fit", "[items][inventory][grid]")
{
    Items     items;
    Inventory bag(items.library, InventoryRules::Grid(3, 2));
    CHECK(bag.Add("sword") == 1);      // 1x2: the first column
    CHECK(bag.Add("potion", 15) == 15); // 10 and 5: two cells
    CHECK(Layout(bag) == std::vector<std::string>{ "sword@:0,0", "potion@:1,0", "potion@:2,0" });
    CHECK(bag.Add("sword") == 1);  // two free cells left, side by side: it turns to fit
    CHECK(Layout(bag).back() == "sword@:1,1r");
    CHECK(bag.Room("gem", 1) == 0); // full

    Inventory narrow(items.library, InventoryRules::Grid(1, 2));
    CHECK(narrow.Add("plank") == 0); // 2 wide, and it can't turn
    CHECK(narrow.Add("sword") == 1); // upright

    CHECK(bag.StackAt("", 0, 1) == 0u); // the first sword covers both its cells
    CHECK(bag.StackAt("", 2, 1) == 3u);
    u32 w = 0, h = 0;
    bag.Footprint(3, w, h);
    CHECK((w == 2 && h == 1));
}

TEST_CASE("Sections take what the main grid can't, and may take only one category", "[items][inventory][grid]")
{
    Items          items;
    InventoryRules rules = InventoryRules::Grid(1, 1);
    rules.Sections       = { { .Name = "hotbar", .Width = 2, .Height = 1 }, { .Name = "quiver", .Width = 1, .Height = 1, .Category = "ammo" } };
    Inventory bag(items.library, rules);
    CHECK(bag.Add("gem", 3) == 3); // the grid, then the hotbar's two cells
    CHECK(Layout(bag) == std::vector<std::string>{ "gem@:0,0", "gem@hotbar:0,0", "gem@hotbar:1,0" });
    CHECK(bag.Add("gem") == 0);     // the quiver only takes ammo
    CHECK(bag.Add("arrow", 20) == 20);
    CHECK(Layout(bag).back() == "arrow@quiver:0,0");
    CHECK_FALSE(bag.MoveTo(0, "quiver", 0, 0, false));
}

TEST_CASE("Entries move, turn and swap where they fit", "[items][inventory][grid]")
{
    Items     items;
    Inventory bag(items.library, InventoryRules::Grid(3, 3));
    (void)bag.Add("sword"); // (0,0) upright
    (void)bag.Add("gem");   // (1,0)
    CHECK_FALSE(bag.MoveTo(0, "", 1, 0, false)); // the gem is there
    CHECK(bag.MoveTo(0, "", 2, 1, false));
    CHECK_FALSE(bag.Rotate(0)); // turned it would reach past the edge
    CHECK(bag.MoveTo(0, "", 0, 2, true)); // turned, along the bottom row
    CHECK_FALSE(bag.MoveTo(0, "", 9, 9, false));

    (void)bag.Add("plank"); // 2x1 lands at (0,1)
    CHECK(Layout(bag) == std::vector<std::string>{ "sword@:0,2r", "gem@:1,0", "plank@:0,1" });
    CHECK(bag.Swap(1, 2)); // the gem to (0,1), the plank to (1,0) - each fits
    CHECK(Layout(bag) == std::vector<std::string>{ "sword@:0,2r", "gem@:0,1", "plank@:1,0" });
    CHECK(bag.MoveTo(1, "", 2, 2, false));
    CHECK_FALSE(bag.Swap(2, 1)); // the plank at (2,2) would reach past the edge
}

TEST_CASE("Sorting a grid repacks it in order, or largest-first when the order won't pack", "[items][inventory][grid]")
{
    Items          items;
    InventoryRules rules = InventoryRules::Grid(3, 2);
    rules.Sections       = { { .Name = "hotbar", .Width = 1, .Height = 1 } };
    Inventory bag(items.library, rules);
    (void)bag.Add("shield"); // 2x2 at (0,0)
    (void)bag.Add("potion");
    (void)bag.Add("gem");
    (void)bag.Add("arrow"); // grid full: the hotbar
    CHECK(Layout(bag).back() == "arrow@hotbar:0,0");

    // By name, Gem and Potion would take the top row first and leave no 2x2
    // space: it packs largest-first instead.
    CHECK(bag.Sort(InventorySort::Name));
    CHECK(Layout(bag) == std::vector<std::string>{ "shield@:0,0", "arrow@hotbar:0,0", "gem@:2,0", "potion@:2,1" });

    Inventory list(items.library, InventoryRules::Grid(3, 2));
    (void)list.Add("potion");
    (void)list.Add("gem");
    CHECK(list.Sort(InventorySort::Name));
    CHECK(Layout(list) == std::vector<std::string>{ "gem@:0,0", "potion@:1,0" }); // reading order
}

TEST_CASE("Changing bags keeps everything that fits, and says what doesn't - junk first", "[items][inventory][grid]")
{
    Items     items;
    Inventory bag(items.library, InventoryRules::Grid(3, 2));
    for (const char* item : { "boot", "gem", "potion", "sword" }) (void)bag.Add(item);
    CHECK(Layout(bag) == std::vector<std::string>{ "boot@:0,0", "gem@:1,0", "potion@:2,0", "sword@:0,1r" });
    const std::vector<std::string> before = Layout(bag);

    SECTION("a bigger grid: everything where it was")
    {
        CHECK(bag.ConvertOverflow(InventoryRules::Grid(4, 4)).empty());
        CHECK(bag.Convert(InventoryRules::Grid(4, 4)));
        CHECK(Layout(bag) == before);
        CHECK(bag.GetRules().Width == 4);
    }
    SECTION("a smaller grid: the junk is what's left out")
    {
        const std::vector<ItemStack> overflow = bag.ConvertOverflow(InventoryRules::Grid(2, 2)); // 5 cells' worth in 4
        REQUIRE(overflow.size() == 1);
        CHECK(overflow[0].Item == "boot");
        CHECK_FALSE(bag.Convert(InventoryRules::Grid(2, 2))); // not until it's discarded
        CHECK(bag.Count("boot") == 1);
        CHECK(bag.Remove("boot") == 1);
        CHECK(bag.Convert(InventoryRules::Grid(2, 2)));
        for (const char* item : { "gem", "potion", "sword" }) CHECK(bag.Count(item) == 1);
    }
    SECTION("a weight limit where there was none")
    {
        const auto overflow = bag.ConvertOverflow(InventoryRules::Weight(1.0f));
        REQUIRE(overflow.size() == 1);
        CHECK(overflow[0].Item == "sword"); // 6 is too heavy; the rest weighs 0.5
    }
    SECTION("a packing that keeps more beats one that keeps places")
    {
        Inventory spread(items.library, InventoryRules::Grid(3, 2));
        (void)spread.Add("gem");
        (void)spread.Add("plank");
        CHECK(spread.MoveTo(1, "", 0, 1, false)); // the plank below, the gem mid-row
        CHECK(spread.MoveTo(0, "", 1, 0, false));
        // Into one row: where they were, the gem splits it; packed fresh, both fit.
        CHECK(spread.ConvertOverflow(InventoryRules::Grid(3, 1)).empty());
        CHECK(spread.Convert(InventoryRules::Grid(3, 1)));
        CHECK(Layout(spread) == std::vector<std::string>{ "gem@:2,0", "plank@:0,0" }); // the bag's order kept
    }
    SECTION("pockets: positions don't matter any more")
    {
        CHECK(bag.Convert(InventoryRules::Pockets()));
        CHECK(bag.Count("boot") == 1);
    }
}

TEST_CASE("A grid saves where everything lies, and re-places what no longer fits", "[items][inventory][grid]")
{
    Items          items;
    InventoryRules rules = InventoryRules::Survival(3, 3, 50.0f);
    rules.Sections       = { { .Name = "hotbar", .Width = 3, .Height = 1 } };
    Inventory bag(items.library, rules);
    (void)bag.Add("sword");
    (void)bag.Add("gem");
    CHECK(bag.MoveTo(1, "hotbar", 2, 0, false));

    Inventory loaded(items.library, {});
    loaded.FromJson(bag.ToJson());
    CHECK(loaded.GetRules().Kind == "survival");
    REQUIRE(loaded.GetRules().Sections.size() == 1);
    CHECK(loaded.GetRules().MaxWeight == 50.0f);
    CHECK(Layout(loaded) == Layout(bag));

    nlohmann::json json          = bag.ToJson();
    json["stacks"][1]["x"]       = 0; // the gem saved on top of the sword (as if the sword grew in an update)
    json["stacks"][1]["section"] = "";
    loaded.FromJson(json);
    CHECK(loaded.Count("gem") == 1);
    CHECK(Layout(loaded).back() == "gem@:1,0"); // moved to the first free cell
}
