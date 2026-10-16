#pragma once

#include <arcbit/core/Types.h>

namespace Arcbit::CollisionLayers
{
// Engine-reserved layer bits. Each constant is a single bit identifying what
// the collider IS; combine bits to build masks describing what to collide with.
//
// Bits 0-8 are reserved for the engine. Game-defined layers should use bits
// 9-31, registered through `project.arcbit` (Phase 38) or
// `PhysicsWorld::RegisterLayer` until then.
constexpr u32 Default    = 1u << 0; // unclassified — fallback
constexpr u32 Player     = 1u << 1;
constexpr u32 NPC        = 1u << 2; // friendly / neutral
constexpr u32 Enemy      = 1u << 3; // hostile
constexpr u32 Wall       = 1u << 4; // tile-synthesized world geometry
constexpr u32 Prop       = 1u << 5; // static world props (trees, rocks)
constexpr u32 Pickup     = 1u << 6; // items, coins, anything OnTriggerEnter collects
constexpr u32 Projectile = 1u << 7;
constexpr u32 Trigger    = 1u << 8; // generic trigger volumes (doors, regions)

// Convenience bundles.
constexpr u32 EngineLayers = Default | Player | NPC | Enemy | Wall | Prop |
                             Pickup | Projectile | Trigger;
constexpr u32 AllLayers    = ~0u;

constexpr u32 FirstUserBit = 9; // bit index, not value; user layers are 1u << N for N >= 9
} // namespace Arcbit::CollisionLayers
