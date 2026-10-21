// Inventory: grids - placing sized items in cells, sections, repacking,
// and changing a bag into another kind.

#include <arcbit/items/Inventory.h>

#include <arcbit/core/Log.h>
#include <arcbit/items/ItemLibrary.h>

#include <algorithm>
#include <limits>

namespace Arcbit
{
namespace
{
void Mark(std::vector<i32>& cells, const u32 rowWidth, const u32 x, const u32 y, const u32 w, const u32 h, const i32 index)
{
    for (u32 row = y; row < y + h; ++row)
        for (u32 col = x; col < x + w; ++col) cells[row * rowWidth + col] = index;
}
} // namespace

InventoryRules InventoryRules::Grid(const u32 width, const u32 height)
{
    return { .Kind = "grid", .Width = width, .Height = height };
}

InventoryRules InventoryRules::Survival(const u32 width, const u32 height, const f32 maxWeight)
{
    return { .Kind = "survival", .MaxWeight = maxWeight, .Width = width, .Height = height };
}

// The main grid first, then each section; every stack's cells marked (but `skip`'s).
std::vector<Inventory::Area> Inventory::BuildAreas(const i32 skip) const
{
    std::vector<Area> areas;
    areas.push_back({ nullptr, _rules.Width, _rules.Height, std::vector<i32>(static_cast<usize>(_rules.Width) * _rules.Height, -1) });
    for (const InventorySection& section : _rules.Sections)
        areas.push_back({ &section, section.Width, section.Height, std::vector<i32>(static_cast<usize>(section.Width) * section.Height, -1) });
    for (i32 i = 0; i < static_cast<i32>(_stacks.size()); ++i) {
        if (i == skip) continue;
        u32   w = 0, h = 0;
        Area* area = FindArea(areas, _stacks[i].Section);
        Footprint(static_cast<u32>(i), w, h);
        if (area && _stacks[i].X + w <= area->Width && _stacks[i].Y + h <= area->Height)
            Mark(area->Cells, area->Width, _stacks[i].X, _stacks[i].Y, w, h, i);
    }
    return areas;
}

Inventory::Area* Inventory::FindArea(std::vector<Area>& areas, const std::string_view section) const
{
    for (Area& area : areas)
        if ((area.Section ? std::string_view(area.Section->Name) : std::string_view{}) == section) return &area;
    return nullptr;
}

bool Inventory::Accepts(const Area& area, const ItemDef& def) const
{
    return !area.Section || area.Section->Category.empty() || area.Section->Category == def.Category;
}

void Inventory::Footprint(const u32 index, u32& width, u32& height) const
{
    width = height = 1;
    if (index >= _stacks.size()) return;
    if (const ItemDef* def = Def(_stacks[index].Item)) width = def->Width, height = def->Height;
    if (_stacks[index].Rotated) std::swap(width, height);
}

// Whether the item fits at a place, every cell in reach and free.
bool Inventory::FitsAt(std::vector<Area>& areas, const ItemDef& def, const Place& place) const
{
    Area* area = FindArea(areas, place.Section);
    if (!area || !Accepts(*area, def)) return false;
    if (place.Rotated && !def.Rotatable && def.Width != def.Height) return false;
    const u32 w = place.Rotated ? def.Height : def.Width;
    const u32 h = place.Rotated ? def.Width : def.Height;
    if (place.X + w > area->Width || place.Y + h > area->Height) return false;
    for (u32 row = place.Y; row < place.Y + h; ++row)
        for (u32 col = place.X; col < place.X + w; ++col)
            if (area->Cells[row * area->Width + col] != -1) return false;
    return true;
}

// The first free space, row by row: the main grid, then the sections; each
// cell upright first, then turned.
std::optional<Inventory::Place> Inventory::FindPlace(std::vector<Area>& areas, const ItemDef& def, const bool mainOnly) const
{
    for (const Area& area : areas) {
        if (mainOnly && area.Section) break;
        const std::string section = area.Section ? area.Section->Name : std::string{};
        for (u32 y = 0; y < area.Height; ++y)
            for (u32 x = 0; x < area.Width; ++x)
                for (const bool rotated : { false, true }) {
                    if (rotated && def.Width == def.Height) continue;
                    if (const Place place{ section, x, y, rotated }; FitsAt(areas, def, place)) return place;
                }
    }
    return std::nullopt;
}

// How many of `count` new stacks could hold, placed one after another.
u32 Inventory::NewStackRoom(const ItemDef& def, const u32 count) const
{
    std::vector<Area> areas = BuildAreas();
    const u32         limit = StackLimit(def);
    u64               room  = 0;
    while (room < count) {
        const std::optional<Place> place = FindPlace(areas, def);
        if (!place) break;
        Area* area = FindArea(areas, place->Section);
        Mark(area->Cells, area->Width, place->X, place->Y, place->Rotated ? def.Height : def.Width,
             place->Rotated ? def.Width : def.Height, std::numeric_limits<i32>::max());
        room += limit;
    }
    return static_cast<u32>(std::min<u64>(room, count));
}

// A new stack's place in the grid (Room has already said there is one).
void Inventory::PlaceNew(ItemStack& stack)
{
    std::vector<Area>          areas = BuildAreas();
    const ItemDef*             def   = Def(stack.Item);
    const std::optional<Place> place = def ? FindPlace(areas, *def) : std::nullopt;
    if (!place) return;
    stack.Section = place->Section;
    stack.X       = place->X;
    stack.Y       = place->Y;
    stack.Rotated = place->Rotated;
}

bool Inventory::MoveTo(const u32 index, const std::string_view section, const u32 x, const u32 y, const bool rotated)
{
    if (!_rules.IsGrid() || index >= _stacks.size()) return false;
    const ItemDef* def = Def(_stacks[index].Item);
    std::vector<Area> areas = BuildAreas(static_cast<i32>(index));
    const Place       place{ std::string(section), x, y, rotated };
    if (!def || !FitsAt(areas, *def, place)) return false;
    ItemStack& stack = _stacks[index];
    stack.Section    = place.Section;
    stack.X          = x;
    stack.Y          = y;
    stack.Rotated    = rotated;
    return true;
}

bool Inventory::Rotate(const u32 index)
{
    return index < _stacks.size() && MoveTo(index, _stacks[index].Section, _stacks[index].X, _stacks[index].Y, !_stacks[index].Rotated);
}

// Each lands where the other was, keeping its own turn.
bool Inventory::Swap(const u32 a, const u32 b)
{
    if (!_rules.IsGrid() || a == b || a >= _stacks.size() || b >= _stacks.size()) return false;
    const ItemDef* defA = Def(_stacks[a].Item);
    const ItemDef* defB = Def(_stacks[b].Item);
    if (!defA || !defB) return false;
    std::vector<Area> areas = BuildAreas(static_cast<i32>(a));
    for (Area& area : areas) std::ranges::replace(area.Cells, static_cast<i32>(b), -1);
    const Place atA{ _stacks[a].Section, _stacks[a].X, _stacks[a].Y, _stacks[b].Rotated };
    const Place atB{ _stacks[b].Section, _stacks[b].X, _stacks[b].Y, _stacks[a].Rotated };
    if (!FitsAt(areas, *defB, atA)) return false;
    Area* area = FindArea(areas, atA.Section);
    Mark(area->Cells, area->Width, atA.X, atA.Y, atA.Rotated ? defB->Height : defB->Width, atA.Rotated ? defB->Width : defB->Height,
         static_cast<i32>(b));
    if (!FitsAt(areas, *defA, atB)) return false;
    std::swap(_stacks[a].Section, _stacks[b].Section);
    std::swap(_stacks[a].X, _stacks[b].X);
    std::swap(_stacks[a].Y, _stacks[b].Y);
    return true;
}

std::optional<u32> Inventory::StackAt(const std::string_view section, const u32 x, const u32 y) const
{
    std::vector<Area> areas = BuildAreas();
    const Area*       area  = FindArea(areas, section);
    if (!area || x >= area->Width || y >= area->Height) return std::nullopt;
    const i32 index = area->Cells[y * area->Width + x];
    return index >= 0 ? std::optional<u32>(static_cast<u32>(index)) : std::nullopt;
}

// The main grid laid out again in `order` (sections stay as they are).
bool Inventory::Repack(const std::vector<ItemStack>& order)
{
    _stacks = order;
    for (ItemStack& stack : _stacks)
        if (stack.Section.empty()) stack.X = stack.Y = std::numeric_limits<u32>::max() / 2; // off the grid while placing
    std::vector<Area> areas = BuildAreas();
    for (u32 i = 0; i < _stacks.size(); ++i) {
        if (!_stacks[i].Section.empty()) continue;
        const ItemDef*             def   = Def(_stacks[i].Item);
        const std::optional<Place> place = def ? FindPlace(areas, *def, true) : std::nullopt;
        if (!place) return false;
        _stacks[i].X       = place->X;
        _stacks[i].Y       = place->Y;
        _stacks[i].Rotated = place->Rotated;
        Mark(areas[0].Cells, areas[0].Width, place->X, place->Y, place->Rotated ? def->Height : def->Width,
             place->Rotated ? def->Width : def->Height, static_cast<i32>(i));
    }
    return true;
}

bool Inventory::Sort(const InventorySort by, const bool descending)
{
    if (!_rules.IsGrid()) {
        SortList(by, descending);
        return true;
    }
    const std::vector<ItemStack> before = _stacks;
    SortList(by, descending);
    const std::vector<ItemStack> sorted = _stacks;
    if (Repack(sorted)) return true;
    std::vector<ItemStack> largest = sorted;
    std::ranges::stable_sort(largest, [this](const ItemStack& a, const ItemStack& b) {
        const ItemDef* x = Def(a.Item);
        const ItemDef* y = Def(b.Item);
        return (x ? x->Width * x->Height : 1) > (y ? y->Width * y->Height : 1);
    });
    if (Repack(largest)) return true;
    _stacks = before;
    return false;
}

// --- Changing bags --------------------------------------------------------------

std::vector<ItemStack> Inventory::ConvertOverflow(const InventoryRules& to) const
{
    std::vector<ItemStack> overflow;
    (void)ConvertedTo(to, overflow);
    return overflow;
}

bool Inventory::Convert(const InventoryRules& to)
{
    std::vector<ItemStack> overflow;
    Inventory              next = ConvertedTo(to, overflow);
    if (!overflow.empty()) return false;
    _rules  = next._rules;
    _stacks = std::move(next._stacks);
    _quickSlots.resize(_rules.QuickSlots);
    return true;
}

// This bag's items in a bag with other rules, junk last (so it's what's left
// out first). A grid keeps each where it was if it still fits there; when
// that leaves something out, a fresh largest-first packing is tried too, and
// whichever leaves out fewer things that aren't junk (then fewer at all) wins.
Inventory Inventory::ConvertedTo(const InventoryRules& to, std::vector<ItemStack>& overflow) const
{
    const auto junk = [this](const ItemStack* s) { const ItemDef* d = Def(s->Item); return d && d->IsJunk(); };
    std::vector<const ItemStack*> order;
    for (const ItemStack& stack : _stacks) order.push_back(&stack);
    std::ranges::stable_partition(order, [&](const ItemStack* s) { return !junk(s); });
    Inventory kept = Fill(to, order, true, overflow);
    if (overflow.empty() || !to.IsGrid()) return kept;

    const auto area = [this](const ItemStack* s) { const ItemDef* d = Def(s->Item); return d ? d->Width * d->Height : 1u; };
    std::ranges::stable_sort(order, [&](const ItemStack* a, const ItemStack* b) {
        return junk(a) != junk(b) ? junk(b) : area(a) > area(b);
    });
    std::vector<ItemStack> packedOverflow;
    Inventory              packed = Fill(to, order, false, packedOverflow);
    const auto score = [&](const std::vector<ItemStack>& out) {
        u64 serious = 0, all = 0;
        for (const ItemStack& s : out) {
            all += s.Count;
            if (!junk(&s)) serious += s.Count;
        }
        return std::pair{ serious, all };
    };
    if (score(packedOverflow) < score(overflow)) {
        overflow = std::move(packedOverflow);
        return packed;
    }
    return kept;
}

Inventory Inventory::Fill(const InventoryRules& to, const std::vector<const ItemStack*>& order, const bool keepPlaces,
                          std::vector<ItemStack>& overflow) const
{
    Inventory next(*_items, to);
    next._clock = _clock;
    overflow.clear();
    for (const ItemStack* stack : order) {
        const u32 kept = next.Take(*stack, keepPlaces && _rules.IsGrid()); // a list's stacks have no places
        if (kept < stack->Count) overflow.push_back({ .Item = stack->Item, .Count = stack->Count - kept });
    }
    // Taken in priority order; listed in the player's order again.
    const auto rank = [this](const ItemStack& s) {
        return std::ranges::find(_stacks, s.Item, &ItemStack::Item) - _stacks.begin();
    };
    std::ranges::stable_sort(next._stacks, {}, rank);
    return next;
}

// A stack from another bag: at its old place if this is a grid, asked to
// and it fits there, else wherever Add puts it. Returns how many it took.
u32 Inventory::Take(const ItemStack& stack, const bool keepPlace)
{
    const ItemDef* def = Def(stack.Item);
    if (!def) return 0;
    if (keepPlace && _rules.IsGrid()) {
        std::vector<Area> areas = BuildAreas();
        const Place       was{ stack.Section, stack.X, stack.Y, stack.Rotated };
        const u32         fits = WeightRoom(*def, std::min(stack.Count, StackLimit(*def)));
        if (fits > 0 && FitsAt(areas, *def, was)) {
            _stacks.push_back({ .Item = stack.Item, .Count = fits, .Acquired = stack.Acquired, .Section = stack.Section,
                                .X = stack.X, .Y = stack.Y, .Rotated = stack.Rotated });
            return fits + Add(stack.Item, stack.Count - fits);
        }
    }
    return Add(stack.Item, stack.Count);
}
} // namespace Arcbit
