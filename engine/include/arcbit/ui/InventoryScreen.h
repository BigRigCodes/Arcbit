#pragma once

#include <arcbit/input/InputTypes.h>
#include <arcbit/items/Inventory.h>
#include <arcbit/items/Party.h>
#include <arcbit/ui/UIScreen.h>

#include <any>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Currencies;
class Inventories;
class ItemLibrary;
class Label;
class NineSliceButton;
class ScrollPanel;
class StatSet;

// The stock art (assets/engine/ui/inventory/): the screen's panel, the rows
// and tabs (drawn white, tinted per state), and the well an icon sits in.
struct InventoryArt
{
    TextureHandle Panel;
    TextureHandle Row;
    TextureHandle Tab;
    TextureHandle Slot;
    SamplerHandle Sampler;
};

// An item's icon, ready to draw.
struct ItemIcon
{
    TextureHandle Texture;
    SamplerHandle Sampler;
    UVRect        UV;
};

// What an inventory drag carries (DragPayload::Data, Kind "item"): the item,
// and its entry in the screen's bag (none: from elsewhere - a container).
struct ItemDrag
{
    std::string        Item;
    std::optional<u32> Index;
};

// The ItemDrag a payload carries; null if it carries something else.
[[nodiscard]] inline const ItemDrag* AsItemDrag(const DragPayload& payload)
{
    return payload.Kind == "item" ? std::any_cast<ItemDrag>(&payload.Data) : nullptr;
}

// What the screen shows and acts on - Application fills it in.
struct InventoryHost
{
    const ItemLibrary* Items   = nullptr;
    Inventories*       Bags    = nullptr;
    Party*             Members = nullptr; // null: no equipment panel
    const StatSet*     Stats   = nullptr;
    Currencies*        Money   = nullptr; // null: no currencies line

    std::function<UseResult(std::string_view item, std::string_view member, std::string_view bag)> Use;
    std::function<bool(std::string_view item, u32 count, std::string_view bag)> Drop; // null: no Drop button
    std::function<std::optional<ItemIcon>(std::string_view icon)> LoadIcon;           // an ItemDef::Icon
};

// ---------------------------------------------------------------------------
// InventoryScreen - a bag of the list kinds (docs/items.md#the-inventory-screen):
// tabs (pockets: a pocket per category; otherwise All and the categories it
// holds), a row per entry (icon, name, count, weight), sort and a text filter,
// the carry weight, the currencies; the selected item's details and actions
// (Use, Equip, Drop); and the party's equipment - a member's slots (click to
// take off, drop an item on one to wear it) and their stats.
//
// Rows select on focus or a click; activating the selected one (Enter, a
// second click) uses or equips it. Dragging a row onto another moves it.
// Q / E (shoulder buttons) change tabs.
//
// The layout (LayoutPath) is an .arcui a game can replace, as the dialog
// box's is; its widgets and templates are named in docs/items.md.
// ---------------------------------------------------------------------------
class InventoryScreen : public UIScreen
{
public:
    static constexpr ActionID ActionPagePrev = MakeAction("UI_PagePrev");
    static constexpr ActionID ActionPageNext = MakeAction("UI_PageNext");

    std::string            LayoutPath = "assets/engine/ui/inventory.arcui";
    InventoryArt           Art;
    InventoryHost          Host;
    std::string            Bag = "party";
    Color                  SelectedTint = { 0.55f, 0.45f, 0.22f, 1.0f }; // the selected row
    std::function<void()>  OnClose;

    InventoryScreen();

    // --- What it shows (and what tests drive) -------------------------------

    [[nodiscard]] const std::vector<std::string>& GetTabs() const { return _tabs; } // categories; "" is All
    [[nodiscard]] u32                             GetTab() const { return _tab; }
    void                                          SelectTab(u32 index);

    // The bag entries shown, in order (indices into its stacks).
    [[nodiscard]] const std::vector<u32>& GetShown() const { return _shown; }
    [[nodiscard]] std::optional<u32>      GetSelected() const { return _selected; } // a bag entry
    void                                  Select(u32 row);                          // a shown row

    void SetFilterText(std::string text);
    void CycleSort();
    [[nodiscard]] InventorySort GetSort() const { return _sort; }

    // The selected item's actions; each leaves a message (GetMessage).
    void UseSelected();
    void EquipSelected();
    void DropSelected();
    void Unequip(std::string_view slot);

    // Put the selected item on a quick slot (a bag with quick slots); on it
    // already, take it off. The hotbar's number keys do it on this screen.
    void AssignQuickSlot(u32 slot);
    void SelectMember(i32 step); // the equipment panel's member, cycling

    [[nodiscard]] const std::string& GetMember() const { return _member; }
    [[nodiscard]] const std::string& GetMessage() const { return _messageText; }

    // Rebuild from the bag now (done each tick the bag or the party changed).
    void Refresh();

    // --- Grid bags (grid, survival) -----------------------------------------

    // A grid bag shows its grid - items drawn across the cells they take -
    // or, switched, a list of the same items with tabs and the filter.
    [[nodiscard]] bool IsGridView() const { return _gridView && GetBag() && GetBag()->GetRules().IsGrid(); }
    void               ToggleView();

    // What dropping an entry on a cell does: count less than it holds splits
    // that many off there; onto the same item, merges; onto another, they
    // trade places; else it moves - turned or not. False (a message says
    // why) if it doesn't fit.
    bool PlaceAt(u32 index, std::string_view section, u32 x, u32 y, bool rotated, u32 count);

    // A name from the locale files - key - else the id tidied ("key_items" ->
    // "Key items"), with a warning the first time, so a missing string shows.
    [[nodiscard]] static std::string NameOr(const std::string& key, std::string_view id);

protected:
    void OnEnter() override;
    void OnBackPressed() override;
    void OnTick(f32 dt, const InputManager& input) override;
    void OnUpdate(f32 dt) override;
    void OnLocaleChanged() override;

private:
    [[nodiscard]] Inventory* GetBag() const;
    [[nodiscard]] const ItemDef* SelectedDef() const;
    void BuildTabs();
    void BuildRows();
    void WireRow(UIWidget& row, u32 shownIndex);
    void FillRow(UIWidget& row, const ItemStack& stack) const;
    void ShowTabs();
    void ShowDetail();
    void ShowEquipment();
    void ShowFooter();
    void ShowActions(const ItemDef* def);
    void SetMessage(std::string_view key, const std::vector<std::pair<std::string, std::string>>& args = {});
    void SetIcon(UIWidget* image, std::string_view icon) const;
    void FollowFocus();
    void ScrollTo(u32 row);
    [[nodiscard]] std::string DescribeStats(const ItemDef& def) const;
    [[nodiscard]] std::string CompareEquip(const ItemDef& def) const;
    [[nodiscard]] std::string MemberStats() const;
    [[nodiscard]] u64         Signature() const; // the bag and the party as shown

    // The help line, from the keys bound now on the device in use (a gamepad's
    // buttons once it's picked up); redrawn when either changes.
    void        ShowHelp(const InputManager& input);
    InputDevice _helpDevice = InputDevice::KeyboardMouse;
    bool        _helpShown  = false;

    // A widget's callback runs its work at the next tick: a row's click may
    // rebuild the rows, and the row mustn't be destroyed mid-callback.
    std::vector<std::function<void()>> _later;
    void Later(std::function<void()> work) { _later.push_back(std::move(work)); }

    std::vector<std::string> _tabs;
    u32                      _tab = 0;
    std::vector<u32>         _shown;
    std::optional<u32>       _selected;
    std::string              _filterText;
    InventorySort            _sort = InventorySort::Name;
    std::string              _member;
    std::string              _messageKey;
    std::vector<std::pair<std::string, std::string>> _messageArgs;
    std::string              _messageText;
    u64                      _signature = 0;
    bool                     _built     = false;

    // The grid view: a cell widget per cell of each area (the main grid "",
    // then the sections), the item views drawn over them, and the cell size
    // that fits the bag in the space the layout gives it.
    struct GridCell
    {
        NineSliceButton* Widget = nullptr;
        std::string      Section;
        u32              X = 0, Y = 0;
    };
    std::vector<GridCell>       _cells;
    std::vector<UIWidget*>      _itemViews;
    std::map<std::string, Vec2> _areaOrigins; // each area's top-left in "grid-area"
    std::string                 _gridShape;   // what the cells were built for
    f32                         _cellSize = 48.0f;
    f32                         _cellGap  = 4.0f;
    std::optional<u32>          _dimmed;  // the entry being dragged, drawn faint where it lies
    bool                        _gridView = true;
    void BuildGrid();
    void BuildCells(const InventoryRules& rules);
    void WireCell(u32 cell);
    void ShowGridItems();
    void ShowDropPreview();
    [[nodiscard]] std::optional<DragPayload> GridPayload(u32 index, const DragStart& how) const;
    [[nodiscard]] bool                       CanPlace(const DragPayload& p, const GridCell& over) const;
    [[nodiscard]] std::optional<GridCell>    AnchorFor(const DragPayload& p, const GridCell& over) const;

    ScrollPanel*                   _list = nullptr;
    std::vector<NineSliceButton*>  _rows;
    std::vector<NineSliceButton*>  _tabButtons;
    std::vector<NineSliceButton*>  _slotRows;
    f32                            _rowHeight = 48.0f;
};
} // namespace Arcbit
