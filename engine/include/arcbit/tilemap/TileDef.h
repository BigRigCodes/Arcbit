#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/CollisionLayers.h>
#include <arcbit/physics/DirectionArc.h>

#include <vector>

namespace Arcbit
{
// Three draw layers for every tile position.
enum class TileLayer : u8
{
    Ground  = 0, // always behind entities (fixed layer -1000)
    Objects = 1, // Y-sorted with entities
    Overlay = 2, // always in front (fixed layer 1000000)
};

// One frame in a flip-book tile animation.
struct TileAnimFrame
{
    u32 TileX      = 0;
    u32 TileY      = 0;
    u32 DurationMs = 150;
};

// Per-tile behaviour overrides.  Register with TileMap::RegisterTile().
// Tiles without a registered def render at their default atlas UV.
struct TileDef
{
    bool Solid       = false; // blocks pathfinding / collision queries
    bool BlocksLight = false; // casts shadows — independent of Solid
    bool Flat        = false; // always sorts below entities (sticks, leaves, decals)

    // Flip-book animation — cycles through listed frames in order.
    // Uses a per-position phase offset so adjacent tiles don't march in lockstep.
    // Mutually exclusive with UVScroll.
    std::vector<TileAnimFrame> Animation;

    // Continuous UV offset applied each frame: UV += scroll * elapsed (seconds).
    // Sampler repeat mode wraps the offset so the texture tiles seamlessly.
    // Mutually exclusive with Animation.
    Vec2 UVScroll = {};

    // ---- Collision metadata (consumed when Solid == true) -----------------
    // Carried into the synthesized TileColliderRect during greedy meshing.
    // Adjacent solid tiles only merge into the same rect when these fields
    // match (alongside Solid).
    //
    // BlockedFrom: directional arcs that gate which approach directions are
    // treated as blocking. Empty list = "block from all directions" (the
    // common case; matches the Collider2D convention). Phase 22D wires the
    // arc check; until then any non-empty list is carried but inert.
    //
    // Layer: collision layer this tile contributes to (see CollisionLayers.h).
    // Defaults to Wall, which is what tile-synthesized geometry usually is.
    std::vector<DirectionArc> BlockedFrom = {};

    u32 Layer = CollisionLayers::Wall;
};
} // namespace Arcbit
