#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/AABB.h>
#include <arcbit/physics/Collider2D.h>

namespace Arcbit::Sweep
{
// Time-of-impact result for one swept-pair test.
//
// ToI in [0, 1] expresses the fraction of the supplied delta the mover may
// safely traverse before contact. Hit==false means the candidates do not
// intersect within the delta; ToI is then 1.0 by convention so callers can
// take min(toi) without a separate "hit?" check.
//
// Normal points away from the obstacle (toward the mover's pre-contact side),
// suitable for projecting the remaining delta onto the wall tangent for slide.
struct Result
{
    f32  ToI    = 1.0f;
    Vec2 Normal = {};
    bool Hit    = false;
};

// Closed-form swept test for two axis-aligned boxes when one moves.
// `moving` is the mover's current world AABB (assumed not already overlapping
// `stationary` past tangency); `delta` is the displacement to test in world
// pixels. Boxes already touching at t==0 (within float noise) report a Hit
// with ToI==0 and the touching face's normal only when `delta` pushes into
// that face; moving away from or along it is not a hit, so a mover resting
// against a box can leave or slide.
[[nodiscard]] Result MovingAABBvsAABB(const AABB& moving, Vec2 delta,
                                      const AABB& stationary) noexcept;

// Sub-step fallback for shape combinations involving circles or rotated
// boxes. Splits the delta into N steps, returns the earliest step at which
// the boolean narrowphase fires. The reported ToI is the fraction *just
// before* the first overlap step — a small gap that's tolerable for character
// movement at the speeds we expect. The normal is the penetration axis at the
// overlapping step (Narrowphase::ComputeContact), i.e. the face that was
// crossed. `substeps` should be >= 4; SweepAgainst picks 8–64 based on the
// move length vs. the thinnest shape feature.
[[nodiscard]] Result Substep(const Collider2D& mover, Vec2 origin, Vec2 delta,
                             const Collider2D& other, Vec2 otherPos,
                             u32               substeps) noexcept;

// Dispatcher: slab method when both shapes are axis-aligned boxes, sub-step
// (adaptive count) otherwise. Single entry point for the resolver — algorithm
// choice stays internal so we can swap implementations later without
// touching call sites.
[[nodiscard]] Result SweepAgainst(const Collider2D& mover, Vec2 origin, Vec2 delta,
                                  const Collider2D& other, Vec2 otherPos) noexcept;

// Debug-only correctness check. Asserts on failure.
void SelfTest();
} // namespace Arcbit::Sweep
