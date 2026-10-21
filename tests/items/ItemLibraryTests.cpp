// ItemLibrary (docs/items.md): .arcitems files read into item definitions,
// "extends" across files, the checks, the English into Loc, reloads - and
// arcbit-loc's view of item files.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/core/LocCheck.h>
#include <arcbit/items/ItemLibrary.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace Arcbit;

namespace
{
struct LocScope
{
    LocScope() { Loc::Clear(); }
    ~LocScope() { Loc::Clear(); }
};

ItemLibrary MakeLibrary()
{
    ItemLibrary library;
    library.LogIssues                  = false;
    library.ItemRules.StackByType      = { { "consumable", 99 }, { "equipment", 1 } };
    library.ItemRules.Stack            = 50;
    library.ItemRules.SlotKinds        = { "weapon", "head", "accessory" };
    return library;
}

// The issues about one item, as "error: ..." / "warning: ...".
std::vector<std::string> IssuesOf(const ItemLibrary& library, const std::string_view item)
{
    std::vector<std::string> found;
    for (const ItemIssue& issue : library.GetIssues())
        if (issue.Item == item) found.push_back(std::string(issue.Error ? "error: " : "warning: ") + issue.Message);
    return found;
}

const char* Potions = R"({
    "potion_base": { "abstract": true, "type": "consumable", "category": "medicine", "weight": 0.5,
                     "stats": { "HP": 10, "MP": 5 } },
    "potion":    { "extends": "potion_base", "name": "Potion", "description": "Restores 50 HP.",
                   "value": 25, "use": { "stats": { "HP": 50 } } },
    "hi_potion": { "extends": "potion", "name": "Hi-Potion", "stats": { "HP": 30, "MP": null }, "tags": ["rare"] }
})";

const char* Gear = R"({
    // comments are fine
    "iron_sword": { "name": "Iron Sword", "type": "equipment", "slot": "weapon", "size": [1, 2],
                    "stats": { "ATK": 5, "SPD%": -5 }, "icon": "icons/sword.png" },
    "old_boot":   { "name": "Old Boot", "tags": ["junk"], "rotatable": false }
})";
} // namespace

TEST_CASE("Items read from their files, with the project's defaults", "[items]")
{
    LocScope                   scope;
    const ArcbitTests::TempDir dir;
    (void)dir.Write("potions.arcitems", Potions);
    std::filesystem::create_directories(dir.File("gear"));
    (void)dir.Write("gear/gear.arcitems", Gear);
    ItemLibrary library = MakeLibrary();
    REQUIRE(library.AddFolder(dir.File("")) == 4); // potion_base is only for extending

    const ItemDef& sword = *library.Find("iron_sword");
    CHECK(sword.Type == "equipment");
    CHECK(sword.Category == "equipment"); // its type
    CHECK(sword.Stack == 1);              // equipment's default
    CHECK((sword.Width == 1 && sword.Height == 2 && sword.Rotatable));
    CHECK(sword.Stats.at("SPD%") == -5.0f);
    CHECK(sword.Icon == "icons/sword.png");

    const ItemDef& boot = *library.Find("old_boot");
    CHECK(boot.Type == "material"); // no type: material
    CHECK(boot.Stack == 50);        // a type not listed: the project's other
    CHECK(boot.IsJunk());
    CHECK_FALSE(boot.Rotatable);

    CHECK(Loc::Get("items.potion.name") == "Potion"); // English, keyed by id
    CHECK(Loc::Get("items.potion.description") == "Restores 50 HP.");
    CHECK(library.GetIds() == std::vector<std::string>{ "hi_potion", "iron_sword", "old_boot", "potion" });
    CHECK(library.GetFile("iron_sword").filename() == "gear.arcitems");
    CHECK(library.GetIssues().empty());
}

TEST_CASE("An item builds on another, field by field", "[items]")
{
    LocScope                   scope;
    const ArcbitTests::TempDir dir;
    (void)dir.Write("potions.arcitems", Potions);
    ItemLibrary library = MakeLibrary();
    (void)library.AddFolder(dir.File(""));

    CHECK_FALSE(library.Has("potion_base"));
    const ItemDef& potion = *library.Find("potion");
    CHECK(potion.Type == "consumable"); // from potion_base
    CHECK(potion.Category == "medicine");
    CHECK(potion.Stack == 99);
    CHECK(potion.Weight == 0.5f);
    CHECK(potion.Use["stats"]["HP"] == 50);

    const ItemDef& hi = *library.Find("hi_potion");
    CHECK(hi.Name == "Hi-Potion");
    CHECK(hi.Value == 25);                // from potion
    CHECK(hi.Stats.at("HP") == 30.0f);    // its own over the chain's
    CHECK_FALSE(hi.Stats.contains("MP")); // null removes
    CHECK(hi.HasTag("rare"));
}

TEST_CASE("Item files are checked as they're read", "[items]")
{
    LocScope                   scope;
    const ArcbitTests::TempDir dir;
    (void)dir.Write("a.arcitems", R"({
        "dup":     { "name": "First" },
        "nameless": { "type": "key" },
        "orphan":  { "name": "Orphan", "extends": "nothing" },
        "loop_a":  { "name": "A", "extends": "loop_b" },
        "loop_b":  { "name": "B", "extends": "loop_a" },
        "big":     { "name": "Big", "size": [0, 2], "stack": -1 },
        "typo":    { "name": "Typo", "wieght": 2 },
        "bare":    { "name": "Bare", "type": "equipment" },
        "ring":    { "name": "Ring", "type": "equipment", "slot": "finger", "icon": "nope.png" } })");
    (void)dir.Write("b.arcitems", R"({ "dup": { "name": "Second" } })");
    (void)dir.Write("c.arcitems", "{ not json");
    ItemLibrary library   = MakeLibrary();
    library.ItemRules.AssetRoot = dir.File("");
    (void)library.AddFolder(dir.File(""));

    CHECK(library.Find("dup")->Name == "First"); // the first keeps the id
    CHECK(IssuesOf(library, "dup").size() == 1);
    CHECK(IssuesOf(library, "nameless") == std::vector<std::string>{ "error: it needs a \"name\"" });
    CHECK_FALSE(library.Has("orphan"));
    CHECK(IssuesOf(library, "orphan") == std::vector<std::string>{ "error: extends 'nothing', which isn't an item" });
    CHECK_FALSE(library.Has("loop_a"));
    CHECK(IssuesOf(library, "big").size() == 2); // the size and the stack
    CHECK(library.Find("big")->Stack == 1);
    CHECK(IssuesOf(library, "typo") == std::vector<std::string>{ "warning: unknown field 'wieght'" });
    CHECK(IssuesOf(library, "bare") == std::vector<std::string>{ "warning: equipment without a \"slot\" can't be worn" });
    CHECK(IssuesOf(library, "ring") == std::vector<std::string>{ "warning: no equipment slot takes 'finger'",
                                                                 "warning: icon 'nope.png' not found" });
    CHECK(std::ranges::any_of(library.GetIssues(), [](const ItemIssue& i) { return i.Item.empty() && i.Error; })); // c
}

TEST_CASE("Edited item files read again", "[items]")
{
    LocScope                   scope;
    const ArcbitTests::TempDir dir;
    const std::string          file = dir.Write("a.arcitems", R"({ "apple": { "name": "Apple" } })");
    ItemLibrary                library = MakeLibrary();
    (void)library.AddFolder(dir.File(""));
    CHECK(library.CheckReloads() == 0);

    (void)dir.Write("a.arcitems", R"({ "apple": { "name": "Red Apple" }, "pear": { "name": "Pear" } })");
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(2));
    CHECK(library.CheckReloads() == 1);
    CHECK(library.Find("apple")->Name == "Red Apple");
    CHECK(library.Has("pear"));
    CHECK(Loc::Get("items.pear.name") == "Pear");

    library.Clear();
    CHECK_FALSE(library.Has("apple"));
    CHECK_FALSE(Loc::Has("items.apple.name"));
}

TEST_CASE("arcbit-loc counts item names and reports item problems", "[items][loc]")
{
    LocScope                   scope;
    const ArcbitTests::TempDir dir;
    for (const char* sub : { "locale", "items" }) std::filesystem::create_directories(dir.File(sub));
    (void)dir.Write("locale/en.json", R"({ "$locale": { "name": "English" } })");
    (void)dir.Write("locale/fr.json", R"({ "$locale": { "name": "Francais" }, "items.potion.name": "Potion" })");
    (void)dir.Write("items/potions.arcitems", Potions);
    (void)dir.Write("items/bad.arcitems", R"({ "broken": { "type": "key" } })");
    const std::vector<std::filesystem::path> locales = { dir.File("locale") }, items = { dir.File("items") };

    const LocCheck::Report report = LocCheck::Check(locales, {}, {}, items);
    CHECK(report.HasItemErrors()); // broken has no name
    const auto french = std::ranges::find(report.Languages, "fr", &LocCheck::Coverage::Code);
    REQUIRE(french != report.Languages.end());
    CHECK(french->Translated == 1);
    CHECK(french->Total == 4); // the potions' names and descriptions
    CHECK(LocCheck::Template().find("items.hi_potion.name") != std::string::npos);
}
