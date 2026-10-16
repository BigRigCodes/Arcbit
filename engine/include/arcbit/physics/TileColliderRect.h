#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/physics/AABB.h>
#include <arcbit/physics/DirectionArc.h>

#include <vector>

namespace Arcbit
{
// One greedy-meshed solid rectangle synthesized from the tilemap. Carries the
// collision metadata of the TileDef the rectangle was built from. Multiple
// adjacent solid tiles with matching collision class (same Layer +
// BlockedFrom + Solid flag) are merged into a single rect.
//
// Produced by TileMap::GetChunkColliders; consumed by PhysicsWorld::QueryTileColliders
// and the resolver.
struct TileColliderRect
{
    std::vector<DirectionArc> BlockedFrom = {}; // empty = block all directions

    AABB WorldAABB = {};
    u32  Layer     = 0;
};
} // namespace Arcbit
