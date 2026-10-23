// InventoryScreen: building the screen, its tabs and rows, and following the
// bag (docs/items.md#the-inventory-screen). Details, equipment and the
// actions are in InventoryScreenDetail.cpp.

#include <arcbit/ui/InventoryScreen.h>

#include <arcbit/core/Log.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <any>
#include <array>
#include <format>
#include <utility>

namespace Arcbit
{
namespace
{
    // "weapons" -> "Weapons", "key_items" -> "Key items": a name for an id
    // the locale files don't have one for.
    std::string Humanize(const std::string_view id)
    {
        std::string name(id);
        std::ranges::replace(name, '_', ' ');
        if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
        return name;
    }

    std::string NameOr(const std::string& key, const std::string_view id) { return Loc::Has(key) ? Loc::Get(key) : Humanize(id); }

    std::string CategoryName(const std::string& category)
    {
        return category.empty() ? Loc::Get("ui.inventory.all") : NameOr("items.category." + category, category);
    }

    std::string CategoryOf(const ItemDef& def) { return def.Category.empty() ? def.Type : def.Category; }

    constexpr std::array SortOrder = { InventorySort::Name, InventorySort::Type, InventorySort::Value,
                                       InventorySort::Weight, InventorySort::Count, InventorySort::Recent };
    constexpr std::array SortKeys  = { "ui.inventory.sort.name", "ui.inventory.sort.type", "ui.inventory.sort.value",
                                       "ui.inventory.sort.weight", "ui.inventory.sort.count", "ui.inventory.sort.recent" };
} // namespace

InventoryScreen::InventoryScreen()
{
    BlocksInput     = true;
    TransitionSpeed = 6.0f;
}

Inventory* InventoryScreen::GetBag() const { return Host.Bags ? Host.Bags->Find(Bag) : nullptr; }

const ItemDef* InventoryScreen::SelectedDef() const
{
    const Inventory* bag = GetBag();
    if (!bag || !_selected || *_selected >= bag->GetStacks().size() || !Host.Items) return nullptr;
    return Host.Items->Find(bag->GetStacks()[*_selected].Item);
}

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

void InventoryScreen::OnEnter()
{
    if (!_built) {
        if (!LoadLayout(LayoutPath)) LOG_WARN(UI, "InventoryScreen: '{}' not found", LayoutPath);
        _list      = FindWidget<ScrollPanel>("list");
        _rowHeight = GetMetaF32("row_height", _rowHeight);
        if (auto* panel = FindWidget<NineSlice>("panel")) panel->Texture = Art.Panel, panel->Sampler = Art.Sampler;
        for (const char* well : { "detail-slot" })
            if (auto* image = FindWidget<Image>(well)) image->Texture = Art.Slot, image->Sampler = Art.Sampler;
        if (auto* sort = FindWidget<NineSliceButton>("sort")) {
            sort->Texture = Art.Tab, sort->Sampler = Art.Sampler;
            sort->OnClick = [this] { Later([this] { CycleSort(); }); };
        }
        if (auto* filter = FindWidget<TextInput>("filter"))
            filter->OnChanged = [this](const std::string& text) { Later([this, text] { SetFilterText(text); }); };
        for (const auto& [name, action] : { std::pair{ "use", &InventoryScreen::UseSelected }, std::pair{ "equip", &InventoryScreen::EquipSelected },
                                            std::pair{ "drop", &InventoryScreen::DropSelected } })
            if (auto* button = FindWidget<Button>(name)) button->OnClick = [this, action] { Later([this, action] { (this->*action)(); }); };
        if (auto* prev = FindWidget<Button>("member-prev")) prev->OnClick = [this] { Later([this] { SelectMember(-1); }); };
        if (auto* next = FindWidget<Button>("member-next")) next->OnClick = [this] { Later([this] { SelectMember(1); }); };
        _built = true;
    }
    if (_member.empty() && Host.Members && Host.Members->GetLeader()) _member = Host.Members->GetLeader()->Id;
    Refresh();
    FocusAt(0);
}

void InventoryScreen::Refresh()
{
    BuildTabs();
    BuildRows();
    ShowTabs();
    ShowDetail();
    ShowEquipment();
    ShowFooter();
    _signature = Signature();
}

// Pockets: one per category the game's items have, empty or not. Other
// kinds: All, then the categories the bag holds.
void InventoryScreen::BuildTabs()
{
    const Inventory* bag = GetBag();
    if (!bag || !Host.Items) return;
    const bool               pockets = bag->GetRules().Kind == "pockets";
    std::vector<std::string> categories;
    const auto               add = [&](const ItemDef* def) {
        if (def && std::ranges::find(categories, CategoryOf(*def)) == categories.end()) categories.push_back(CategoryOf(*def));
    };
    if (pockets)
        for (const std::string& id : Host.Items->GetIds()) add(Host.Items->Find(id));
    else
        for (const ItemStack& stack : bag->GetStacks()) add(Host.Items->Find(stack.Item));
    std::ranges::sort(categories, [](const std::string& a, const std::string& b) { return CategoryName(a) < CategoryName(b); });
    if (!pockets || categories.empty()) categories.insert(categories.begin(), std::string{});

    const std::string current = _tab < _tabs.size() ? _tabs[_tab] : std::string{};
    _tabs                     = std::move(categories);
    const auto kept           = std::ranges::find(_tabs, current);
    _tab                      = kept != _tabs.end() ? static_cast<u32>(kept - _tabs.begin()) : 0;
}

void InventoryScreen::ShowTabs()
{
    auto* grid = FindWidget<GridPanel>("tabs");
    if (!grid) return;
    if (_tabButtons.size() != _tabs.size()) {
        grid->ClearChildren();
        _tabButtons.clear();
        for (UIWidget* made : AddFromTemplate("tab", *grid, static_cast<u32>(_tabs.size())))
            if (auto* button = dynamic_cast<NineSliceButton*>(made)) _tabButtons.push_back(button);
    }
    for (u32 i = 0; i < _tabButtons.size(); ++i) {
        NineSliceButton& tab = *_tabButtons[i];
        tab.Texture = Art.Tab, tab.Sampler = Art.Sampler;
        tab.Text    = CategoryName(_tabs[i]);
        tab.TintNormal = i == _tab ? SelectedTint : Color{ 0.20f, 0.24f, 0.38f, 1.0f };
        tab.OnClick = [this, i] { Later([this, i] { SelectTab(i); }); };
    }
    if (auto* sort = FindWidget<NineSliceButton>("sort"))
        sort->Text = Loc::Format("ui.inventory.sort", { { "by", Loc::Get(SortKeys[std::ranges::find(SortOrder, _sort) - SortOrder.begin()]) } });
}

// The rows: the bag's entries in the tab's category whose names match the
// filter. Rows are made only when their number changes; focus stays on the
// row it was on.
void InventoryScreen::BuildRows()
{
    Inventory* bag = GetBag();
    _shown.clear();
    if (bag) _shown = bag->Filter({ .Category = _tab < _tabs.size() ? _tabs[_tab] : std::string{}, .Text = _filterText });
    // The selection is an entry's place: one used up or worn may leave it
    // past the end, or off this tab.
    if (_selected && std::ranges::find(_shown, *_selected) == _shown.end()) _selected.reset();
    if (!_selected && !_shown.empty()) _selected = _shown.front();
    if (_list && _rows.size() != _shown.size()) {
        const i32 focus = GetFocusIndex();
        _list->ClearChildren();
        _rows.clear();
        for (UIWidget* made : AddFromTemplate("row", *_list, static_cast<u32>(_shown.size())))
            if (auto* row = dynamic_cast<NineSliceButton*>(made)) _rows.push_back(row);
        if (focus >= 0) FocusAt(focus);
    }
    if (_list) _list->ContentHeight = _rowHeight * static_cast<f32>(_rows.size());
    for (u32 i = 0; i < _rows.size() && i < _shown.size(); ++i) {
        _rows[i]->Name   = std::format("row-{}", i);
        _rows[i]->Offset = { 0.0f, _rowHeight * static_cast<f32>(i) };
        WireRow(*_rows[i], i);
        FillRow(*_rows[i], bag->GetStacks()[_shown[i]]);
    }
    if (auto* empty = FindWidget<Label>("empty")) empty->Visible = _shown.empty();
}

// A click selects the row; a click on the selected row (or Enter) uses or
// equips it. Rows drag onto rows (moving the entry) and onto equipment slots.
void InventoryScreen::WireRow(UIWidget& row, const u32 shownIndex)
{
    auto& button = dynamic_cast<NineSliceButton&>(row);
    button.OnClick = [this, shownIndex] {
        Later([this, shownIndex] {
            if (shownIndex >= _shown.size()) return;
            if (_selected != _shown[shownIndex]) return Select(shownIndex);
            const ItemDef* def = SelectedDef();
            if (def && !def->Use.empty()) UseSelected();
            else if (def && !def->Slot.empty()) EquipSelected();
        });
    };
    row.OnDragStart = [this, shownIndex](const DragStart&) -> std::optional<DragPayload> {
        const Inventory* bag = GetBag();
        if (!bag || shownIndex >= _shown.size()) return std::nullopt;
        const ItemStack& stack = bag->GetStacks()[_shown[shownIndex]];
        DragPayload      payload{ .Kind = "item", .Data = stack.Item, .Count = stack.Count, .Size = { 40.0f, 40.0f } };
        payload.Text = stack.Count > 1 ? std::format("{}", stack.Count) : std::string{};
        const ItemDef* def = Host.Items ? Host.Items->Find(stack.Item) : nullptr;
        if (const auto icon = def && Host.LoadIcon ? Host.LoadIcon(def->Icon) : std::nullopt)
            payload.Texture = icon->Texture, payload.Sampler = icon->Sampler, payload.UV = icon->UV;
        return payload;
    };
    row.CanDrop = [](const DragPayload& p) { return p.Kind == "item"; };
    row.OnDrop  = [this, shownIndex](const DragPayload& p) {
        const std::string item = std::any_cast<std::string>(p.Data);
        Later([this, shownIndex, item] {
            Inventory* bag = GetBag();
            if (!bag || shownIndex >= _shown.size()) return;
            const auto& stacks = bag->GetStacks();
            const auto  from   = std::ranges::find_if(stacks, [&](const ItemStack& s) { return s.Item == item; });
            if (from != stacks.end()) (void)bag->Move(static_cast<u32>(from - stacks.begin()), _shown[shownIndex]);
            Refresh();
        });
    };
}

void InventoryScreen::FillRow(UIWidget& row, const ItemStack& stack) const
{
    const ItemDef* def = Host.Items ? Host.Items->Find(stack.Item) : nullptr;
    auto&          button = dynamic_cast<NineSliceButton&>(row);
    button.Texture = Art.Row, button.Sampler = Art.Sampler;
    button.TintNormal = _selected && GetBag() && &GetBag()->GetStacks()[*_selected] == &stack ? SelectedTint : Color{ 0.16f, 0.19f, 0.31f, 1.0f };
    if (auto* well = dynamic_cast<Image*>(row.FindDescendant("slot"))) well->Texture = Art.Slot, well->Sampler = Art.Sampler;
    SetIcon(row.FindDescendant("icon"), def ? def->Icon : std::string_view{});
    if (auto* name = dynamic_cast<Label*>(row.FindDescendant("name"))) name->Text = def ? Loc::Get(def->NameKey) : stack.Item;
    if (auto* count = dynamic_cast<Label*>(row.FindDescendant("count"))) count->Text = stack.Count > 1 ? std::format("x{}", stack.Count) : "";
    // Weight only matters (and only shows) in a bag with a carry limit.
    const bool weighed = GetBag() && GetBag()->GetRules().MaxWeight > 0.0f;
    if (auto* weight = dynamic_cast<Label*>(row.FindDescendant("weight")))
        weight->Text = weighed && def && def->Weight > 0.0f ? std::format("{:g}", def->Weight * static_cast<f32>(stack.Count)) : "";
    row.TooltipKey = def ? def->DescriptionKey : std::string{};
}

void InventoryScreen::SetIcon(UIWidget* widget, const std::string_view icon) const
{
    auto* image = dynamic_cast<Image*>(widget);
    if (!image) return;
    const auto loaded = !icon.empty() && Host.LoadIcon ? Host.LoadIcon(icon) : std::nullopt;
    image->Visible    = loaded.has_value();
    if (loaded) image->Texture = loaded->Texture, image->Sampler = loaded->Sampler, image->UV = loaded->UV;
}

// ---------------------------------------------------------------------------
// Choosing
// ---------------------------------------------------------------------------

void InventoryScreen::SelectTab(const u32 index)
{
    if (index >= _tabs.size()) return;
    _tab = index;
    _selected.reset();
    Refresh();
}

void InventoryScreen::Select(const u32 row)
{
    if (row >= _shown.size()) return;
    _selected = _shown[row];
    Refresh();
    ScrollTo(row);
}

void InventoryScreen::SetFilterText(std::string text)
{
    _filterText = std::move(text);
    Refresh();
}

void InventoryScreen::CycleSort()
{
    const auto at = std::ranges::find(SortOrder, _sort);
    _sort         = SortOrder[(at - SortOrder.begin() + 1) % SortOrder.size()];
    if (Inventory* bag = GetBag()) {
        const std::string selected = _selected ? bag->GetStacks()[*_selected].Item : std::string{};
        (void)bag->Sort(_sort, _sort == InventorySort::Value || _sort == InventorySort::Count || _sort == InventorySort::Recent);
        const auto& stacks = bag->GetStacks();
        const auto  again  = std::ranges::find_if(stacks, [&](const ItemStack& s) { return s.Item == selected; });
        if (again != stacks.end()) _selected = static_cast<u32>(again - stacks.begin()); // the same item, wherever it went
    }
    Refresh();
}

// Keyboard and gamepad focus moving down the rows selects as it goes.
void InventoryScreen::FollowFocus()
{
    const UIWidget* focused = GetFocusedWidget();
    for (u32 i = 0; i < _rows.size() && i < _shown.size(); ++i)
        if (_rows[i] == focused && _selected != _shown[i]) {
            Select(i);
            return;
        }
}

// The row in view: scrolled to when focus or a selection reaches it.
void InventoryScreen::ScrollTo(const u32 row)
{
    if (!_list) return;
    const f32 top = _rowHeight * static_cast<f32>(row), view = _list->Size.Y;
    if (top < _list->ScrollOffset) _list->ScrollOffset = top;
    else if (top + _rowHeight > _list->ScrollOffset + view) _list->ScrollOffset = top + _rowHeight - view;
}

// ---------------------------------------------------------------------------
// Each tick
// ---------------------------------------------------------------------------

void InventoryScreen::OnUpdate(f32)
{
    for (const auto& work : std::exchange(_later, {})) work();
    if (_built && Signature() != _signature) Refresh(); // the bag changed under it (a script, a pickup)
}

void InventoryScreen::OnTick(f32, const InputManager& input)
{
    FollowFocus();
    if (_tabs.empty()) return;
    const u32 count = static_cast<u32>(_tabs.size());
    if (input.JustPressed(ActionPagePrev)) SelectTab((_tab + count - 1) % count);
    if (input.JustPressed(ActionPageNext)) SelectTab((_tab + 1) % count);
}

void InventoryScreen::OnBackPressed()
{
    if (OnClose) OnClose();
}

void InventoryScreen::OnLocaleChanged()
{
    if (_built) Refresh();
}

// What Refresh depends on: the bag's entries, the member's equipment and the
// currencies - a change to any redraws the screen.
u64 InventoryScreen::Signature() const
{
    u64        hash = 1469598103934665603ull;
    const auto mix  = [&hash](const std::string_view text) {
        for (const char c : text) hash = (hash ^ static_cast<u8>(c)) * 1099511628211ull;
        hash = (hash ^ 0xFF) * 1099511628211ull;
    };
    if (const Inventory* bag = GetBag())
        for (const ItemStack& stack : bag->GetStacks()) mix(std::format("{}:{}", stack.Item, stack.Count));
    if (Host.Members)
        for (const PartyMember& member : Host.Members->GetMembers()) {
            mix(member.Id);
            for (const auto& [slot, item] : member.Equipment) mix(slot + item);
            for (const auto& [stat, value] : member.Own.Current) mix(std::format("{}{}", stat, value)); // HP after a potion
        }
    if (Host.Money)
        for (const CurrencyDef& currency : Host.Money->GetAll()) mix(std::format("{}", Host.Money->Get(currency.Id)));
    return hash;
}
} // namespace Arcbit
