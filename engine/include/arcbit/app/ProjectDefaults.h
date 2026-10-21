#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/items/Container.h>
#include <arcbit/items/Currencies.h>
#include <arcbit/items/Inventory.h>

#include <map>
#include <string>
#include <vector>

namespace Arcbit
{
// How a one-time notice is shown: a card that waits for OK, a toast that
// fades by itself, or not at all (the engine event still goes out).
enum class NoticeStyle : u8 { Card, Toast, None };

// One of a party member's equipment slots: its name, and the kind of item
// it takes (an item's "slot") - two accessory slots both take "accessory".
struct EquipmentSlot
{
    std::string Name;
    std::string Kind;
};

// ---------------------------------------------------------------------------
// ProjectDefaults - the engine behavior a game can change in one place
// (ApplicationConfig::Defaults), until project.arcbit (Phase 38) reads the
// same fields from a file. Most can be changed per instance too; each field
// says where.
// ---------------------------------------------------------------------------
struct ProjectDefaults
{
    // Tap to turn: how long (seconds) a standing tile mover holds a new
    // direction before it walks - a quicker tap only turns it. 0 = off.
    // Per mover: SmoothTileMovement / SnapTileMovement::TurnDelay
    // ("turn_delay").
    f32 TileTurnDelay = 0.1f;

    // Whether Esc / gamepad East on a dialog's line ends the conversation
    // (on a choice it picks the option marked "cancel", either way).
    // Per dialog: "cancelable" in the .arcdialog file.
    bool DialogCancelable = true;

    // Sprites draw in order of their bottom edge's Y, sorting with each other
    // and Objects-layer tiles: a character walks behind a tree and in front
    // of it. Per sprite: SpriteRenderer::YSort ("y_sort"; false for flat
    // things, which keep their Layer).
    bool YSortSprites = true;

    // After an update gave the player new (or reset) actions, the notice of
    // where they landed (Settings::LoadInputBindings), once the boot is
    // done: actions that didn't get their default keys, and resets.
    // engine.input.bindings_updated is published with the same lines.
    NoticeStyle BindingNotice = NoticeStyle::Card;

    // Also list new actions that simply got their default keys.
    bool BindingNoticeAllNew = false;

    // How many of an item one stack holds, by its type, when the item
    // doesn't say ("stack" in its .arcitems entry); ItemStackOther for a
    // type of the game's own.
    std::map<std::string, u32, std::less<>> ItemStack = {
        { "consumable", 99 }, { "equipment", 1 }, { "key", 1 }, { "material", 999 }, { "currency", 999999 },
    };
    u32 ItemStackOther = 99;

    // The party's shared bag: its kind (pockets, weight, grid, survival - see
    // InventoryRules) as a new game starts.
    InventoryRules PartyBag = InventoryRules::Pockets();

    // The game's currencies: SaveData counters, or (Item) stacks in a bag.
    std::vector<CurrencyDef> Currencies = { { .Id = "gold" } };

    // Opening a Container: the transfer screen, or take everything at once.
    // Per container: "open".
    ContainerOpening ContainerOpen = ContainerOpening::Transfer;

    // A party member's equipment slots; an item's "slot" names a Kind.
    std::vector<EquipmentSlot> EquipmentSlots = {
        { "weapon", "weapon" }, { "offhand", "offhand" },       { "head", "head" },
        { "body", "body" },     { "accessory1", "accessory" }, { "accessory2", "accessory" },
    };
};
} // namespace Arcbit
