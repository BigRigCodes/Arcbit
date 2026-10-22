#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/ecs/Entity.h>

#include <functional>
#include <string>
#include <string_view>

namespace Arcbit
{
class Scene;

// The stock prefab a dropped item becomes when it names none of its own
// ("drop_prefab"). A game's prefab of this id replaces it.
inline constexpr std::string_view PickupPrefab = "pickup";

// ---------------------------------------------------------------------------
// ItemPickup - an item lying in the world (docs/items.md#pickups-and-drops).
// A party entity (PartyLink) picks it up by walking onto it ("on": "touch",
// the default) or by interacting with it ("interact"). What fits goes into
// the bag; the rest stays on the ground. In a scene file:
//
//   "ItemPickup": { "item": "potion", "count": 2 }
//
// Make it Persistent so it stays picked up. Without a SpriteRenderer it's
// drawn as the item's icon, half a tile across.
// ---------------------------------------------------------------------------
struct ItemPickup
{
    std::string Item;
    u32         Count   = 1;
    bool        OnTouch = true;
    std::string Bag     = "party";
    f32         Reach   = 0.0f; // touch distance from its center; 0: half a tile

    // A touch pickup is taken on arrival. One that didn't all fit, or that
    // was dropped where the player stands, waits until they step away.
    bool _armed = true;
};

// LootDrop - an entity that leaves loot when it's destroyed (an enemy, a
// barrel): its table is rolled and scattered around where it was, as
// pickups. Not when its scene unloads, nor when a save destroys it again.
// In a scene file: "LootDrop": { "table": "slime" }.
struct LootDrop
{
    std::string Table;
};

// What a scene's pickups need from the game. Application gives every scene
// one; a scene without one (tests that don't set it) leaves pickups alone.
struct ItemHost
{
    // Put up to `count` of an item into a bag (or a counter currency);
    // returns how many went in.
    std::function<u32(std::string_view item, u32 count, std::string_view bag)> Take;

    // Give a pickup without a SpriteRenderer its look. Optional.
    std::function<void(Scene& scene, Entity pickup)> Dress;

    // `by` used a Container: its loot rolled the first time, then the
    // transfer screen or everything into the bag. Optional.
    std::function<void(Scene& scene, Entity container, Entity by)> Open;

    // Roll a loot table and scatter it at `at` as pickups (LootDrop). Optional.
    std::function<void(Scene& scene, std::string_view table, Vec2 at)> DropLoot;
};
} // namespace Arcbit
