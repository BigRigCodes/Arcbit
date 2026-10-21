#pragma once

#include <arcbit/core/Types.h>

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// An item's definition, read from an .arcitems file (docs/items.md):
//
//   { "potion": { "name": "Potion", "description": "Restores 50 HP.",
//                 "icon": "assets/items/icons.json#potion", "type": "consumable",
//                 "stack": 99, "weight": 0.5, "value": 25,
//                 "use": { "stats": { "HP": 50 } } },
//     "iron_sword": { "name": "Iron Sword", "type": "equipment", "slot": "weapon",
//                     "size": [1, 2], "weight": 6, "stats": { "ATK": 5, "SPD%": -5 } } }
//
// The English name and description live in the file; their Loc keys come
// from the id (items.<id>.name, items.<id>.description), so translations go
// in the usual locale files.
// ---------------------------------------------------------------------------

namespace ItemTypes
{
    constexpr std::string_view Consumable = "consumable";
    constexpr std::string_view Equipment  = "equipment";
    constexpr std::string_view Key        = "key"; // can't be dropped or sold
    constexpr std::string_view Material   = "material"; // the default
    constexpr std::string_view Currency   = "currency";
} // namespace ItemTypes

namespace ItemTags
{
    constexpr std::string_view Junk = "junk"; // sorted last; offered first when the player must discard
} // namespace ItemTags

struct ItemDef
{
    std::string Id;
    std::string Name;        // English
    std::string Description; // English
    std::string NameKey;     // items.<id>.name
    std::string DescriptionKey;

    // A texture path, or a sprite sheet and one of its frames:
    // "assets/items/icons.json#potion".
    std::string Icon;

    std::string              Type = std::string(ItemTypes::Material); // a game may use its own
    std::string              Category; // pockets and filters; default: the type
    std::vector<std::string> Tags;

    u32  Stack     = 1;    // the most in one stack (the file's, else the type's default)
    f32  Weight    = 0.0f; // carry weight of one
    i64  Value     = 0;    // a price, in the project's main currency
    u32  Width     = 1;    // cells it takes in a grid inventory ("size")
    u32  Height    = 1;
    bool Rotatable = true; // a grid may turn it to fit

    // Equipment: the kind of slot it fits ("weapon", "accessory"), and what
    // it adds to the wearer's stats - flat ("ATK": 5) or percent ("SPD%": -5).
    std::string                Slot;
    std::map<std::string, f32> Stats;

    nlohmann::json Use        = nlohmann::json::object(); // what using it does (docs/items.md)
    std::string    DropPrefab; // dropped into the world as this prefab; empty: the stock pickup
    nlohmann::json Properties = nlohmann::json::object(); // a game's own fields

    [[nodiscard]] bool HasTag(std::string_view tag) const;
    [[nodiscard]] bool IsJunk() const { return HasTag(ItemTags::Junk); }
};

// Something wrong with an item. Errors are mistakes it can't load past (no
// name, a loop of "extends"); warnings are likely ones (a misspelled field,
// a missing icon).
struct ItemIssue
{
    std::string Item; // its id; empty for the file as a whole
    std::string File;
    bool        Error = true;
    std::string Message;
};
} // namespace Arcbit
