// Loot tables (docs/items.md#loot-tables): .arcloot files, their checks,
// rolling, LootDrop, a Container's loot field, and Lua's Loot.Roll.

#include "TestSupport.h"

#include <arcbit/items/Container.h>
#include <arcbit/items/Loot.h>
#include <arcbit/items/Pickup.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
using Stacks = std::vector<LootStack>;

LootLibrary Library(const char* tables)
{
    LootLibrary library;
    library.LogIssues = false;
    for (const auto& [id, table] : nlohmann::json::parse(tables).items()) library.Add(id, table);
    return library;
}

bool Has(const std::vector<LootIssue>& issues, const std::string& table, const std::string& message, const bool error = true)
{
    for (const LootIssue& issue : issues)
        if (issue.Table == table && issue.Message == message && issue.Error == error) return true;
    return false;
}

class NoAssets final : public SceneAssets
{
public:
    TextureHandle      LoadTexture(std::string_view) override { return {}; }
    std::string        TexturePath(TextureHandle) const override { return {}; }
    SamplerHandle      Sampler(std::string_view) override { return {}; }
    std::string        SamplerName(SamplerHandle) const override { return {}; }
    const SpriteSheet* LoadSpriteSheet(std::string_view) override { return nullptr; }
    std::string        SpriteSheetPath(const SpriteSheet*) const override { return {}; }
    std::string        ActionName(ActionID) const override { return {}; }
    bool               LoadTileMap(TileMap&, std::string_view) override { return true; }
};
} // namespace

TEST_CASE("A roll gives the always entries, then weighted picks, merged by item", "[items][loot]")
{
    const LootLibrary loot = Library(R"({
        "chest":  { "always": [ { "currency": "gold", "count": 10 }, { "item": "potion" } ],
                    "rolls": 3,
                    "entries": [ { "item": "potion", "count": 2 }, { "item": "boot", "weight": 0 } ] },
        "nested": { "entries": [ { "table": "chest", "count": 2 } ] },
        "empty":  { "entries": [ { "weight": 5 } ] } })");
    std::mt19937 rng(7);
    CHECK(loot.Roll("chest", rng) == Stacks{ { "gold", 10, true }, { "potion", 7, false } }); // 1 + 3 x 2; weight 0 never
    CHECK(loot.Roll("nested", rng) == Stacks{ { "gold", 20, true }, { "potion", 14, false } });
    CHECK(loot.Roll("empty", rng).empty()); // an entry of nothing
    CHECK(loot.Roll("nowhere", rng).empty());
}

TEST_CASE("Counts and rolls stay in their ranges; picks follow the weights", "[items][loot]")
{
    const LootLibrary loot = Library(R"({
        "slime": { "rolls": [0, 2], "entries": [ { "item": "jelly", "count": [1, 3], "weight": 3 }, { "weight": 1 } ] } })");
    std::mt19937 rng(42);
    u32          jellies = 0, most = 0;
    for (int i = 0; i < 4000; ++i)
        for (const LootStack& stack : loot.Roll("slime", rng)) {
            jellies += stack.Count;
            most = std::max(most, stack.Count);
        }
    CHECK(most <= 6); // two picks of at most 3
    // A roll: 1 pick on average, jelly 3/4 of the time, 2 jellies a pick: 1.5 a roll.
    const f64 perRoll = jellies / 4000.0;
    CHECK(perRoll > 1.35);
    CHECK(perRoll < 1.65);
}

TEST_CASE("Loot files are checked against the items, currencies and tables", "[items][loot]")
{
    LootLibrary loot = Library(R"({
        "a":   { "entries": [ { "item": "potion" }, { "item": "potoin" }, { "currency": "gems" }, { "table": "b" } ] },
        "b":   { "entries": [ { "table": "a" } ], "rols": 2 },
        "bad": { "rolls": [3, 1], "entries": [ { "item": "potion", "table": "a" }, { "weight": -1 } ] } })");
    loot.Check([](const std::string_view id) { return id == "potion"; }, [](const std::string_view id) { return id == "gold"; });
    const std::vector<LootIssue>& issues = loot.GetIssues();
    CHECK(Has(issues, "a", "no item 'potoin'"));
    CHECK(Has(issues, "a", "no currency 'gems'"));
    CHECK(Has(issues, "a", "rolls itself: a -> b -> a"));
    CHECK(Has(issues, "b", "unknown field 'rols'", false));
    CHECK(Has(issues, "bad", "[3, 1] runs backwards"));
    CHECK(Has(issues, "bad", "an entry gives one item, currency or table"));
    CHECK(Has(issues, "bad", "\"weight\" must be a whole number, 0 or more"));

    std::mt19937 rng(1);
    (void)loot.Roll("a", rng); // a loop still ends
}

TEST_CASE(".arcloot files hold many tables; an id twice is an error", "[items][loot]")
{
    const ArcbitTests::TempDir dir;
    (void)dir.Write("a.arcloot", R"({ // comments are fine
        "slime": { "entries": [ { "item": "jelly" } ] }, "bat": {} })");
    (void)dir.Write("b.arcloot", R"({ "slime": {} })");
    LootLibrary loot;
    loot.LogIssues = false;
    CHECK(loot.AddFolder(dir.File("")) == 2);
    CHECK(loot.GetIds() == std::vector<std::string>{ "bat", "slime" });
    REQUIRE(loot.GetIssues().size() == 1);
    CHECK(loot.GetIssues()[0].Table == "slime");
}

TEST_CASE("A LootDrop drops its table when destroyed - not when its scene goes", "[items][loot]")
{
    std::vector<std::pair<std::string, Vec2>> dropped;
    ItemHost host;
    host.DropLoot = [&](Scene&, const std::string_view table, const Vec2 at) { dropped.emplace_back(table, at); };
    {
        SceneManager scenes;
        scenes.SetHost({ .Items = &host });
        scenes.SetActive(std::make_unique<Scene>());
        Scene&       scene = *scenes.GetActive();
        const Entity slime = scene.GetWorld().CreateEntity();
        scene.GetWorld().GetTransform(slime)->Position = { 96.0f, 32.0f };
        scene.GetWorld().AddComponent<LootDrop>(slime, LootDrop{ .Table = "slime" });
        const Entity bat = scene.GetWorld().CreateEntity();
        scene.GetWorld().AddComponent<LootDrop>(bat, LootDrop{ .Table = "bat" });

        scene.GetWorld().DestroyEntity(slime);
        CHECK(dropped.empty()); // not mid-destroy: at the next update
        scene.Update(1.0f / 60.0f);
        REQUIRE(dropped.size() == 1);
        CHECK(dropped[0].first == "slime");
        CHECK(dropped[0].second == Vec2{ 96.0f, 32.0f });
    } // the bat goes with its scene: no loot
    CHECK(dropped.size() == 1);
}

TEST_CASE("Containers keep currency and their loot table in scene files; LootDrop too", "[items][loot][scene]")
{
    SceneRegistry registry;
    RegisterBuiltinComponents(registry);
    NoAssets     assets;
    Scene        scene;
    const Entity chest = scene.GetWorld().CreateEntity();
    const auto*  entry = registry.FindComponent("Container");
    REQUIRE(entry);
    entry->Read(scene.GetWorld(), chest, nlohmann::json::parse(R"({ "currency": { "gold": 25 }, "loot": "chest" })"), assets);
    const Container& c = *scene.GetWorld().GetComponent<Container>(chest);
    CHECK(c.Currency.at("gold") == 25);
    CHECK(c.Loot == "chest");
    const nlohmann::json written = entry->Write(scene.GetWorld(), chest, assets);
    CHECK(written["currency"]["gold"] == 25);
    CHECK(ParseContainerOpening("take_all") == ContainerOpening::TakeAll);
    CHECK_FALSE(ParseContainerOpening("open_sesame"));

    const auto* drop = registry.FindComponent("LootDrop");
    REQUIRE(drop);
    drop->Read(scene.GetWorld(), chest, nlohmann::json::parse(R"({ "table": "slime" })"), assets);
    CHECK(scene.GetWorld().GetComponent<LootDrop>(chest)->Table == "slime");
}

TEST_CASE("Scripts roll loot", "[items][loot][script]")
{
    const LootLibrary          loot = Library(R"({ "chest": { "always": [ { "item": "potion", "count": 2 }, { "currency": "gold", "count": 5 } ] } })");
    std::mt19937               rng(3);
    const ArcbitTests::TempDir scripts;
    ScriptEngine               lua{ scripts.File("") };
    lua.SetServices({ .RollLoot = [&](const std::string_view table) { return loot.Roll(table, rng); } });
    const ScriptResult result = lua.Run("local r = Loot.Roll('chest') return #r, r[1].Id, r[1].Count, r[2].Currency");
    REQUIRE(result.Ok);
    CHECK(result.Values == std::vector<std::string>{ "2", "potion", "2", "true" });
}
