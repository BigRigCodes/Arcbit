#pragma once

#include <arcbit/items/Inventory.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Arcbit
{
// How opening a container goes: the transfer screen (it beside the bag), or
// everything into the bag at once with a toast.
enum class ContainerOpening : u8 { Transfer, TakeAll };

// ---------------------------------------------------------------------------
// Container - a scene entity that holds items: a chest, a crate, later a
// shop (docs/items.md#containers). Saved with the scene's state like any
// component (Persistent). In a scene file:
//
//   "Container": { "items": { "potion": 2, "cellar_key": 1 }, "currency": { "gold": 25 },
//                  "rules": { "kind": "list", "slots": 8 }, "open": "take_all" }
//
// "items" sets the contents (in that order); "currency" amounts of the
// game's currencies; "rules" the kind of bag (InventoryRules' JSON; default
// a plain list); "open" overrides the project's Defaults.ContainerOpen;
// "loot" names a loot table (docs/items.md#loot-tables) rolled into it the
// first time it's opened - then cleared, so it's rolled once.
// ---------------------------------------------------------------------------
struct Container
{
    Inventory                               Items;
    std::map<std::string, i64, std::less<>> Currency;
    std::string                             Open; // "transfer" / "take_all"; empty: the project's
    std::string                             Loot;
};

// "transfer", "take_all"; and back (nullopt for anything else).
[[nodiscard]] std::string_view                ContainerOpeningName(ContainerOpening opening);
[[nodiscard]] std::optional<ContainerOpening> ParseContainerOpening(std::string_view name);
} // namespace Arcbit
