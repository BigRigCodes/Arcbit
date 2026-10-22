// Stats and the party (docs/items.md#stats, #the-party): the engine's stat
// set and a game's over it, effective stats with equipment, resources, the
// party file, joining and leaving, equipping, using items, saving - and Lua.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/core/LocCheck.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/items/Party.h>
#include <arcbit/items/Stats.h>
#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
const std::vector<EquipmentSlot> Slots = {
    { "weapon", "weapon" }, { "head", "head" }, { "accessory1", "accessory" }, { "accessory2", "accessory" },
};

struct World
{
    ArcbitTests::TempDir dir;
    ItemLibrary          items;
    StatSet              stats;
    Inventories          bags{ items };

    World()
    {
        Loc::Clear();
        (void)dir.Write("a.arcitems", R"({
            "sword":  { "name": "Sword", "type": "equipment", "slot": "weapon", "stats": { "ATK": 5 } },
            "axe":    { "name": "Axe", "type": "equipment", "slot": "weapon", "stats": { "ATK": 9, "SPD%": -20 } },
            "ring":   { "name": "Ring", "type": "equipment", "slot": "accessory", "stats": { "MaxHP": 20 } },
            "potion": { "name": "Potion", "type": "consumable", "use": { "stats": { "HP": 50 } } },
            "tonic":  { "name": "Tonic", "type": "consumable", "use": { "stats": { "ATK": 1 } } } })");
        items.LogIssues = false;
        REQUIRE(items.AddFolder(dir.File("")) == 5);
        bags.AddStarting("party", InventoryRules::Pockets());
    }
    ~World() { Loc::Clear(); }

    [[nodiscard]] std::vector<const ItemDef*> Defs(std::initializer_list<const char*> ids) const
    {
        std::vector<const ItemDef*> out;
        for (const char* id : ids) out.push_back(items.Find(id));
        return out;
    }
};
} // namespace

TEST_CASE("The engine's stats, a game's over them, with equipment and resources", "[items][stats]")
{
    World    w;
    StatSet& stats = w.stats;
    REQUIRE(stats.All().size() == 10);
    CHECK(stats.Find("HP")->MaxStat == "MaxHP");

    stats.Load(nlohmann::json::parse(R"({ "stats": {
        "STR": { "name": "Strength", "default": 10, "max": 20 },
        "ATK": { "default": 8 } } })"));
    CHECK(stats.All().size() == 11);
    CHECK(stats.Find("ATK")->Name == "Attack"); // only what the file names changes

    Stats       own{ .Base = { { "ATK", 12.0f }, { "SPD", 10.0f } } };
    const auto  axe = w.Defs({ "axe", "ring" });
    StatValues  now = stats.Effective(own, axe);
    CHECK(now["ATK"] == 21.0f);  // 12 + 9
    CHECK(now["SPD"] == 8.0f);   // 10 - 20%
    CHECK(now["MaxHP"] == 120.0f);
    CHECK(now["HP"] == 120.0f);  // a resource with no current value is full
    CHECK(now["STR"] == 10.0f);  // its default

    own.Current["HP"] = 30.0f;
    stats.Apply(own, { { "HP", 200.0f } }, axe); // healed past the max: capped at it
    CHECK(own.Current["HP"] == 120.0f);
    stats.Apply(own, { { "HP", -500.0f } });
    CHECK(own.Current["HP"] == 0.0f);         // not below the stat's min
    stats.Apply(own, { { "STR", 50.0f } });
    CHECK(own.Base["STR"] == 20.0f);          // a base stat: kept within its max

    StatSet replaced;
    replaced.Load(nlohmann::json::parse(R"({ "replace": true, "stats": { "Grit": { "default": 3 } } })"));
    REQUIRE(replaced.All().size() == 1);
    CHECK(replaced.Effective({})["Grit"] == 3.0f);
}

TEST_CASE("The party file says who can join and who starts", "[items][party]")
{
    World w;
    Party party(w.items, w.stats, w.bags, Slots);
    REQUIRE(party.GetMembers().size() == 1); // without a file: "player" alone
    CHECK(party.GetLeader()->Id == "player");

    std::vector<std::string> told;
    party.OnMembership = [&](const std::string& id, const bool joined) { told.push_back(id + (joined ? "+" : "-")); };
    party.Load(nlohmann::json::parse(R"({ "starting": ["hero"], "members": {
        "hero": { "name": "Hero", "stats": { "ATK": 12 }, "equipment": { "weapon": "sword" } },
        "ayla": { "name": "Ayla", "stats": { "MAG": 18 }, "bag": "ayla" } } })"));
    REQUIRE(party.GetMembers().size() == 1);
    CHECK(party.GetLeader()->Id == "hero");
    CHECK(party.GetStats("hero")["ATK"] == 17.0f); // wearing the starting sword

    CHECK(party.Join("ayla"));
    CHECK_FALSE(party.Join("ayla"));
    CHECK_FALSE(party.Join("nobody"));
    CHECK(w.bags.Find("ayla")); // her own bag
    CHECK(party.Move("ayla", 0));
    CHECK(party.GetLeader()->Id == "ayla");
    CHECK(party.Leave("hero"));
    CHECK(w.bags.GetParty().Count("sword") == 1); // what they wore stays with the party
    CHECK(told == std::vector<std::string>{ "hero+", "ayla+", "hero-" });
}

TEST_CASE("Equipping moves items between the bag and a member's slots", "[items][party]")
{
    World      w;
    Party      party(w.items, w.stats, w.bags, Slots);
    Inventory& bag = w.bags.GetParty();
    for (const char* item : { "sword", "axe", "ring", "ring", "ring" }) (void)bag.Add(item);
    std::vector<std::string> told;
    party.OnEquip = [&](const std::string&, const std::string& item, const std::string& slot, const bool on) {
        told.push_back(item + (on ? ">" : "<") + slot);
    };

    CHECK(party.Equip("player", "sword"));
    CHECK(party.GetStats("player")["ATK"] == 15.0f);
    CHECK(party.Equip("player", "axe")); // the weapon slot: the sword goes back to the bag
    CHECK(bag.Count("sword") == 1);
    CHECK(party.Equip("player", "ring"));
    CHECK(party.Equip("player", "ring")); // the second accessory slot
    CHECK(party.Find("player")->Equipment.size() == 3);
    CHECK(party.Equip("player", "ring", "accessory1")); // a slot named: ring for ring
    CHECK_FALSE(party.Equip("player", "potion"));       // not equipment
    CHECK_FALSE(party.Equip("player", "sword", "head")); // that slot doesn't take it
    CHECK(party.GetStats("player")["MaxHP"] == 140.0f);

    CHECK(party.Unequip("player", "weapon"));
    CHECK_FALSE(party.Unequip("player", "weapon")); // empty now
    CHECK(told == std::vector<std::string>{ "sword>weapon", "sword<weapon", "axe>weapon", "ring>accessory1", "ring>accessory2",
                                            "ring<accessory1", "ring>accessory1", "axe<weapon" });
}

TEST_CASE("Using an item changes a member's stats; the party saves", "[items][party]")
{
    World w;
    Party party(w.items, w.stats, w.bags, Slots);
    party.Find("player")->Own.Current["HP"] = 40.0f;
    party.ApplyUse("player", *w.items.Find("potion"));
    CHECK(party.GetStats("player")["HP"] == 90.0f);
    party.ApplyUse("player", *w.items.Find("potion"));
    CHECK(party.GetStats("player")["HP"] == 100.0f); // up to MaxHP
    party.ApplyUse("player", *w.items.Find("tonic"));
    CHECK(party.GetStats("player")["ATK"] == 11.0f); // a base stat, for good

    (void)w.bags.GetParty().Add("sword");
    CHECK(party.Equip("player", "sword"));
    const nlohmann::json saved = party.ToJson();
    party.FromJson(nlohmann::json::object()); // a new game
    CHECK(party.GetStats("player")["ATK"] == 10.0f);
    party.FromJson(saved);
    CHECK(party.GetStats("player")["ATK"] == 16.0f);
    CHECK(party.Find("player")->Equipment.at("weapon") == "sword");
}

TEST_CASE("Scripts see the party, its stats, and change it", "[items][party][script]")
{
    World                      w;
    Party                      party(w.items, w.stats, w.bags, Slots);
    const ArcbitTests::TempDir scripts;
    ScriptEngine               lua{ scripts.File("") };
    std::string                used;
    lua.SetServices({ .Items = &w.items, .Bags = &w.bags, .Members = &party, .StatDefs = &w.stats,
                      .UseItem = [&](const std::string_view item, const std::string_view member, const std::string_view) {
                          used = std::string(item) + "@" + std::string(member);
                          return true;
                      } });
    (void)w.bags.GetParty().Add("sword");
    const auto run = [&](const char* code) {
        const ScriptResult result = lua.Run(code);
        INFO(code << " -> " << result.Error);
        REQUIRE(result.Ok);
        return result.Values;
    };
    CHECK(run("return Party.Leader(), #Party.Members()") == std::vector<std::string>{ "player", "1" });
    CHECK(run("return Party.Equip('player', 'sword'), Party.Stats('player').ATK") == std::vector<std::string>{ "true", "15.0" });
    CHECK(run("return Party.Get('player').Equipment.weapon") == std::vector<std::string>{ "sword" });
    CHECK(run("return Party.Use('potion', 'player')") == std::vector<std::string>{ "true" });
    CHECK(used == "potion@player");
}

TEST_CASE("arcbit-loc reads the names a game's stats and party files write", "[items][party][loc]")
{
    Loc::Clear();
    const ArcbitTests::TempDir dir;
    std::filesystem::create_directories(dir.File("locale"));
    (void)dir.Write("locale/en.json", R"({ "$locale": { "name": "English" } })");
    (void)dir.Write("locale/fr.json", R"({ "$locale": { "name": "Francais" }, "party.ayla.name": "Ayla", "stats.STR.name": "Force" })");
    const std::string stats = dir.Write("stats.json", R"({ "stats": { "STR": { "name": "Strength" } } })");
    const std::string party = dir.Write("party.json", R"({ "members": { "ayla": { "name": "Ayla" } } })");
    const std::vector<std::filesystem::path> locales = { dir.File("locale") }, data = { stats, party };

    const LocCheck::Report report = LocCheck::Check(locales, {}, {}, {}, data);
    CHECK(report.Stale.empty()); // their English is known
    CHECK(LocCheck::Template().find("stats.STR.name") != std::string::npos);
    Loc::Clear();
}

