#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/BodyKind.h>
#include <arcbit/physics/CollisionLayers.h>
#include <arcbit/physics/DirectionArc.h>

#include <vector>

namespace Arcbit
{
// Collider shape primitive. Box with Rotation == 0 hits the AABB fast path;
// non-zero Rotation switches the narrowphase to the OBB code path. Circle is
// rotation-invariant.
enum class ColliderShape : u8
{
    Box    = 0,
    Circle = 1,
};

// Physics shape attached to an entity.
//
// Position is `Transform2D.Position + Offset`. Neither Offset nor the shape
// follows `Transform2D.Rotation` — `Rotation` here is the collider's own
// (radians, clockwise on screen like the transform). Props that should turn
// visually and physically set both. Box uses HalfExtents
// (half-width, half-height); Circle uses Radius. Shapes that won't be
// authored at the same time (e.g. a Circle's HalfExtents) are simply unused.
//
// Layer is a single bit identifying what this collider IS (see
// CollisionLayers.h). Mask is the bitfield of layers this collider WILL
// COLLIDE WITH; the resolver pairs two colliders only when both
// (a.Layer & b.Mask) and (b.Layer & a.Mask) are non-zero.
//
// BlockedFrom carries the directional arcs that gate which approach directions
// are treated as blocking contacts. **An empty list means "block from all
// directions"** (the common case — and avoids per-collider heap allocation for
// the default). Add explicit arcs only when the prop should pass through some
// approach directions (trees, ledges, bushes — see DirectionArc.h presets).
// Arcs are evaluated in the collider's local frame, so they turn with Rotation.
struct Collider2D
{
    ColliderShape Shape       = ColliderShape::Box;
    Vec2          Offset      = { 0.0f, 0.0f };
    f32           Rotation    = 0.0f;            // radians; 0 = AABB fast path
    Vec2          HalfExtents = { 16.0f, 16.0f }; // Box only
    f32           Radius      = 16.0f;            // Circle only

    BodyKind                  Kind        = BodyKind::Kinematic;
    u32                       Layer       = CollisionLayers::Default;
    u32                       Mask        = CollisionLayers::AllLayers;
    bool                      IsTrigger   = false;
    std::vector<DirectionArc> BlockedFrom = {}; // empty = block from all directions
};
} // namespace Arcbit
