#pragma once

#include <arcbit/items/Inventory.h>
#include <arcbit/ui/InventoryScreen.h>

#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
class NineSliceButton;

// ---------------------------------------------------------------------------
// DiscardScreen - changing a bag to one that holds less (docs/items.md#changing-bags):
// every entry, with what won't fit marked to go - junk first, as
// Inventory::ConvertOverflow suggests. The player changes the marks; Confirm
// (only once what's left fits) discards the marked entries and changes the
// bag; Cancel, or Back, leaves it as it was. OnDone says which.
//
// Layout (LayoutPath) replaceable: "title", "message", "list" (ScrollPanel),
// "status", "confirm", "cancel", and the "row" template (with "mark",
// "icon", "name", "count").
// ---------------------------------------------------------------------------
class DiscardScreen : public UIScreen
{
public:
    std::string               LayoutPath = "assets/engine/ui/discard.arcui";
    InventoryArt              Art;
    Inventory*                Bag = nullptr;
    InventoryRules            To;
    const ItemLibrary*        Items = nullptr;
    std::function<std::optional<ItemIcon>(std::string_view)> LoadIcon;
    std::function<void(bool changed)> OnDone;

    DiscardScreen();

    // The entries (by index) marked to go, in order.
    [[nodiscard]] const std::vector<u32>& GetMarked() const { return _marked; }
    void                                  Toggle(u32 index);

    // Whether what isn't marked fits the new bag.
    [[nodiscard]] bool CanConfirm() const;
    void               Confirm();
    void               Cancel();

protected:
    void OnEnter() override;
    void OnUpdate(f32 dt) override;
    void OnBackPressed() override { Cancel(); }
    void OnLocaleChanged() override { Show(); }

private:
    void Show();

    std::vector<u32>                   _marked;
    std::vector<NineSliceButton*>      _rows;
    bool                               _built = false;
    bool                               _done  = false;
    std::vector<std::function<void()>> _later;
};
} // namespace Arcbit
