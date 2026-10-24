// The transfer screen (docs/items.md#containers): a container beside the
// party's bag - rows moved across whole or one at a time, dragged, Take All
// with the container's currency, and closing when the container goes.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/items/Container.h>
#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/save/SaveData.h>
#include <arcbit/ui/TransferScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace Arcbit;

namespace
{
struct Fixture
{
    ArcbitTests::TempDir         dir;
    ItemLibrary                  items;
    std::unique_ptr<Inventories> bags;
    SaveData                     data;
    std::unique_ptr<Currencies>  money;
    Container                    chest;
    bool                         chestThere = true;
    bool                         closed     = false;
    InputManager                 input;
    UIManager                    ui;
    TransferScreen*              screen = nullptr;

    Fixture()
    {
        Loc::Clear();
        Loc::AddFolder(ArcbitTests::SourcePath("engine/assets/locale"));
        REQUIRE(Loc::SetLanguage("en"));
        (void)dir.Write("a.arcitems", R"({
            "potion": { "name": "Potion", "type": "consumable", "stack": 10 },
            "rock":   { "name": "Rock", "stack": 1 } })");
        items.LogIssues = false;
        REQUIRE(items.AddFolder(dir.File("")) == 2);
        bags = std::make_unique<Inventories>(items);
        InventoryRules three;
        three.Slots = 3;
        bags->AddStarting("party", three);
        money = std::make_unique<Currencies>(data, *bags);
        money->SetCurrencies({ { .Id = "gold" } });
        chest.Items = Inventory(items, InventoryRules{});
        (void)chest.Items.Add("potion", 4);
        (void)chest.Items.Add("rock", 1);
        chest.Currency["gold"] = 25;

        auto made        = std::make_unique<TransferScreen>();
        made->LayoutPath = ArcbitTests::SourcePath("engine/assets/ui/transfer.arcui").string();
        made->Host       = { .Items = &items, .Bags = bags.get(), .Money = money.get(),
                             .Container = [this]() -> Container* { return chestThere ? &chest : nullptr; } };
        made->OnClose    = [this] { closed = true; };
        screen           = made.get();
        ui.Push(std::move(made));
        Tick();
    }
    ~Fixture() { Loc::Clear(); }
    void Tick() { ui.Update(1.0f / 60.0f, { 1280.0f, 720.0f }, input); }
    Inventory& Bag() { return bags->GetParty(); }
};
} // namespace

TEST_CASE("Rows move across, all or one; a full side says so", "[ui][transfer]")
{
    Fixture f;
    CHECK(f.screen->Move(TransferScreen::Side::Container, 0, true) == 1); // one potion
    CHECK(f.Bag().Count("potion") == 1);
    CHECK(f.chest.Items.Count("potion") == 3);
    CHECK(f.screen->Move(TransferScreen::Side::Container, 0) == 3); // the rest
    CHECK(f.chest.Items.Count("potion") == 0);
    CHECK(f.screen->Move(TransferScreen::Side::Bag, 0) == 4); // and back again
    CHECK(f.chest.Items.Count("potion") == 4);

    (void)f.Bag().Add("rock", 3); // three slots, all rocks
    const u32 rock = f.chest.Items.GetStacks()[0].Item == "rock" ? 0 : 1;
    CHECK(f.screen->Move(TransferScreen::Side::Container, rock) == 0);
    CHECK(f.screen->GetMessage() == "There's no room for the Rock.");
}

TEST_CASE("Take All takes what fits and the currency; a drag moves a row across", "[ui][transfer]")
{
    Fixture f;
    UIWidget* to = f.screen->FindWidget<UIWidget>("to");
    UIWidget* from = f.screen->FindWidget<UIWidget>("from-0");
    REQUIRE(to);
    REQUIRE(from);
    const std::optional<DragPayload> drag = from->OnDragStart(DragStart{});
    REQUIRE(drag);
    CHECK(to->Accepts(*drag));
    CHECK_FALSE(f.screen->FindWidget<UIWidget>("from")->Accepts(*drag)); // not back onto its own side
    to->OnDrop(*drag);
    f.Tick();
    CHECK(f.chest.Items.GetStacks().size() == 1);

    f.screen->TakeAll();
    CHECK(f.chest.Items.IsEmpty());
    CHECK(f.money->Get("gold") == 25);
    CHECK(f.chest.Currency.empty());
    CHECK(f.screen->GetMessage() == "Took everything.");
}

TEST_CASE("The transfer screen closes when its container goes", "[ui][transfer]")
{
    Fixture f;
    CHECK_FALSE(f.closed);
    f.chestThere = false; // its scene unloaded
    f.Tick();
    CHECK(f.closed);
}
