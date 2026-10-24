#pragma once

#include <arcbit/ui/InventoryScreen.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Currencies;
class Inventories;
class Inventory;
class ItemLibrary;
class NineSliceButton;
class ScrollPanel;
struct Container;

// What the transfer screen moves between - Application fills it in.
struct TransferHost
{
    const ItemLibrary*                                            Items = nullptr;
    Inventories*                                                  Bags  = nullptr;
    Currencies*                                                   Money = nullptr;
    std::function<Container*()>                                   Container; // null once it's gone (its scene unloaded)
    std::function<std::optional<ItemIcon>(std::string_view icon)> LoadIcon;
};

// ---------------------------------------------------------------------------
// TransferScreen - a container beside the party's bag (docs/items.md#containers):
// what each holds, as rows. A click or Enter on a row moves all of it to the
// other side (Shift: one); a row drags across too. Take All moves everything
// that fits, and the container's currency. It closes itself if the container
// goes. Layout (LayoutPath) replaceable: "title", "from" and "to"
// (ScrollPanels), "from-title", "to-title", "money", "take-all", "message",
// "help", and the "row" template (with "slot", "icon", "name", "count").
// ---------------------------------------------------------------------------
class TransferScreen : public UIScreen
{
public:
    enum class Side : u8 { Container, Bag };

    std::string           LayoutPath = "assets/engine/ui/transfer.arcui";
    InventoryArt          Art;
    TransferHost          Host;
    std::string           Bag = "party";
    std::string           Title; // the container's name; empty: "Container"
    std::function<void()> OnClose;

    TransferScreen();

    // Move an entry to the other side - all of it, or one; returns how many
    // went (fewer when the other side is full - a message says so).
    u32  Move(Side from, u32 index, bool one = false);
    void TakeAll(); // everything that fits, and the currency
    [[nodiscard]] const std::string& GetMessage() const { return _message; }
    void Refresh();

protected:
    void OnEnter() override;
    void OnUpdate(f32 dt) override;
    void OnTick(f32 dt, const InputManager& input) override;
    void OnBackPressed() override;
    void OnLocaleChanged() override { Refresh(); }

private:
    [[nodiscard]] Inventory* SideBag(Side side) const;
    void FillSide(Side side);
    void WireRow(UIWidget& row, Side side, u32 index);
    [[nodiscard]] u64 Signature() const;
    void Later(std::function<void()> work) { _later.push_back(std::move(work)); }

    std::vector<NineSliceButton*>      _rows[2];
    std::string                        _message;
    u64                                _signature = 0;
    bool                               _built     = false;
    bool                               _shift     = false; // held this tick: a click moves one
    std::vector<std::function<void()>> _later;
};
} // namespace Arcbit
