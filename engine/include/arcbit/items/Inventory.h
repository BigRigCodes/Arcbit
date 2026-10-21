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
//   grid / survival (slots, and slots plus weight) - see InventoryRules.
//
// The rules mix freely; the presets are starting points. Every kind sorts
// and filters. Items come from an ItemLibrary, which the inventory reads for
// stack sizes, weights, names and categories.
// ---------------------------------------------------------------------------

// One entry: an item and how many.
struct ItemStack
{
    std::string Item;
    u32         Count    = 0;
    u64         Acquired = 0; // when it last grew - the "recent" sort
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

    u32 QuickSlots = 0; // item references bound to keys (any kind)

    // Pokemon: a list per category, one entry per item, up to its stack size.
    [[nodiscard]] static InventoryRules Pockets();
    // Skyrim: one entry per item, any count, a carry weight limit.
    [[nodiscard]] static InventoryRules Weight(f32 maxWeight);
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
    bool Split(u32 index, u32 count);       // `count` of an entry into a new one after it
    u32  Merge(u32 from, u32 to);           // as much of `from` into `to` (the same item) as fits; returns how many
    void Sort(InventorySort by, bool descending = false);

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
