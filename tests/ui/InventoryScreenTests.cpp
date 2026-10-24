// The inventory screen for list bags (docs/items.md#the-inventory-screen):
// tabs, rows, the detail panel, actions and equipment - a headless
// UIManager ticking it, with the engine's English strings.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/items/Party.h>
#include <arcbit/items/Stats.h>
#include <arcbit/save/SaveData.h>
#include <arcbit/ui/HotbarScreen.h>
#include <arcbit/ui/InventoryScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace Arcbit;

namespace
{
struct Fixture
{
    ArcbitTests::TempDir dir;
    ItemLibrary          items;
    StatSet              stats;
    std::unique_ptr<Inventories> bags;
    std::unique_ptr<Party>       party;
    SaveData             data;
    std::unique_ptr<Currencies>  money;
    InputManager         input;
    UIManager            ui;
    InventoryScreen*     screen = nullptr;

    explicit Fixture(const InventoryRules& rules = InventoryRules::Pockets())
    {
        Loc::Clear();
        Loc::AddFolder(ArcbitTests::SourcePath("engine/assets/locale"));
        REQUIRE(Loc::SetLanguage("en"));
        (void)dir.Write("a.arcitems", R"({
            "potion": { "name": "Potion", "description": "Restores 50 HP.", "type": "consumable", "category": "medicine",
                        "weight": 0.5, "value": 25, "use": { "stats": { "HP": 50 } } },
            "ether":  { "name": "Ether", "type": "consumable", "category": "medicine", "weight": 0.5 },
            "sword":  { "name": "Sword", "type": "equipment", "slot": "weapon", "category": "weapons", "weight": 6, "size": [1, 2],
                        "stats": { "ATK": 5 } },
            "key":    { "name": "Cellar Key", "type": "key" } })");
        items.LogIssues = false;
        REQUIRE(items.AddFolder(dir.File("")) == 4);
        bags  = std::make_unique<Inventories>(items);
        bags->AddStarting("party", rules);
        party = std::make_unique<Party>(items, stats, *bags, std::vector<EquipmentSlot>{ { "weapon", "weapon" }, { "head", "head" } });
        money = std::make_unique<Currencies>(data, *bags);
        money->SetCurrencies({ { .Id = "gold" } });
        (void)money->Add("gold", 40);
        for (const auto& [item, count] : { std::pair{ "potion", 3u }, std::pair{ "sword", 1u }, std::pair{ "key", 1u } })
            (void)bags->GetParty().Add(item, count);
    }
    ~Fixture() { Loc::Clear(); }

    void Open()
    {
        auto made        = std::make_unique<InventoryScreen>();
        made->LayoutPath = ArcbitTests::SourcePath("engine/assets/ui/inventory.arcui").string();
        made->Host       = { .Items = &items, .Bags = bags.get(), .Members = party.get(), .Stats = &stats, .Money = money.get(),
                             .Use = [this](const std::string_view item, const std::string_view member, std::string_view bag) {
                                 const ItemDef& def = *items.Find(item);
                                 if (!party->ApplyUse(member, def)) return UseResult::NoEffect;
                                 (void)bags->Find(bag)->Remove(item);
                                 return UseResult::Used;
                             } };
        screen = made.get();
        ui.Push(std::move(made));
        Tick();
    }
    void Tick() { ui.Update(1.0f / 60.0f, { 1280.0f, 720.0f }, input); }
    [[nodiscard]] std::string Text(const char* name) const { return screen->FindWidget<Label>(name)->Text; }
    [[nodiscard]] std::vector<std::string> Shown() const
    {
        std::vector<std::string> names;
        for (const u32 i : screen->GetShown()) names.push_back(bags->GetParty().GetStacks()[i].Item);
        return names;
    }
    void SelectItem(const std::string& item)
    {
        for (u32 tab = 0; tab < screen->GetTabs().size(); ++tab) {
            screen->SelectTab(tab);
            const auto shown = Shown();
            for (u32 row = 0; row < shown.size(); ++row)
                if (shown[row] == item) return screen->Select(row);
        }
        FAIL("no row for " << item);
    }
};
} // namespace

TEST_CASE("Pockets: a tab per category of the game's items, each its own rows", "[ui][inventory]")
{
    Fixture f;
    f.Open();
    // Sorted by name: Key Items, Medicine, Weapons - every category, held or not.
    REQUIRE(f.screen->GetTabs() == std::vector<std::string>{ "key", "medicine", "weapons" });
    CHECK(f.Shown() == std::vector<std::string>{ "key" });
    f.screen->SelectTab(1);
    CHECK(f.Shown() == std::vector<std::string>{ "potion" });
    CHECK(f.screen->FindWidget<Label>("currencies")->Text == "Gold <color=gold>40</color>");
    CHECK_FALSE(f.screen->FindWidget<Label>("weight-text")->Visible); // no carry weight in pockets
}

TEST_CASE("A weight bag: All first, then what it holds; the carry weight shows", "[ui][inventory]")
{
    Fixture f(InventoryRules::Weight(10.0f));
    f.Open();
    REQUIRE(f.screen->GetTabs() == std::vector<std::string>{ "", "key", "medicine", "weapons" });
    CHECK(f.Shown().size() == 3);
    CHECK(f.Text("weight-text") == "Weight 7.5 / 10");

    f.screen->SetFilterText("ot"); // "Potion"
    CHECK(f.Shown() == std::vector<std::string>{ "potion" });
    f.screen->SetFilterText("");
    f.screen->CycleSort(); // Name -> Type
    CHECK(f.screen->GetSort() == InventorySort::Type);
}

TEST_CASE("Typing in the filter keeps focus as the rows come and go; focus on a row stays on a row", "[ui][inventory]")
{
    Fixture f(InventoryRules::Weight(10.0f));
    f.Open();
    UIWidget* filter = f.screen->FindWidget<UIWidget>("filter");
    f.screen->Focus(filter);
    f.screen->SetFilterText("zz"); // three rows to none
    CHECK(f.Shown().empty());
    CHECK(f.screen->GetFocusedWidget() == filter);
    f.screen->SetFilterText("o"); // none to some
    CHECK_FALSE(f.Shown().empty());
    CHECK(f.screen->GetFocusedWidget() == filter);

    f.screen->SetFilterText("");
    f.screen->Focus(f.screen->FindWidget<UIWidget>("row-2"));
    f.screen->SetFilterText("ot"); // three rows to one: focus goes to the last row there is
    CHECK(f.screen->GetFocusedWidget() == f.screen->FindWidget<UIWidget>("row-0"));
}

TEST_CASE("The help line names the keys bound now", "[ui][inventory]")
{
    Fixture f;
    for (const auto& [action, name, key] : { std::tuple{ UIManager::ActionConfirm, "UI_Confirm", Key::Enter },
                                             std::tuple{ UIManager::ActionGrab, "UI_Grab", Key::G },
                                             std::tuple{ InventoryScreen::ActionPagePrev, "UI_PagePrev", Key::Q },
                                             std::tuple{ InventoryScreen::ActionPageNext, "UI_PageNext", Key::E } }) {
        f.input.RegisterAction(action, name);
        f.input.BindKey(action, key);
    }
    f.Open();
    f.Tick(); // the tick after opening reads the keys
    const auto name = [](const Key key) { return InputManager::BindingToString(Binding{ .BindingType = Binding::Type::Key, .BoundKey = key }); };
    // Esc isn't bound here: "?" rather than a name it doesn't have.
    CHECK(f.Text("help") == std::format("<b>{}</b> use / equip   <b>{}</b> move   <b>{}</b> / <b>{}</b> tabs   <b>?</b> close",
                                        name(Key::Enter), name(Key::G), name(Key::Q), name(Key::E)));
}

TEST_CASE("The detail panel: what an item does, and what wearing it would change", "[ui][inventory]")
{
    Fixture f;
    f.Open();
    f.SelectItem("sword");
    CHECK(f.Text("detail-name") == "Sword");
    CHECK(f.Text("detail-info") == "Weight 6   Value 0");
    CHECK(f.Text("detail-stats") == "Attack +5\nAttack: 10 \xE2\x86\x92 <color=green>15</color>"); // "->" as an arrow
    CHECK(f.screen->FindWidget<Button>("equip")->Visible);
    CHECK_FALSE(f.screen->FindWidget<Button>("use")->Visible);

    f.SelectItem("potion");
    CHECK(f.Text("detail-stats") == "Use: HP +50");
    CHECK(f.screen->FindWidget<Button>("use")->Visible);
}

TEST_CASE("Use, equip and unequip from the screen, each with its message", "[ui][inventory]")
{
    Fixture f;
    f.Open();
    f.SelectItem("potion");
    f.screen->UseSelected();
    CHECK(f.screen->GetMessage() == "The Potion would have no effect."); // full HP
    f.party->Find("player")->Own.Current["HP"] = 20.0f;
    f.screen->UseSelected();
    CHECK(f.screen->GetMessage() == "Used the Potion.");
    CHECK(f.bags->GetParty().Count("potion") == 2);

    f.SelectItem("sword");
    f.screen->EquipSelected();
    CHECK(f.screen->GetMessage() == "Equipped the Sword.");
    CHECK(f.party->Find("player")->Equipment.at("weapon") == "sword");
    CHECK(f.bags->GetParty().Count("sword") == 0);
    CHECK(f.Text("member-stats").find("Attack 15") != std::string::npos);

    f.screen->Unequip("weapon");
    CHECK(f.screen->GetMessage() == "Took off the Sword.");
    CHECK(f.bags->GetParty().Count("sword") == 1);
}

TEST_CASE("Dropping an item on an equipment slot wears it; the screen follows the bag", "[ui][inventory]")
{
    Fixture f;
    f.Open();
    UIWidget* weapon = f.screen->FindWidget<UIWidget>("slot-weapon");
    UIWidget* head   = f.screen->FindWidget<UIWidget>("slot-head");
    REQUIRE(weapon);
    REQUIRE(head);
    const DragPayload sword{ .Kind = "item", .Data = ItemDrag{ "sword" } };
    CHECK(weapon->Accepts(sword));
    CHECK_FALSE(head->Accepts(sword)); // a sword isn't worn on the head
    CHECK_FALSE(weapon->Accepts(DragPayload{ .Kind = "item", .Data = ItemDrag{ "potion" } }));

    weapon->OnDrop(sword);
    f.Tick(); // the drop's work runs at the next tick
    CHECK(f.party->Find("player")->Equipment.at("weapon") == "sword");
    CHECK(f.Text("message") == "Equipped the Sword.");

    (void)f.bags->GetParty().Add("potion", 2); // a script, a pickup - not the screen
    f.screen->SelectTab(1);
    CHECK(f.Text("message") == "Equipped the Sword.");
    f.Tick();
    CHECK(f.bags->GetParty().GetStacks()[f.screen->GetShown()[0]].Count == 5);
    CHECK(f.screen->FindWidget<Label>("row-0") == nullptr); // rows are buttons
    CHECK(f.screen->FindWidget<UIWidget>("row-0")->FindDescendant("count"));
}

namespace
{
// A 4 x 3 grid with a hotbar section of 4 below it.
InventoryRules GridBag()
{
    InventoryRules rules = InventoryRules::Grid(4, 3);
    rules.Sections.push_back({ .Name = "hotbar", .Width = 4, .Height = 1 });
    return rules;
}

u32 IndexOf(const Inventory& bag, const std::string& item)
{
    const auto& stacks = bag.GetStacks();
    return static_cast<u32>(std::ranges::find_if(stacks, [&](const ItemStack& s) { return s.Item == item; }) - stacks.begin());
}
} // namespace

TEST_CASE("A grid bag shows its grid: a cell for each, items across the cells they take", "[ui][inventory][grid]")
{
    Fixture f(GridBag());
    f.Open();
    REQUIRE(f.screen->IsGridView());
    const Inventory& bag   = f.bags->GetParty();
    const u32        sword = IndexOf(bag, "sword");
    CHECK(bag.StackAt("", bag.GetStacks()[sword].X, bag.GetStacks()[sword].Y + 1) == sword); // a 1 x 2 sword: two cells
    CHECK(f.screen->FindWidget<UIWidget>("cell-main-3-2"));
    CHECK(f.screen->FindWidget<UIWidget>("cell-hotbar-3-0"));
    CHECK_FALSE(f.screen->FindWidget<UIWidget>("list")->Visible); // the list view's widgets are hidden
    CHECK_FALSE(f.screen->FindWidget<UIWidget>("tabs")->Visible);

    f.screen->ToggleView(); // the same items as a list, with tabs
    CHECK_FALSE(f.screen->IsGridView());
    CHECK(f.Shown().size() == 3);
    CHECK_FALSE(f.screen->FindWidget<UIWidget>("grid-area")->Visible);
}

TEST_CASE("Grid drops move, turn, split, merge and swap - or say it doesn't fit", "[ui][inventory][grid]")
{
    Fixture f(GridBag());
    f.Open();
    Inventory& bag    = f.bags->GetParty();
    const auto at     = [&](const std::string& item) { const ItemStack& s = bag.GetStacks()[IndexOf(bag, item)]; return std::tuple{ s.Section, s.X, s.Y, s.Rotated }; };

    CHECK(f.screen->PlaceAt(IndexOf(bag, "sword"), "", 0, 2, true, 1)); // turned: across two cells of the last row
    CHECK(at("sword") == std::tuple{ std::string{}, 0u, 2u, true });
    CHECK_FALSE(f.screen->PlaceAt(IndexOf(bag, "sword"), "", 3, 2, true, 1)); // turned, it'd stick out
    CHECK(f.screen->GetMessage() == "The Sword doesn't fit there.");
    CHECK(f.screen->PlaceAt(IndexOf(bag, "sword"), "", 3, 1, false, 1));     // upright it fits

    CHECK(f.screen->PlaceAt(IndexOf(bag, "potion"), "hotbar", 0, 0, false, 1)); // one of three, split off
    CHECK(bag.Count("potion") == 3);
    CHECK(bag.GetStacks().size() == 4);
    const auto split = std::ranges::find_if(bag.GetStacks(), [](const ItemStack& s) { return s.Section == "hotbar"; });
    REQUIRE(split != bag.GetStacks().end());
    CHECK(split->Count == 1);
    const u32 single = static_cast<u32>(split - bag.GetStacks().begin());
    const auto [section, x, y, turned] = at("potion");
    CHECK(f.screen->PlaceAt(single, section, x, y, false, 1)); // back onto the rest: merged
    CHECK(bag.GetStacks().size() == 3);

    // A real drag lands where its ghost is drawn - centered on the pointer -
    // not on the cell under the pointer: the turned sword (two cells across)
    // held by its right half, let go with the pointer in a cell's left part,
    // takes that cell's left neighbor and that cell.
    CHECK(f.screen->PlaceAt(IndexOf(bag, "sword"), "", 0, 2, true, 1));
    const UIRect screenRect{ 0.0f, 0.0f, 1280.0f, 720.0f };
    const UISkin skin;
    const auto   point = [&](const char* cell, const f32 across) {
        const UIRect r = *f.screen->GetWidgetRect(f.screen->FindWidget<UIWidget>(cell));
        return Vec2{ r.X + r.W * across, r.Y + r.H * 0.5f };
    };
    (void)f.screen->Interact(1.0f / 60.0f, screenRect, { .Pos = point("cell-main-1-2", 0.5f), .Down = true, .JustDown = true }, skin);
    (void)f.screen->Interact(1.0f / 60.0f, screenRect, { .Pos = point("cell-main-2-1", 0.25f), .Down = true }, skin);
    REQUIRE(f.screen->IsDragging());
    CHECK(f.screen->GetDrag()->Rotated);
    (void)f.screen->Interact(1.0f / 60.0f, screenRect, { .Pos = point("cell-main-2-1", 0.25f), .JustUp = true }, skin);
    f.Tick(); // the drop's work runs at the next tick
    CHECK(at("sword") == std::tuple{ std::string{}, 1u, 1u, true }); // cells (1,1) and (2,1)
}

TEST_CASE("Number keys on the inventory screen put the selected item on a quick slot", "[ui][inventory][hotbar]")
{
    InventoryRules rules = InventoryRules::Pockets();
    rules.QuickSlots     = 3;
    Fixture f(rules);
    f.Open();
    f.SelectItem("potion");
    f.screen->AssignQuickSlot(1);
    CHECK(f.bags->GetParty().GetQuickSlots() == std::vector<std::string>{ "", "potion", "" });
    CHECK(f.screen->GetMessage() == "Potion is on quick slot 2.");
    CHECK(dynamic_cast<Label*>(f.screen->FindWidget<UIWidget>("row-0")->FindDescendant("name"))->Text == "Potion  [2]");
    f.screen->AssignQuickSlot(1); // again: off
    CHECK(f.bags->GetParty().GetQuickSlots()[1].empty());
    f.screen->AssignQuickSlot(7); // past the bag's quick slots: nothing
    CHECK(f.screen->GetMessage() == "Potion is off quick slot 2.");
}

TEST_CASE("The hotbar shows quick slots, or a grid's hotbar section; selection wraps", "[ui][hotbar]")
{
    InventoryRules rules = InventoryRules::Pockets();
    rules.QuickSlots     = 4;
    Fixture f(rules);
    (void)f.bags->GetParty().SetQuickSlot(0, "potion");
    (void)f.bags->GetParty().SetQuickSlot(2, "sword");
    auto made        = std::make_unique<HotbarScreen>();
    made->LayoutPath = ArcbitTests::SourcePath("engine/assets/ui/hotbar.arcui").string();
    made->Host       = { .Bag = [&f] { return &f.bags->GetParty(); }, .Items = &f.items };
    HotbarScreen* bar = made.get();
    f.ui.Push(std::move(made));
    f.Tick();
    CHECK(bar->GetSlots() == std::vector<std::string>{ "potion", "", "sword", "" });
    CHECK(bar->GetSelectedItem() == "potion");
    bar->Step(-1);
    CHECK(bar->GetSelected() == 3); // round to the end
    bar->Select(2);
    CHECK(bar->GetSelectedItem() == "sword");
    CHECK(bar->FindWidget<UIWidget>("bar")->Visible);

    // A grid with a hotbar section and no quick slots: what lies in it.
    InventoryRules grid = InventoryRules::Grid(3, 2);
    grid.Sections.push_back({ .Name = "hotbar", .Width = 3, .Height = 1 });
    Inventory& stash = f.bags->Create("stash", grid);
    (void)stash.Add("key");
    REQUIRE(stash.MoveTo(0, "hotbar", 1, 0, false));
    bar->Host.Bag = [&stash] { return &stash; };
    CHECK(bar->GetSlots() == std::vector<std::string>{ "", "key", "" });
}
