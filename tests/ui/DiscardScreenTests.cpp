// Changing a bag to one that holds less (docs/items.md#changing-bags): the
// discard prompt's suggestion (junk first), the player's marks, Confirm only
// once the rest fits, Cancel keeping the bag as it was.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/items/Inventory.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/ui/DiscardScreen.h>
#include <arcbit/ui/UIManager.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
struct Fixture
{
    ArcbitTests::TempDir dir;
    ItemLibrary          items;
    Inventory            bag;
    InputManager         input;
    UIManager            ui;
    DiscardScreen*       screen = nullptr;
    std::optional<bool>  done;

    Fixture()
    {
        Loc::Clear();
        Loc::AddFolder(ArcbitTests::SourcePath("engine/assets/locale"));
        REQUIRE(Loc::SetLanguage("en"));
        (void)dir.Write("a.arcitems", R"({
            "potion": { "name": "Potion", "stack": 10 },
            "rock":   { "name": "Rock", "stack": 1 },
            "boot":   { "name": "Old Boot", "stack": 1, "tags": ["junk"] } })");
        items.LogIssues = false;
        REQUIRE(items.AddFolder(dir.File("")) == 3);
        InventoryRules five;
        five.Slots = 5;
        bag        = Inventory(items, five);
        (void)bag.Add("boot");
        (void)bag.Add("potion", 3);
        (void)bag.Add("rock", 3); // five entries

        InventoryRules three;
        three.Slots      = 3;
        auto made        = std::make_unique<DiscardScreen>();
        made->LayoutPath = ArcbitTests::SourcePath("engine/assets/ui/discard.arcui").string();
        made->Bag        = &bag;
        made->To         = three;
        made->Items      = &items;
        made->OnDone     = [this](const bool changed) { done = changed; };
        screen           = made.get();
        ui.Push(std::move(made));
        ui.Update(1.0f / 60.0f, { 1280.0f, 720.0f }, input);
    }
    ~Fixture() { Loc::Clear(); }
};
} // namespace

TEST_CASE("The discard prompt marks what won't fit, junk first; Confirm waits for the rest to fit", "[ui][discard]")
{
    Fixture f;
    // Two of five entries must go: the boot (junk) and the newest rock.
    REQUIRE(f.screen->GetMarked() == std::vector<u32>{ 0, 4 });
    CHECK(f.screen->CanConfirm());

    f.screen->Toggle(0); // keep the boot after all...
    CHECK_FALSE(f.screen->CanConfirm());
    f.screen->Toggle(3); // ...and lose another rock
    CHECK(f.screen->GetMarked() == std::vector<u32>{ 3, 4 });
    CHECK(f.screen->CanConfirm());

    f.screen->Confirm();
    CHECK(f.done == true);
    CHECK(f.bag.GetRules().Slots == 3);
    CHECK(f.bag.Count("boot") == 1);
    CHECK(f.bag.Count("rock") == 1);
}

TEST_CASE("Cancelling the discard prompt keeps the bag as it was", "[ui][discard]")
{
    Fixture f;
    f.screen->Cancel();
    CHECK(f.done == false);
    CHECK(f.bag.GetRules().Slots == 5);
    CHECK(f.bag.GetStacks().size() == 5);
}

TEST_CASE("Rules from JSON start from their kind's preset", "[items][inventory]")
{
    const InventoryRules weight = RulesFromJson({ { "kind", "weight" }, { "max_weight", 30.0 } });
    CHECK(weight.OneEntryPerItem);
    CHECK(weight.UnlimitedStacks);
    CHECK(weight.MaxWeight == 30.0f);
    CHECK(RulesFromJson({ { "kind", "pockets" } }).OneEntryPerItem);
    CHECK_FALSE(RulesFromJson({ { "kind", "pockets" }, { "one_entry_per_item", false } }).OneEntryPerItem); // a saved bag says so itself
}
