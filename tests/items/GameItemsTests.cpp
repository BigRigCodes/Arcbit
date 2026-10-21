// Items in the game (docs/items.md): the game's bags and their save section,
// currencies either kind, Container in scene files, dialogs that check, give
// and take items, and the Lua API.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/dialog/DialogRunner.h>
#include <arcbit/items/Container.h>
#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/save/SaveData.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

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
            "potion": { "name": "Potion", "description": "Restores 50 HP.", "type": "consumable", "stack": 10, "value": 25 },
            "key":    { "name": "Cellar Key", "type": "key", "stack": 1 },
            "coin":   { "name": "Coin", "type": "currency", "stack": 999 } })");
        library.LogIssues = false;
        REQUIRE(library.AddFolder(dir.File("")) == 3);
    }
    ~Items() { Loc::Clear(); }
};

class ItemAssets final : public SceneAssets
{
public:
    explicit ItemAssets(const ItemLibrary& items) : _items(items) {}
    TextureHandle      LoadTexture(std::string_view) override { return {}; }
    std::string        TexturePath(TextureHandle) const override { return {}; }
    SamplerHandle      Sampler(std::string_view) override { return {}; }
    std::string        SamplerName(SamplerHandle) const override { return {}; }
    const SpriteSheet* LoadSpriteSheet(std::string_view) override { return nullptr; }
    std::string        SpriteSheetPath(const SpriteSheet*) const override { return {}; }
    std::string        ActionName(ActionID) const override { return {}; }
    bool               LoadTileMap(TileMap&, std::string_view) override { return true; }
    const ItemLibrary* Items() const override { return &_items; }

private:
    const ItemLibrary& _items;
};

std::shared_ptr<const Dialog> Talk(const char* json)
{
    std::vector<DialogIssue> issues;
    auto dialog = ParseDialog(nlohmann::ordered_json::parse(json), "shop", issues);
    REQUIRE(dialog);
    for (const DialogIssue& issue : issues) {
        INFO(issue.Message);
        CHECK_FALSE(issue.Error);
    }
    Loc::SetStrings(Loc::English, "test:shop", dialog->GetStrings());
    return std::make_shared<const Dialog>(std::move(*dialog));
}
} // namespace

TEST_CASE("The game's bags: a party bag from the start, saved as a section, reset by a new game", "[items][bags]")
{
    Items       items;
    Inventories bags(items.library);
    bags.AddStarting("party", InventoryRules::Pockets());
    std::vector<std::string> told;
    bags.OnChange = [&](const std::string& bag, const std::string& item, const i64 delta) {
        told.push_back(bag + ":" + item + std::to_string(delta));
    };
    CHECK(bags.GetParty().Add("potion", 3) == 3);
    Inventory& stash = bags.Create("stash", InventoryRules::Grid(4, 4));
    CHECK(stash.Add("key") == 1);
    CHECK(told == std::vector<std::string>{ "party:potion3", "stash:key1" });
    CHECK(bags.GetNames() == std::vector<std::string>{ "party", "stash" });

    const nlohmann::json saved = bags.ToJson();
    bags.Reset(); // a new game
    CHECK(bags.GetParty().IsEmpty());
    CHECK_FALSE(bags.Find("stash"));

    bags.FromJson(saved);
    CHECK(bags.GetParty().Count("potion") == 3);
    REQUIRE(bags.Find("stash"));
    CHECK(bags.Find("stash")->GetRules().Width == 4);
    CHECK(told.size() == 2); // loading isn't the player's doing: no changes told
    bags.Find("stash")->Remove("key");
    CHECK(told.back() == "stash:key-1"); // ... but loaded bags are watched

    bags.FromJson(nlohmann::json::object()); // the empty section a new game gets
    CHECK(bags.GetParty().IsEmpty());
}

TEST_CASE("Currencies read and pay the same way, counters or items", "[items][currency]")
{
    Items       items;
    SaveData    data;
    Inventories bags(items.library);
    bags.AddStarting("party", InventoryRules::Pockets());
    Currencies money(data, bags);
    money.SetCurrencies({ { .Id = "gold" }, { .Id = "coin", .Item = true } });
    std::vector<std::string> told;
    money.OnChange = [&](const std::string& id, const i64 amount, const i64 delta) {
        told.push_back(id + "=" + std::to_string(amount) + "(" + std::to_string(delta) + ")");
    };

    CHECK(money.Add("gold", 30) == 30);
    CHECK(data.Get<i64>("gold", 0) == 30); // a SaveData counter
    CHECK(money.Add("gold", -50) == -30);  // never below 0
    CHECK(money.Add("gold", 10) == 10);
    CHECK_FALSE(money.Spend("gold", 11));  // all or nothing
    CHECK(money.Spend("gold", 4));
    CHECK(money.Get("gold") == 6);

    CHECK(money.Add("coin", 25) == 25);
    CHECK(bags.GetParty().Count("coin") == 25); // carried in the bag
    CHECK(money.Spend("coin", 5));
    CHECK(money.Get("coin") == 20);
    CHECK(money.Add("silver", 1) == 0); // not a currency (logged)
    CHECK(told == std::vector<std::string>{ "gold=30(30)", "gold=0(-30)", "gold=10(10)", "gold=6(-4)", "coin=25(25)", "coin=20(-5)" });
}

TEST_CASE("A Container in a scene file holds items, and saves what it holds", "[items][container]")
{
    Items         items;
    SceneRegistry registry;
    RegisterBuiltinComponents(registry);
    ItemAssets                           assets(items.library);
    Scene                                scene;
    const SceneRegistry::ComponentEntry* entry = registry.FindComponent("Container");
    REQUIRE(entry);

    const Entity chest = scene.GetWorld().CreateEntity();
    entry->Read(scene.GetWorld(), chest, nlohmann::json::parse(R"({
        "rules": { "kind": "list", "slots": 4 }, "items": { "potion": 12, "key": 1 }, "open": "take_all" })"), assets);
    const Container& c = *scene.GetWorld().GetComponent<Container>(chest);
    CHECK(c.Items.Count("potion") == 12); // two stacks of the 10-stack
    CHECK(c.Items.GetStacks().size() == 3);
    CHECK(c.Items.GetRules().Slots == 4);
    CHECK(c.Open == "take_all");

    const nlohmann::json written = entry->Write(scene.GetWorld(), chest, assets);
    CHECK(written["stacks"].size() == 3);
    const Entity copy = scene.GetWorld().CreateEntity();
    entry->Read(scene.GetWorld(), copy, written, assets);
    CHECK(scene.GetWorld().GetComponent<Container>(copy)->Items.Count("key") == 1);

    // A save's change applied over the authored chest: just its stacks.
    entry->Read(scene.GetWorld(), chest, nlohmann::json::parse(R"({ "stacks": [ { "item": "key", "count": 1 } ] })"), assets);
    CHECK(c.Items.Count("potion") == 0);
    CHECK(c.Items.GetRules().Slots == 4); // the rules it had
}

TEST_CASE("Dialogs check, give and take items, and read currencies however they're kept", "[items][dialog]")
{
    Items       items;
    SaveData    data;
    Inventories bags(items.library);
    bags.AddStarting("party", InventoryRules::Pockets());
    Currencies money(data, bags);
    money.SetCurrencies({ { .Id = "coin", .Item = true } });
    Inventory& party = bags.GetParty();
    (void)party.Add("coin", 7);

    DialogHost host{ .Data = &data };
    host.CountItem  = [&](const std::string_view item) { return party.Count(item); };
    host.ChangeItem = [&](const std::string_view item, const i64 n) -> u32 {
        return n >= 0 ? party.Add(item, static_cast<u32>(n)) : party.Remove(item, static_cast<u32>(-n));
    };
    host.Read = [&](const std::string_view key) -> std::optional<SaveValue> {
        if (money.Find(key)) return SaveValue{ money.Get(key) };
        return std::nullopt;
    };
    host.AddTo = [&](const std::string_view key, const SaveValue& delta) {
        if (!money.Find(key)) return false;
        (void)money.Add(key, std::get<i64>(delta));
        return true;
    };

    const auto shop = Talk(R"({ "nodes": { "a": [
        { "say": "You have {coin} coins.", "id": "count" },
        { "choice": [
            { "id": "buy", "text": "A potion, please.", "if": { "coin": [">=", 5] }, "add": { "coin": -5 }, "give": { "potion": 2 } },
            { "id": "key", "text": "The key?", "if": { "has": ["key"] } },
            { "id": "bye", "text": "Bye." } ] },
        { "say": "Thanks.", "take": ["potion"] } ] } })");
    DialogRunner runner;
    runner.Start(shop, host);
    CHECK(runner.GetLine().Text == "You have 7 coins."); // a currency carried as an item
    runner.Next();
    REQUIRE(runner.GetChoices().size() == 2); // no key: that option hides
    runner.Choose(0);
    CHECK(money.Get("coin") == 2);
    CHECK(party.Count("potion") == 1); // given 2, one taken as "Thanks." showed
}

TEST_CASE("Scripts reach items, the game's bags and currencies", "[items][script]")
{
    Items                      items;
    SaveData                   data;
    Inventories                bags(items.library);
    const ArcbitTests::TempDir scripts;
    bags.AddStarting("party", InventoryRules::Pockets());
    Currencies money(data, bags);
    money.SetCurrencies({ { .Id = "gold" } });
    ScriptEngine lua{ scripts.File("") };
    lua.SetServices({ .Items = &items.library, .Bags = &bags, .Money = &money });

    const auto run = [&](const char* code) {
        const ScriptResult result = lua.Run(code);
        INFO(code);
        REQUIRE(result.Ok);
        return result.Values;
    };
    CHECK(run("return Inventory.Add('potion', 12)") == std::vector<std::string>{ "10" }); // pockets: one entry, up to 10
    CHECK(run("return Inventory.Count('potion'), Inventory.Has('key')") == std::vector<std::string>{ "10", "false" });
    CHECK(run("return #Inventory.Get(), Inventory.Get()[1].Item") == std::vector<std::string>{ "1", "potion" });
    CHECK(run("return Items.Get('potion').Name, Items.Get('potion').Value") == std::vector<std::string>{ "Potion", "25" });
    CHECK(run("return Items.Get('nothing') == nil") == std::vector<std::string>{ "true" });
    CHECK(run("Currency.Add('gold', 9) return Currency.Spend('gold', 4), Currency.Get('gold')") ==
          std::vector<std::string>{ "true", "5" });
    CHECK_FALSE(lua.Run("Inventory.Add('potion', 1, 'nowhere')").Ok); // no such bag: an error
}
