#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/AABB.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/physics/OBB.h>

namespace Arcbit::Narrowphase
{
// Pure-math overlap tests. Pair lookups come from the broadphase
// (PhysicsWorld::QueryAABB → SpatialHash); these functions decide whether
// a candidate pair *actually* touches.
//
// The overlap tests are boolean. ComputeContact adds the penetration normal +
// depth for the substep sweep (22E); the slab sweep computes its own normal.
//
// Coordinates are world-space. The dispatcher takes a `Vec2 worldPosition`
// for each collider; the collider's local Offset is added internally.

[[nodiscard]] bool AABBOverlap(const AABB& a, const AABB& b) noexcept;

// True iff the AABB and the circle defined by (center, radius) overlap.
[[nodiscard]] bool AABBCircleOverlap(const AABB& box, Vec2 center, f32 radius) noexcept;

// True iff the two discs overlap.
[[nodiscard]] bool CircleOverlap(Vec2 centerA, f32 radiusA,
                                 Vec2 centerB, f32 radiusB) noexcept;

// Separating-axis test for two oriented boxes. Tests the four face axes;
// touching (zero overlap on the tightest axis) counts as overlap, matching the
// `<=` convention of the other tests.
[[nodiscard]] bool OBBOverlap(const OBB& a, const OBB& b) noexcept;

// True iff the oriented box and the circle overlap. The circle center is
// moved into the box's local frame, where the test is the AABB one.
[[nodiscard]] bool OBBCircleOverlap(const OBB& box, Vec2 center, f32 radius) noexcept;

// Dispatcher: picks the right specialized test based on shape combination.
//
// Box-vs-Box and Box-vs-Circle use the AABB fast path when every box involved
// has `Rotation == 0`; any rotated box routes the pair to the exact OBB tests.
[[nodiscard]] bool Overlap(const Collider2D& a, Vec2 worldPosA,
                           const Collider2D& b, Vec2 worldPosB) noexcept;

// Penetration contact between two shapes. Normal is unit length and points
// from `b` toward `a` — the direction that pushes `a` out of `b`, matching
// Sweep::Result.Normal when `a` is the mover. Depth is the overlap along it.
// Hit == false means no overlap; Normal and Depth are then unset.
struct Contact
{
    Vec2 Normal = {};
    f32  Depth  = 0.0f;
    bool Hit    = false;
};

// Minimum-penetration contact for any shape pair:
//   - Box vs Box:       SAT, the face axis with the least overlap.
//   - Box vs Circle:    closest point on the box; a circle center inside the
//                       box resolves along the box's least-penetration axis.
//   - Circle vs Circle: along the center line.
// Always uses the OBB math (an unrotated box is an OBB too) — it runs only on
// the substep path, never in the axis-aligned fast path.
[[nodiscard]] Contact ComputeContact(const Collider2D& a, Vec2 worldPosA,
                                     const Collider2D& b, Vec2 worldPosB) noexcept;
} // namespace Arcbit::Narrowphase
