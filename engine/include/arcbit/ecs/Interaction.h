#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/ecs/Entity.h>

#include <string>

namespace Arcbit
{
class World;

// ---------------------------------------------------------------------------
// Interaction - pressing a button to use what's in front of you: talk to an
// NPC, open a chest, read a sign (docs/scripting.md).
//
// The player gets an Interactor; things it can use get an Interactable. When
// the Interactor's action is pressed, Scene picks one target
// (FindInteractTarget), starts its Dialog if it has one, calls its script's
// OnInteract(interactor) and publishes engine.entity.interacted - so C++ can
// react too.
// ---------------------------------------------------------------------------

// What does the interacting (the player).
struct Interactor
{
    std::string Action = "Interact"; // input action that interacts
    f32         Reach  = 0.0f;       // pixels, for free movers; 0 = 1.25 tiles
};

// Something an Interactor can use. A Disabled one is skipped.
struct Interactable
{
    std::string Prompt;     // what using it does ("Open", "Talk") - for a prompt UI
    std::string Dialog;     // a dialog id to start when used - talking needs no script
    std::string DialogNode; // where in it to start; empty: its start
};

// What `interactor` would use right now, or Entity::Invalid():
//   - a tile mover (SmoothTileMovement / SnapTileMovement): the Interactable
//     on the tile it faces, and nothing else;
//   - a free mover: the nearest Interactable in reach within 60 degrees of
//     where it faces, else the nearest in reach.
// Facing comes from MoveIntent (down if it has none).
[[nodiscard]] Entity FindInteractTarget(World& world, Entity interactor, f32 tileSize);
} // namespace Arcbit
