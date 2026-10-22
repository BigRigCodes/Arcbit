#pragma once

#include <arcbit/core/Types.h>

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class ItemLibrary;
struct ItemDef;

// ---------------------------------------------------------------------------
// Inventory (docs/items.md#inventories): stacks of items, and the rules of
// the kind of bag it is. A game picks a kind per inventory:
//
//   pockets  Pokemon: one entry per item, up to its stack size; the screen
//            shows a list per category.
//   weight   Skyrim: one entry per item, any count; a carry weight limit;
//            category tabs filter the list.
//   grid     Minecraft: a W x H grid of cells; an item takes the cells of
//            its size (rotated to fit, unless it can't turn), one stack each;
//            optionally, sections beside it (a quiver).
//
// A hotbar is optional and open to every kind: QuickSlots, item references
// bound to keys (none by default). A grid may instead give a section of real
// slots that role, Minecraft-style - also only if the game asks for one.
//   survival a grid with a carry weight limit too.
//
// The rules mix freely; the presets are starting points. Every kind sorts
// and filters. Items come from an ItemLibrary, which the inventory reads for
// stack sizes, weights, names and categories.
// ---------------------------------------------------------------------------

// One entry: an item and how many - and, in a grid, where it lies.
struct ItemStack
{
    std::string Item;
    u32         Count    = 0;
    u64         Acquired = 0; // when it last grew - the "recent" sort

    std::string Section;         // grid: "" the main grid, else a section's name
    u32         X = 0, Y = 0;    // grid: its top-left cell
    bool        Rotated = false; // grid: turned a quarter (its width and height swap)
};

// A small grid beside a grid inventory's main one, only when a game adds
// one - a quiver, or a hotbar of real slots.
struct InventorySection
{
    std::string Name;
    u32         Width  = 1;
    u32         Height = 1;
    std::string Category; // only items of this category; empty: any
};

enum class OverWeightRule : u8
{
    Refuse, // what would go over the limit isn't taken (the default)
    Allow,  // it is, and the bag is over (IsOverWeight - encumbered)
};

struct InventoryRules
{
    std::string Kind = "list"; // the preset it came from, for the screen: "pockets", "weight", ...
    u32         Slots = 0;     // the most entries; 0 = no limit

    // One entry per item: more of it joins that entry (Pokemon, Skyrim).
    // Otherwise a full stack starts another.
    bool OneEntryPerItem = false;

    // Counts ignore items' stack sizes (Skyrim's 300 arrows in one entry).
    bool UnlimitedStacks = false;

    f32            MaxWeight  = 0.0f; // carry weight; 0 = no limit
    OverWeightRule OverWeight = OverWeightRule::Refuse;

    u32 QuickSlots = 0; // the hotbar any kind can have: item references bound to keys; 0 = none

    // A grid: Width x Height cells (both above 0); Slots is unused.
    u32                           Width  = 0;
    u32                           Height = 0;
    std::vector<InventorySection> Sections;

    [[nodiscard]] bool IsGrid() const { return Width > 0 && Height > 0; }

    // Pokemon inspired: a list per category, one entry per item, up to its stack size.
    [[nodiscard]] static InventoryRules Pockets();
    // Skyrim inspired: one entry per item, any count, a carry weight limit.
    [[nodiscard]] static InventoryRules Weight(f32 maxWeight);
    // Minecraft inspired: a grid of cells, nothing more (sections and quick
    // slots are extras a game adds).
    [[nodiscard]] static InventoryRules Grid(u32 width, u32 height);
    // Valheim inspired: A grid with a carry weight limit too.
    [[nodiscard]] static InventoryRules Survival(u32 width, u32 height, f32 maxWeight);
};

// What a sort orders by. Junk always sorts last; ties go by name.
enum class InventorySort : u8 { Name, Type, Category, Value, Weight, Count, Recent };

// What a filter keeps: every field given must match (empty: any).
struct InventoryFilter
{
    std::string Category;
    std::string Type;
    std::string Tag;
    std::string Text; // in the item's name, in the player's language, any case
};

class Inventory
{
public:
    Inventory() = default;
    Inventory(const ItemLibrary& items, InventoryRules rules);

    // Told whenever an item's count changes, with the change (+ / -).
    std::function<void(const std::string& item, i64 delta)> OnChange;

    [[nodiscard]] const InventoryRules&         GetRules() const { return _rules; }
    [[nodiscard]] const std::vector<ItemStack>& GetStacks() const { return _stacks; }
    [[nodiscard]] const ItemLibrary*            GetItems() const { return _items; }

    // --- Adding and removing ------------------------------------------------

    // Adds as many as fit - into its stacks first, then new entries - and
    // returns how many did (fewer when slots, stack sizes or weight run out;
    // 0 for an unknown item, logged).
    u32 Add(std::string_view item, u32 count = 1);

    // How many of `count` would fit, without adding them.
    [[nodiscard]] u32 Room(std::string_view item, u32 count) const;

    // Removes up to `count` (newest stacks first); returns how many it did.
    u32 Remove(std::string_view item, u32 count = 1);
    u32 RemoveAt(u32 index, u32 count); // from one entry

    [[nodiscard]] u32  Count(std::string_view item) const;
    [[nodiscard]] bool Has(std::string_view item, const u32 count = 1) const { return Count(item) >= count; }
    [[nodiscard]] bool IsEmpty() const { return _stacks.empty(); }
    void               Clear();

    // --- Arranging ----------------------------------------------------------

    bool Move(u32 from, u32 to);            // an entry to another place in the list
    bool Split(u32 index, u32 count);       // `count` of an entry into a new one (a grid: the first space it fits)
    u32  Merge(u32 from, u32 to);           // as much of `from` into `to` (the same item) as fits; returns how many

    // Lists reorder. A grid repacks its main grid in that order (sections
    // keep the player's arrangement), trying largest-first if the order
    // doesn't pack; false, unchanged, if neither does.
    bool Sort(InventorySort by, bool descending = false);

    // --- Grids --------------------------------------------------------------

    // Put an entry at a cell of the main grid ("") or a section, turned or
    // not; false if it doesn't fit there (it, or the section, refuses).
    bool MoveTo(u32 index, std::string_view section, u32 x, u32 y, bool rotated);
    bool Rotate(u32 index);             // turn it where it lies, if it fits turned
    bool Swap(u32 a, u32 b);            // trade places, if each fits in the other's
    [[nodiscard]] std::optional<u32> StackAt(std::string_view section, u32 x, u32 y) const; // what covers a cell
    void Footprint(u32 index, u32& width, u32& height) const; // the cells an entry takes, as it lies

    // --- Changing bags ------------------------------------------------------

    // What wouldn't fit in a bag with other rules (a smaller grid, a weight
    // limit where there was none) - empty when everything does. Junk is
    // what's left out first: it's what the player is offered to discard.
    [[nodiscard]] std::vector<ItemStack> ConvertOverflow(const InventoryRules& to) const;

    // Become a bag with other rules, everything kept (in a grid, where it
    // was when it still fits there). False, unchanged, while anything would
    // be left out - discard ConvertOverflow first.
    bool Convert(const InventoryRules& to);

    // The indices of the entries a filter keeps, in order.
    [[nodiscard]] std::vector<u32> Filter(const InventoryFilter& filter) const;

    // --- Weight -------------------------------------------------------------

    [[nodiscard]] f32  GetWeight() const;
    [[nodiscard]] bool IsOverWeight() const;

    // --- Quick slots --------------------------------------------------------

    // References to items (not the items): a slot keeps its item while the
    // bag has none of it, and shows 0. Empty: unbound.
    [[nodiscard]] const std::vector<std::string>& GetQuickSlots() const { return _quickSlots; }
    bool SetQuickSlot(u32 slot, std::string item);

    // --- Saving -------------------------------------------------------------

    [[nodiscard]] nlohmann::json ToJson() const;
    // Stacks of items the library no longer has are dropped (logged).
    void FromJson(const nlohmann::json& json);

private:
    // A grid or section's cells, each the index of the stack covering it, or -1.
    struct Area
    {
        const InventorySection* Section = nullptr; // null: the main grid
        u32                     Width = 0, Height = 0;
        std::vector<i32>        Cells;
    };
    struct Place
    {
        std::string Section;
        u32         X = 0, Y = 0;
        bool        Rotated = false;
    };

    [[nodiscard]] std::vector<Area>    BuildAreas(i32 skip = -1) const;
    [[nodiscard]] Area*                FindArea(std::vector<Area>& areas, std::string_view section) const;
    [[nodiscard]] bool                 Accepts(const Area& area, const ItemDef& def) const;
    [[nodiscard]] std::optional<Place> FindPlace(std::vector<Area>& areas, const ItemDef& def, bool mainOnly = false) const;
    [[nodiscard]] u32                  NewStackRoom(const ItemDef& def, u32 count) const;
    [[nodiscard]] bool                 FitsAt(std::vector<Area>& areas, const ItemDef& def, const Place& place) const;
    [[nodiscard]] bool                 Repack(const std::vector<ItemStack>& order);
    void                               PlaceNew(ItemStack& stack);
    void                               CheckPlaces();
    [[nodiscard]] Inventory            ConvertedTo(const InventoryRules& to, std::vector<ItemStack>& overflow) const;
    [[nodiscard]] Inventory            Fill(const InventoryRules& to, const std::vector<const ItemStack*>& order, bool keepPlaces,
                                            std::vector<ItemStack>& overflow) const;
    u32                                Take(const ItemStack& stack, bool keepPlace);
    void                               SortList(InventorySort by, bool descending);
    [[nodiscard]] const ItemDef* Def(std::string_view item) const;
    [[nodiscard]] u32            StackLimit(const ItemDef& def) const;
    [[nodiscard]] u32            WeightRoom(const ItemDef& def, u32 count) const;
    [[nodiscard]] std::string    NameOf(const std::string& item) const;
    void                         Changed(const std::string& item, i64 delta);

    const ItemLibrary*       _items = nullptr;
    InventoryRules           _rules;
    std::vector<ItemStack>   _stacks;
    std::vector<std::string> _quickSlots;
    u64                      _clock = 0; // Acquired stamps
};

[[nodiscard]] nlohmann::json RulesToJson(const InventoryRules& rules);
[[nodiscard]] InventoryRules RulesFromJson(const nlohmann::json& json);
} // namespace Arcbit
