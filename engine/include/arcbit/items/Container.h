#pragma once

#include <arcbit/items/Inventory.h>

#include <string>

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
//   "Container": { "items": { "potion": 2, "cellar_key": 1 },
//                  "rules": { "kind": "list", "slots": 8 }, "open": "take_all" }
//
// "items" sets the contents (in that order); "rules" the kind of bag
// (InventoryRules' JSON; default a plain list); "open" overrides the
// project's Defaults.ContainerOpen; "loot" names a loot table it rolls the
// first time it's opened.
// ---------------------------------------------------------------------------
struct Container
{
    Inventory   Items;
    std::string Open; // "transfer" / "take_all"; empty: the project's
    std::string Loot;
};
} // namespace Arcbit
