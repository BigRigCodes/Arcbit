#pragma once

#include <arcbit/input/InputTypes.h>
#include <arcbit/ui/InventoryScreen.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Inventory;
class ItemLibrary;

// What the hotbar shows and where from - Application fills it in.
struct HotbarHost
{
    std::function<Inventory*()>                                   Bag; // the party's
    const ItemLibrary*                                            Items = nullptr;
    std::function<std::optional<ItemIcon>(std::string_view icon)> LoadIcon;
    std::function<bool()>                                         Shown; // null: always (a game's HUD rules)
};

// ---------------------------------------------------------------------------
// HotbarScreen - the quick slot bar along the bottom of the screen
// (docs/items.md#quick-slots-and-the-hotbar), a HUD: the party bag's quick
// slots (references to items: a slot shows how many the bag has), or - a
// grid bag with a "hotbar" section and no quick slots - what lies in that
// section's cells. One slot is selected; Application's keys select and use.
//
// Optional, as quick slots are: Application only makes it when the project's
// party bag has quick slots or a hotbar section (or Defaults.Hotbar is set).
// Its layout (LayoutPath) is replaceable: a "bar" widget, the "slot" template
// (with "icon", "name", "key", "count").
// ---------------------------------------------------------------------------
class HotbarScreen : public UIScreen
{
public:
    // The hotbar's keys, gameplay actions the player can rebind: 1 to 9
    // pick a slot, Next / Prev step (and the mouse wheel), Use uses it.
    static constexpr std::array<ActionID, 9> ActionSlots = {
        MakeAction("Hotbar_1"), MakeAction("Hotbar_2"), MakeAction("Hotbar_3"), MakeAction("Hotbar_4"), MakeAction("Hotbar_5"),
        MakeAction("Hotbar_6"), MakeAction("Hotbar_7"), MakeAction("Hotbar_8"), MakeAction("Hotbar_9"),
    };
    static constexpr ActionID ActionNext = MakeAction("Hotbar_Next");
    static constexpr ActionID ActionPrev = MakeAction("Hotbar_Prev");
    static constexpr ActionID ActionUse  = MakeAction("Hotbar_Use");

    // The section a grid bag's real hotbar is.
    static constexpr std::string_view Section = "hotbar";

    std::string  LayoutPath = "assets/engine/ui/hotbar.arcui";
    InventoryArt Art;
    HotbarHost   Host;

    HotbarScreen();

    // What each slot holds (an item id; empty: nothing), left to right.
    [[nodiscard]] std::vector<std::string> GetSlots() const;

    [[nodiscard]] u32         GetSelected() const { return _selected; }
    void                      Select(u32 slot);
    void                      Step(i32 by); // wrapping round
    [[nodiscard]] std::string GetSelectedItem() const;

protected:
    void OnEnter() override;
    void OnUpdate(f32 dt) override;

private:
    void Show();

    u32                    _selected = 0;
    bool                   _built    = false;
    std::vector<UIWidget*> _slotViews;
};
} // namespace Arcbit
