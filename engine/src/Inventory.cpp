#include <arcbit/items/Inventory.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/items/ItemLibrary.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace Arcbit
{
namespace
{
using nlohmann::json;

constexpr u32 NoLimit = std::numeric_limits<u32>::max();

u32 SaturatingAdd(const u64 a, const u64 b) { return static_cast<u32>(std::min<u64>(a + b, NoLimit)); }

std::string Lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

json SectionsToJson(const std::vector<InventorySection>& sections)
{
    json out = json::array();
    for (const InventorySection& s : sections)
        out.push_back({ { "name", s.Name }, { "width", s.Width }, { "height", s.Height }, { "category", s.Category } });
    return out;
}

template<typename T>
int Compare(const T& a, const T& b)
{
    return a < b ? -1 : b < a ? 1 : 0;
}
} // namespace

InventoryRules InventoryRules::Pockets()
{
    return { .Kind = "pockets", .OneEntryPerItem = true };
}

InventoryRules InventoryRules::Weight(const f32 maxWeight)
{
    return { .Kind = "weight", .OneEntryPerItem = true, .UnlimitedStacks = true, .MaxWeight = maxWeight };
}

Inventory::Inventory(const ItemLibrary& items, InventoryRules rules) : _items(&items), _rules(std::move(rules))
{
    _quickSlots.resize(_rules.QuickSlots);
}

const ItemDef* Inventory::Def(const std::string_view item) const { return _items ? _items->Find(item) : nullptr; }

u32 Inventory::StackLimit(const ItemDef& def) const { return _rules.UnlimitedStacks ? NoLimit : def.Stack; }

std::string Inventory::NameOf(const std::string& item) const
{
    const ItemDef* def = Def(item);
    return def ? Loc::Get(def->NameKey) : item;
}

// --- Adding and removing ----------------------------------------------------

u32 Inventory::Room(const std::string_view item, const u32 count) const
{
    const ItemDef* def = Def(item);
    if (!def || count == 0) return 0;
    const u32 limit = StackLimit(*def);
    u32       room  = 0;
    bool      held  = false;
    for (const ItemStack& stack : _stacks) {
        if (stack.Item != item) continue;
        held = true;
        room = SaturatingAdd(room, limit - std::min(limit, stack.Count));
    }
    if (_rules.IsGrid()) return WeightRoom(*def, std::min(SaturatingAdd(room, NewStackRoom(*def, count - std::min(room, count))), count));
    const u64 freeEntries = _rules.Slots == 0 ? NoLimit : _rules.Slots - std::min<u64>(_rules.Slots, _stacks.size());
    if (_rules.OneEntryPerItem) {
        if (!held && freeEntries > 0) room = SaturatingAdd(room, limit);
    }
    else room = SaturatingAdd(room, std::min<u64>(freeEntries * limit, NoLimit));
    return WeightRoom(*def, std::min(room, count));
}

u32 Inventory::Add(const std::string_view item, const u32 count)
{
    const ItemDef* def = Def(item);
    if (!def) {
        LOG_WARN(Engine, "Inventory: there's no item '{}'", item);
        return 0;
    }
    const u32 added = Room(item, count);
    const u32 limit = StackLimit(*def);
    u32       left  = added;
    for (ItemStack& stack : _stacks) {
        if (left == 0) break;
        if (stack.Item != item || stack.Count >= limit) continue;
        const u32 take = std::min(left, limit - stack.Count);
        stack.Count += take;
        stack.Acquired = ++_clock;
        left -= take;
    }
    while (left > 0) {
        ItemStack stack{ .Item = def->Id, .Count = std::min(left, limit), .Acquired = ++_clock };
        if (_rules.IsGrid()) PlaceNew(stack);
        left -= stack.Count;
        _stacks.push_back(std::move(stack));
    }
    if (added) Changed(def->Id, added);
    return added;
}

u32 Inventory::Remove(const std::string_view item, const u32 count)
{
    u32 removed = 0;
    for (u32 i = static_cast<u32>(_stacks.size()); i-- > 0 && removed < count;)
        if (_stacks[i].Item == item) removed += RemoveAt(i, count - removed);
    return removed;
}

u32 Inventory::RemoveAt(const u32 index, const u32 count)
{
    if (index >= _stacks.size() || count == 0) return 0;
    ItemStack&        stack = _stacks[index];
    const u32         taken = std::min(count, stack.Count);
    const std::string item  = stack.Item;
    stack.Count -= taken;
    if (stack.Count == 0) _stacks.erase(_stacks.begin() + index);
    if (taken) Changed(item, -static_cast<i64>(taken));
    return taken;
}

u32 Inventory::Count(const std::string_view item) const
{
    u64 total = 0;
    for (const ItemStack& stack : _stacks)
        if (stack.Item == item) total += stack.Count;
    return static_cast<u32>(std::min<u64>(total, NoLimit));
}

void Inventory::Clear()
{
    const std::vector<ItemStack> was = std::move(_stacks);
    _stacks.clear();
    for (const ItemStack& stack : was) Changed(stack.Item, -static_cast<i64>(stack.Count));
}

void Inventory::Changed(const std::string& item, const i64 delta)
{
    if (OnChange) OnChange(item, delta);
}

// --- Arranging ----------------------------------------------------------------

bool Inventory::Move(const u32 from, const u32 to)
{
    if (from >= _stacks.size() || to >= _stacks.size()) return false;
    ItemStack stack = std::move(_stacks[from]);
    _stacks.erase(_stacks.begin() + from);
    _stacks.insert(_stacks.begin() + to, std::move(stack));
    return true;
}

bool Inventory::Split(const u32 index, const u32 count)
{
    if (_rules.OneEntryPerItem || index >= _stacks.size()) return false;
    if (count == 0 || count >= _stacks[index].Count) return false;
    if (!_rules.IsGrid() && _rules.Slots != 0 && _stacks.size() >= _rules.Slots) return false;
    ItemStack part = { .Item = _stacks[index].Item, .Count = count, .Acquired = _stacks[index].Acquired };
    if (_rules.IsGrid()) {
        std::vector<Area>          areas = BuildAreas();
        const ItemDef*             def   = Def(part.Item);
        const std::optional<Place> place = def ? FindPlace(areas, *def) : std::nullopt;
        if (!place) return false;
        part.Section = place->Section, part.X = place->X, part.Y = place->Y, part.Rotated = place->Rotated;
    }
    _stacks[index].Count -= count;
    _stacks.insert(_stacks.begin() + index + 1, std::move(part));
    return true;
}

u32 Inventory::Merge(const u32 from, const u32 to)
{
    if (from == to || from >= _stacks.size() || to >= _stacks.size() || _stacks[from].Item != _stacks[to].Item) return 0;
    const ItemDef* def  = Def(_stacks[to].Item);
    const u32      room = def ? StackLimit(*def) - std::min(StackLimit(*def), _stacks[to].Count) : 0;
    const u32      move = std::min(room, _stacks[from].Count);
    _stacks[to].Count += move;
    _stacks[from].Count -= move;
    if (_stacks[from].Count == 0) _stacks.erase(_stacks.begin() + from);
    return move;
}

void Inventory::SortList(const InventorySort by, const bool descending)
{
    const auto order = [&](const ItemStack& a, const ItemStack& b) {
        const ItemDef* x = Def(a.Item);
        const ItemDef* y = Def(b.Item);
        if (!x || !y) return 0;
        switch (by) {
            case InventorySort::Name: return Compare(NameOf(a.Item), NameOf(b.Item));
            case InventorySort::Type: return Compare(x->Type, y->Type);
            case InventorySort::Category: return Compare(x->Category, y->Category);
            case InventorySort::Value: return Compare(x->Value, y->Value);
            case InventorySort::Weight: return Compare(x->Weight * static_cast<f32>(a.Count), y->Weight * static_cast<f32>(b.Count));
            case InventorySort::Count: return Compare(a.Count, b.Count);
            case InventorySort::Recent: return Compare(a.Acquired, b.Acquired);
        }
        return 0;
    };
    const auto junk = [this](const ItemStack& s) { const ItemDef* d = Def(s.Item); return d && d->IsJunk(); };
    std::ranges::stable_sort(_stacks, [&](const ItemStack& a, const ItemStack& b) {
        if (junk(a) != junk(b)) return junk(b); // junk last, whichever way
        const int c = order(a, b) * (descending ? -1 : 1);
        return c != 0 ? c < 0 : NameOf(a.Item) < NameOf(b.Item);
    });
}

std::vector<u32> Inventory::Filter(const InventoryFilter& filter) const
{
    const std::string text = Lower(filter.Text);
    std::vector<u32>  kept;
    for (u32 i = 0; i < _stacks.size(); ++i) {
        const ItemDef* def = Def(_stacks[i].Item);
        if (!def) continue;
        if (!filter.Category.empty() && def->Category != filter.Category) continue;
        if (!filter.Type.empty() && def->Type != filter.Type) continue;
        if (!filter.Tag.empty() && !def->HasTag(filter.Tag)) continue;
        if (!text.empty() && Lower(Loc::Get(def->NameKey)).find(text) == std::string::npos) continue;
        kept.push_back(i);
    }
    return kept;
}

// --- Weight -------------------------------------------------------------------

f32 Inventory::GetWeight() const
{
    f64 total = 0.0;
    for (const ItemStack& stack : _stacks)
        if (const ItemDef* def = Def(stack.Item)) total += static_cast<f64>(def->Weight) * stack.Count;
    return static_cast<f32>(total);
}

bool Inventory::IsOverWeight() const { return _rules.MaxWeight > 0.0f && GetWeight() > _rules.MaxWeight + 1e-4f; }

// How many of `count` the weight limit lets in.
u32 Inventory::WeightRoom(const ItemDef& def, const u32 count) const
{
    if (_rules.MaxWeight <= 0.0f || def.Weight <= 0.0f || _rules.OverWeight == OverWeightRule::Allow) return count;
    const f32 free = _rules.MaxWeight - GetWeight();
    if (free <= 0.0f) return 0;
    const f64 fits = std::floor((static_cast<f64>(free) + 1e-4) / def.Weight);
    return static_cast<u32>(std::min<f64>(fits, count));
}

// --- Quick slots --------------------------------------------------------------

bool Inventory::SetQuickSlot(const u32 slot, std::string item)
{
    if (slot >= _quickSlots.size() || (!item.empty() && !Def(item))) return false;
    _quickSlots[slot] = std::move(item);
    return true;
}

// --- Saving -------------------------------------------------------------------

json RulesToJson(const InventoryRules& rules)
{
    return { { "kind", rules.Kind },
             { "slots", rules.Slots },
             { "one_entry_per_item", rules.OneEntryPerItem },
             { "unlimited_stacks", rules.UnlimitedStacks },
             { "max_weight", rules.MaxWeight },
             { "over_weight", rules.OverWeight == OverWeightRule::Allow ? "allow" : "refuse" },
             { "quick_slots", rules.QuickSlots },
             { "width", rules.Width },
             { "height", rules.Height },
             { "sections", SectionsToJson(rules.Sections) } };
}

InventoryRules RulesFromJson(const json& j)
{
    InventoryRules rules;
    const std::string kind = j.value("kind", rules.Kind);
    if (kind == "pockets") rules = InventoryRules::Pockets();
    else if (kind == "weight") rules = InventoryRules::Weight(0.0f);
    rules.Kind            = kind;
    rules.Slots           = j.value("slots", rules.Slots);
    rules.OneEntryPerItem = j.value("one_entry_per_item", rules.OneEntryPerItem);
    rules.UnlimitedStacks = j.value("unlimited_stacks", rules.UnlimitedStacks);
    rules.MaxWeight       = j.value("max_weight", rules.MaxWeight);
    rules.OverWeight      = j.value("over_weight", std::string("refuse")) == "allow" ? OverWeightRule::Allow : OverWeightRule::Refuse;
    rules.QuickSlots      = j.value("quick_slots", rules.QuickSlots);
    rules.Width           = j.value("width", rules.Width);
    rules.Height          = j.value("height", rules.Height);
    for (const json& s : j.value("sections", json::array()))
        rules.Sections.push_back({ .Name     = s.value("name", std::string{}),
                                   .Width    = s.value("width", 1u),
                                   .Height   = s.value("height", 1u),
                                   .Category = s.value("category", std::string{}) });
    return rules;
}

json Inventory::ToJson() const
{
    json stacks = json::array();
    for (const ItemStack& stack : _stacks) {
        json s = { { "item", stack.Item }, { "count", stack.Count }, { "acquired", stack.Acquired } };
        if (_rules.IsGrid()) s.update({ { "section", stack.Section }, { "x", stack.X }, { "y", stack.Y }, { "rotated", stack.Rotated } });
        stacks.push_back(std::move(s));
    }
    return { { "rules", RulesToJson(_rules) }, { "stacks", std::move(stacks) }, { "quick_slots", _quickSlots } };
}

// The saved rules are the bag's own (a bag the player upgraded stays upgraded).
void Inventory::FromJson(const json& j)
{
    if (const auto rules = j.find("rules"); rules != j.end()) _rules = RulesFromJson(*rules);
    _stacks.clear();
    _clock = 0;
    for (const json& s : j.value("stacks", json::array())) {
        ItemStack stack{ .Item     = s.value("item", std::string{}),
                         .Count    = s.value("count", 0u),
                         .Acquired = s.value("acquired", u64{ 0 }),
                         .Section  = s.value("section", std::string{}),
                         .X        = s.value("x", 0u),
                         .Y        = s.value("y", 0u),
                         .Rotated  = s.value("rotated", false) };
        if (!Def(stack.Item)) {
            LOG_WARN(Engine, "Inventory: a saved stack of '{}', which is no longer an item - dropped", stack.Item);
            continue;
        }
        if (stack.Count == 0) continue;
        _clock = std::max(_clock, stack.Acquired);
        _stacks.push_back(std::move(stack));
    }
    _quickSlots = j.value("quick_slots", std::vector<std::string>{});
    _quickSlots.resize(_rules.QuickSlots);
    if (_rules.IsGrid()) CheckPlaces();
}

// A saved grid whose stacks no longer fit where they were (an item's size
// changed in an update): those move to the first space they fit; with none
// left, they're dropped (logged).
void Inventory::CheckPlaces()
{
    std::vector<ItemStack> saved = std::move(_stacks), misplaced;
    _stacks.clear();
    for (ItemStack& stack : saved) { // in order: the first to claim a cell keeps it
        std::vector<Area> areas = BuildAreas();
        const ItemDef*    def   = Def(stack.Item);
        if (def && FitsAt(areas, *def, { stack.Section, stack.X, stack.Y, stack.Rotated })) _stacks.push_back(std::move(stack));
        else misplaced.push_back(std::move(stack));
    }
    for (ItemStack& stack : misplaced) {
        std::vector<Area> areas = BuildAreas();
        const ItemDef*    def   = Def(stack.Item);
        if (const auto place = def ? FindPlace(areas, *def) : std::nullopt) {
            stack.Section = place->Section, stack.X = place->X, stack.Y = place->Y, stack.Rotated = place->Rotated;
            _stacks.push_back(std::move(stack));
        }
        else LOG_WARN(Engine, "Inventory: a saved stack of '{}' no longer fits the grid - dropped", stack.Item);
    }
}
} // namespace Arcbit
