// InventoryScreen: the grid view of a grid bag - a cell widget for each cell
// of the main grid and its sections, the items drawn across the cells they
// take, and dragging them about: moved, turned, split, merged, swapped.

#include <arcbit/ui/InventoryScreen.h>

#include <arcbit/items/Inventories.h>
#include <arcbit/items/ItemLibrary.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <format>

namespace Arcbit
{
namespace
{
    constexpr f32   SectionLabel = 26.0f; // a section's name above it
    constexpr Color CellTint     = { 0.16f, 0.19f, 0.31f, 1.0f };
    constexpr Color FitsTint     = { 0.25f, 0.55f, 0.30f, 1.0f };
    constexpr Color NoFitTint    = { 0.60f, 0.22f, 0.22f, 1.0f };

    std::string ShapeOf(const InventoryRules& rules)
    {
        std::string shape = std::format("{}x{}", rules.Width, rules.Height);
        for (const InventorySection& s : rules.Sections) shape += std::format(";{}:{}x{}", s.Name, s.Width, s.Height);
        return shape;
    }
} // namespace

void InventoryScreen::ToggleView()
{
    _gridView  = !_gridView;
    _helpShown = false; // the grid view's keys differ
    Refresh();
    FocusAt(0);
}

// The grid view's widgets: cells rebuilt when the bag's shape changes, the
// items laid over them every refresh.
void InventoryScreen::BuildGrid()
{
    const Inventory* bag  = GetBag();
    const bool       grid = IsGridView();
    for (const char* name : { "list", "tabs" })
        if (auto* w = FindWidget<UIWidget>(name)) w->Visible = !grid;
    if (auto* empty = FindWidget<UIWidget>("empty")) empty->Visible = empty->Visible && !grid;
    if (auto* area = FindWidget<UIWidget>("grid-area")) area->Visible = grid;
    if (auto* view = FindWidget<NineSliceButton>("view")) {
        view->Visible = bag && bag->GetRules().IsGrid();
        view->Text    = Loc::Get(grid ? "ui.inventory.view.list" : "ui.inventory.view.grid");
        view->Texture = Art.Tab, view->Sampler = Art.Sampler;
    }
    if (!grid) return;
    if (ShapeOf(bag->GetRules()) != _gridShape) BuildCells(bag->GetRules());
    ShowGridItems();
}

// A GridPanel of cells per area - the main grid, then each section in a row
// beneath it with its name - sized so the whole bag fits "grid-area".
void InventoryScreen::BuildCells(const InventoryRules& rules)
{
    auto* area = FindWidget<UIWidget>("grid-area");
    if (!area) return;
    const i32 focus = GetFocusIndex();
    area->ClearChildren();
    _cells.clear();
    _itemViews.clear();
    _areaOrigins.clear();
    _gridShape = ShapeOf(rules);

    u32 sectionRows = 0, sectionCols = 0;
    for (const InventorySection& s : rules.Sections) sectionRows = std::max(sectionRows, s.Height), sectionCols += s.Width + 1;
    const f32 rows  = static_cast<f32>(rules.Height + sectionRows);
    const f32 fitW  = (area->Size.X - _cellGap * static_cast<f32>(std::max(rules.Width, sectionCols))) / static_cast<f32>(std::max(rules.Width, sectionCols));
    const f32 fitH  = (area->Size.Y - (sectionRows ? SectionLabel + _cellGap * 2.0f : 0.0f) - _cellGap * rows) / rows;
    _cellSize       = std::floor(std::min({ GetMetaF32("cell_size", 56.0f), fitW, fitH }));

    const auto addArea = [&](const std::string& section, const u32 w, const u32 h, const Vec2 at) {
        auto* panel     = area->AddChild<GridPanel>();
        panel->Columns  = w;
        panel->CellSize = { _cellSize, _cellSize };
        panel->Gap      = { _cellGap, _cellGap };
        panel->Offset   = at;
        panel->ZOrder   = 3;
        _areaOrigins[section] = at;
        for (u32 i = 0; i < w * h; ++i)
            if (auto* cell = dynamic_cast<NineSliceButton*>(AddFromTemplate("cell", *panel)))
                _cells.push_back({ cell, section, i % w, i / w });
    };
    addArea("", rules.Width, rules.Height, { 0.0f, 0.0f });
    f32 x = 0.0f;
    const f32 y = static_cast<f32>(rules.Height) * (_cellSize + _cellGap) + _cellGap;
    for (const InventorySection& s : rules.Sections) {
        auto* label   = area->AddChild<Label>();
        label->Text   = NameOr("items.section." + s.Name, s.Name);
        label->Offset = { x, y }, label->Size = { static_cast<f32>(s.Width) * (_cellSize + _cellGap), SectionLabel };
        label->ZOrder = 3;
        addArea(s.Name, s.Width, s.Height, { x, y + SectionLabel });
        x += static_cast<f32>(s.Width + 1) * (_cellSize + _cellGap);
    }
    for (u32 i = 0; i < _cells.size(); ++i) WireCell(i);
    if (focus >= 0) FocusAt(focus);
}

// A cell: selects (and, selected, uses or equips) what covers it; drags it
// away; takes a drop.
void InventoryScreen::WireCell(const u32 index)
{
    GridCell& cell = _cells[index];
    cell.Widget->Name    = std::format("cell-{}-{}-{}", cell.Section.empty() ? "main" : cell.Section, cell.X, cell.Y);
    cell.Widget->Texture = Art.Slot, cell.Widget->Sampler = Art.Sampler;
    cell.Widget->OnClick = [this, index] {
        Later([this, index] {
            const Inventory*         bag = GetBag();
            const std::optional<u32> on  = bag && index < _cells.size() ? bag->StackAt(_cells[index].Section, _cells[index].X, _cells[index].Y) : std::nullopt;
            if (!on) return;
            if (_selected != on) {
                _selected = on;
                return Refresh();
            }
            const ItemDef* def = SelectedDef();
            if (def && !def->Use.empty()) UseSelected();
            else if (def && !def->Slot.empty()) EquipSelected();
        });
    };
    cell.Widget->OnDragStart = [this, index](const DragStart& how) -> std::optional<DragPayload> {
        const Inventory* bag = GetBag();
        const auto       on  = bag && index < _cells.size() ? bag->StackAt(_cells[index].Section, _cells[index].X, _cells[index].Y) : std::nullopt;
        return on ? GridPayload(*on, how) : std::nullopt;
    };
    cell.Widget->GetTooltip = [this, index] {
        const Inventory* bag = GetBag();
        const auto       on  = bag && index < _cells.size() ? bag->StackAt(_cells[index].Section, _cells[index].X, _cells[index].Y) : std::nullopt;
        const ItemDef*   def = on && Host.Items ? Host.Items->Find(bag->GetStacks()[*on].Item) : nullptr;
        return def ? std::format("<b>{}</b>\n{}", Loc::Get(def->NameKey), Loc::Get(def->DescriptionKey)) : std::string{};
    };
    cell.Widget->CanDrop = [this, index](const DragPayload& p) { return index < _cells.size() && CanPlace(p, _cells[index]); };
    cell.Widget->OnDrop  = [this, index](const DragPayload& p) {
        const ItemDrag* drag = AsItemDrag(p);
        if (!drag || !drag->Index || index >= _cells.size()) return;
        const GridCell at = _cells[index];
        Later([this, at, from = *drag->Index, rotated = p.Rotated, count = p.Count] { (void)PlaceAt(from, at.Section, at.X, at.Y, rotated, count); });
    };
}

// An entry picked up from the grid: its icon across its cells, turned as it
// lies; the alternate drag (right button, Shift) takes half.
std::optional<DragPayload> InventoryScreen::GridPayload(const u32 index, const DragStart& how) const
{
    const Inventory* bag   = GetBag();
    const ItemStack& stack = bag->GetStacks()[index];
    const ItemDef*   def   = Host.Items ? Host.Items->Find(stack.Item) : nullptr;
    if (!def) return std::nullopt;
    const u32 count = how.Alternate && stack.Count > 1 ? stack.Count / 2 : stack.Count;
    const auto span = [this](const u32 cells) { return static_cast<f32>(cells) * _cellSize + static_cast<f32>(cells - 1) * _cellGap; };
    DragPayload payload{ .Kind = "item", .Data = ItemDrag{ stack.Item, index }, .Count = count, .Rotatable = def->Rotatable,
                         .Rotated = stack.Rotated, .Tint = { 0.30f, 0.36f, 0.56f, 1.0f }, .Size = { span(def->Width), span(def->Height) } };
    payload.Text = count > 1 ? std::format("{}", count) : std::string{};
    if (const auto icon = Host.LoadIcon ? Host.LoadIcon(def->Icon) : std::nullopt)
        payload.Texture = icon->Texture, payload.Sampler = icon->Sampler, payload.UV = icon->UV, payload.Tint = { 1, 1, 1, 1 };
    return payload;
}

// Whether a drop on this cell would land: the entry fits there (as it's
// held, turned or not), or what's there is the same item (merge) or another
// to trade places with.
bool InventoryScreen::CanPlace(const DragPayload& p, const GridCell& cell) const
{
    const ItemDrag*  drag = AsItemDrag(p);
    const Inventory* bag  = GetBag();
    if (!drag || !drag->Index || !bag || *drag->Index >= bag->GetStacks().size()) return false;
    if (bag->CanMoveTo(*drag->Index, cell.Section, cell.X, cell.Y, p.Rotated)) return true;
    const std::optional<u32> there = bag->StackAt(cell.Section, cell.X, cell.Y);
    return there && *there != *drag->Index && p.Count == bag->GetStacks()[*drag->Index].Count;
}

bool InventoryScreen::PlaceAt(const u32 index, const std::string_view section, const u32 x, const u32 y, const bool rotated, const u32 count)
{
    Inventory* bag = GetBag();
    if (!bag || index >= bag->GetStacks().size()) return false;
    const ItemStack          moving = bag->GetStacks()[index];
    const std::optional<u32> there  = bag->StackAt(section, x, y);
    bool                     done   = false;
    const bool sameThere = there && *there != index && bag->GetStacks()[*there].Item == moving.Item;
    if (count < moving.Count && sameThere) { // split onto more of it: the part joins them
        const u32 onto = *there > index ? *there + 1 : *there; // the part goes in right after `index`
        done           = bag->Split(index, count) && bag->Merge(index + 1, onto) > 0;
    }
    else if (count < moving.Count) done = bag->SplitTo(index, count, section, x, y, rotated); // the rest stays put
    else if (bag->CanMoveTo(index, section, x, y, rotated)) done = bag->MoveTo(index, section, x, y, rotated);
    else if (sameThere) done = bag->Merge(index, *there) > 0;
    else if (there && *there != index) done = bag->Swap(index, *there);
    if (!done) SetMessage("ui.inventory.no_fit", { { "item", Host.Items && Host.Items->Find(moving.Item) ? Loc::Get(Host.Items->Find(moving.Item)->NameKey) : moving.Item } });
    Refresh();
    return done;
}

// Each entry drawn across its cells: icon, count, the selected one marked,
// those the filter leaves out dimmed.
void InventoryScreen::ShowGridItems()
{
    auto*            area = FindWidget<UIWidget>("grid-area");
    const Inventory* bag  = GetBag();
    if (!area || !bag) return;
    for (UIWidget* view : _itemViews) view->Visible = false;
    const auto& stacks = bag->GetStacks();
    const auto  kept   = bag->Filter({ .Text = _filterText });
    while (_itemViews.size() < stacks.size())
        if (UIWidget* made = AddFromTemplate("grid-item", *area)) _itemViews.push_back(made);
        else break;
    for (u32 i = 0; i < stacks.size() && i < _itemViews.size(); ++i) {
        const ItemStack& stack = stacks[i];
        const ItemDef*   def   = Host.Items ? Host.Items->Find(stack.Item) : nullptr;
        u32              w = 1, h = 1;
        bag->Footprint(i, w, h);
        const auto origin = _areaOrigins.find(stack.Section);
        if (origin == _areaOrigins.end()) continue;
        UIWidget& view = *_itemViews[i];
        view.Visible   = true;
        view.ZOrder    = 5;
        view.Offset    = origin->second + Vec2{ static_cast<f32>(stack.X) * (_cellSize + _cellGap), static_cast<f32>(stack.Y) * (_cellSize + _cellGap) };
        view.Size      = { static_cast<f32>(w) * (_cellSize + _cellGap) - _cellGap, static_cast<f32>(h) * (_cellSize + _cellGap) - _cellGap };
        view.Opacity   = std::ranges::find(kept, i) != kept.end() ? 1.0f : 0.3f;
        if (auto* panel = dynamic_cast<Panel*>(&view))
            panel->SkinOverride.PanelBg = _selected == i ? SelectedTint : Color{ 0.26f, 0.32f, 0.50f, 0.85f };
        if (auto* icon = view.FindDescendant("icon")) {
            SetIcon(icon, def ? def->Icon : std::string_view{});
            icon->Size = view.Size - Vec2{ 8.0f, 8.0f };
        }
        if (auto* count = dynamic_cast<Label*>(view.FindDescendant("count"))) count->Text = stack.Count > 1 ? std::format("{}", stack.Count) : "";
        if (auto* name = dynamic_cast<Label*>(view.FindDescendant("name"))) name->Text = def && !def->Icon.empty() ? "" : (def ? Loc::Get(def->NameKey) : stack.Item);
    }
}

// While an entry is dragged over a cell: the cells it would take there,
// green if it fits, red if not.
void InventoryScreen::ShowDropPreview()
{
    for (GridCell& cell : _cells) cell.Widget->TintNormal = CellTint;
    const DragPayload* p      = GetDrag();
    const UIWidget*    target = GetDropTarget();
    const ItemDrag*    drag   = p ? AsItemDrag(*p) : nullptr;
    const auto         over   = std::ranges::find_if(_cells, [&](const GridCell& c) { return c.Widget == target; });
    if (!drag || !drag->Index || over == _cells.end() || !Host.Items) return;
    const ItemDef* def = Host.Items->Find(drag->Item);
    if (!def) return;
    const u32   w = p->Rotated ? def->Height : def->Width, h = p->Rotated ? def->Width : def->Height;
    const Color tint = CanPlace(*p, *over) ? FitsTint : NoFitTint;
    for (GridCell& cell : _cells)
        if (cell.Section == over->Section && cell.X >= over->X && cell.X < over->X + w && cell.Y >= over->Y && cell.Y < over->Y + h)
            cell.Widget->TintNormal = tint;
}
} // namespace Arcbit
