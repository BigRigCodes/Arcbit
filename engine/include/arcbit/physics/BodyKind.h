#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit
{
// Distinguishes how a Collider2D participates in physics resolution.
//
// Static    — never moves; lives in the broadphase forever. Other bodies
//             resolve against it. Use for trees, rocks, walls, terrain.
// Kinematic — position is owned by a movement system or script; the resolver
//             clamps its motion against other bodies. Use for the player,
//             NPCs, pushable boxes, AI agents.
//
// Triggers aren't a kind: set Collider2D::IsTrigger on either kind (a static
// door zone or a kinematic sword hitbox) to get overlap events instead of
// blocking.
enum class BodyKind : u8
{
    Static    = 0,
    Kinematic = 1,
};
} // namespace Arcbit
