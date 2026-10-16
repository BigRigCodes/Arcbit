#include <arcbit/physics/Sweep.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/PhysicsWorld.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Arcbit::Sweep
{
namespace
{
    constexpr f32 SweepEpsilon = 1e-6f;

    // Shapes closer than this (gap or overlap, in px) count as touching
    // rather than overlapping. Covers the float noise left by
    // `position + delta * ToI` after a resolved hit.
    constexpr f32 TouchTolerance = 1e-3f;

    // Substep bounds for the non-slab path. 8 covers ordinary per-tick
    // movement; 64 caps the cost of a long move (a full tile step) past a
    // thin obstacle.
    constexpr u32 MinSubsteps = 8;
    constexpr u32 MaxSubsteps = 64;

    // Thinnest dimension of a shape — the feature a substep must not skip over.
    f32 SmallestFeature(const Collider2D& c) noexcept
    {
        return c.Shape == ColliderShape::Circle
               ? c.Radius * 2.0f
               : std::min(c.HalfExtents.X, c.HalfExtents.Y) * 2.0f;
    }

    // Choose a substep count so each step advances at most half the thinner
    // shape's smallest feature. Without this, a 64 px tile step in 8 substeps
    // moves 8 px at a time and can tunnel straight through a 4 px plank.
    u32 SubstepCount(const Collider2D& mover, const Vec2 delta, const Collider2D& other) noexcept
    {
        const f32 feature = std::min(SmallestFeature(mover), SmallestFeature(other));
        if (feature <= SweepEpsilon) return MaxSubsteps;

        const f32 length = Length(delta);
        const f32 ideal  = std::ceil(length / (feature * 0.5f));
        return std::clamp(static_cast<u32>(ideal), MinSubsteps, MaxSubsteps);
    }

    // Per-axis slab-method timing. Returns false when the mover slides past the
    // slab without ever entering it (no contact possible). On success, fills
    // tEnter/tExit; either may be infinite when v == 0 and the axis ranges
    // already overlap.
    bool AxisSlab(const f32 mMin, const f32 mMax,
                  const f32 sMin, const f32 sMax,
                  const f32 v,
                  f32&      tEnter, f32& tExit) noexcept
    {
        if (std::abs(v) < SweepEpsilon) {
            // Stationary on this axis — overlap decides "always open" vs "never
            // enterable". Ranges that only share an edge don't overlap: a box
            // sliding flush along one box would otherwise snag on the next box's
            // corner at the seam between them.
            if (mMax <= sMin + TouchTolerance || mMin >= sMax - TouchTolerance) return false;
            tEnter = -std::numeric_limits<f32>::infinity();
            tExit  = std::numeric_limits<f32>::infinity();
            return true;
        }
        if (v > 0.0f) {
            tEnter = (sMin - mMax) / v;
            tExit  = (sMax - mMin) / v;
        }
        else {
            tEnter = (sMax - mMin) / v;
            tExit  = (sMin - mMax) / v;
        }
        return true;
    }

    // True when the boxes overlap by more than float noise on both axes —
    // the mover is genuinely inside the obstacle, not resting against it.
    bool StartsInside(const AABB& m, const AABB& s) noexcept
    {
        const f32 overlapX = std::min(m.Max.X - s.Min.X, s.Max.X - m.Min.X);
        const f32 overlapY = std::min(m.Max.Y - s.Min.Y, s.Max.Y - m.Min.Y);
        return overlapX > TouchTolerance && overlapY > TouchTolerance;
    }

    // Contact rule for boxes already touching at t = 0 — the normal state of
    // a mover the resolver stopped against a face last tick. Only motion
    // *into* the touching face is a hit; moving away or sliding along it is
    // free. Without this, the slab math reports ToI = 0 for every direction
    // and a box mover touching a box can never leave it.
    //
    // Returns nullopt when the boxes aren't in touching contact (separated, or
    // genuinely overlapping — the general slab path handles both).
    std::optional<Result> TouchingContact(const AABB& m, const Vec2 delta, const AABB& s) noexcept
    {
        const f32 overlapX = std::min(m.Max.X - s.Min.X, s.Max.X - m.Min.X);
        const f32 overlapY = std::min(m.Max.Y - s.Min.Y, s.Max.Y - m.Min.Y);
        if (overlapX < -TouchTolerance || overlapY < -TouchTolerance) return std::nullopt; // separated
        if (std::min(overlapX, overlapY) > TouchTolerance) return std::nullopt;           // inside

        // Outward normals of the faces in contact (from s toward m).
        const Vec2 nX       = { m.Center().X >= s.Center().X ? 1.0f : -1.0f, 0.0f };
        const Vec2 nY       = { 0.0f, m.Center().Y >= s.Center().Y ? 1.0f : -1.0f };
        const bool touchX   = overlapX <= TouchTolerance;
        const bool touchY   = overlapY <= TouchTolerance;
        const bool intoX    = Dot(delta, nX) < 0.0f;
        const bool intoY    = Dot(delta, nY) < 0.0f;

        // Corner contact: only a move into both faces is blocked. Report the
        // axis with less motion, so the slide keeps the dominant direction.
        if (touchX && touchY) {
            if (!intoX || !intoY) return Result{};
            return Result{ .ToI = 0.0f, .Normal = std::abs(delta.X) < std::abs(delta.Y) ? nX : nY, .Hit = true };
        }
        const Vec2 n    = touchX ? nX : nY;
        const bool into = touchX ? intoX : intoY;
        if (!into) return Result{};
        return Result{ .ToI = 0.0f, .Normal = n, .Hit = true };
    }

    // Contact rule for a substep pair that only *touches* at t = 0 (the
    // non-slab counterpart of TouchingContact). Moving away or along the
    // surface is free; moving into it is a hit at ToI 0 with the surface
    // normal. Without this, a circle exactly touching a wall got a normal
    // opposite its motion for every direction and could never leave.
    //
    // Returns nullopt for a genuine overlap; that keeps the motion-opposed
    // normal below, which directional arcs depend on.
    std::optional<Result> ShallowContact(const Collider2D& mover, const Vec2 origin, const Vec2 delta,
                                         const Collider2D& other, const Vec2 otherPos) noexcept
    {
        const Narrowphase::Contact c = Narrowphase::ComputeContact(mover, origin, other, otherPos);
        if (!c.Hit || c.Depth > TouchTolerance) return std::nullopt;
        if (Dot(delta, c.Normal) >= 0.0f) return Result{};
        return Result{ .ToI = 0.0f, .Normal = c.Normal, .Hit = true };
    }
} // anonymous namespace

Result MovingAABBvsAABB(const AABB& moving, const Vec2 delta,
                        const AABB& stationary) noexcept
{
    if (const std::optional<Result> touch = TouchingContact(moving, delta, stationary)) return *touch;
    if (StartsInside(moving, stationary)) return {};

    f32 tEnterX = 0, tExitX = 0;
    f32 tEnterY = 0, tExitY = 0;
    if (!AxisSlab(moving.Min.X, moving.Max.X,
                  stationary.Min.X, stationary.Max.X,
                  delta.X, tEnterX, tExitX))
        return {};
    if (!AxisSlab(moving.Min.Y, moving.Max.Y,
                  stationary.Min.Y, stationary.Max.Y,
                  delta.Y, tEnterY, tExitY))
        return {};

    const f32 tEnter = std::max(tEnterX, tEnterY);
    const f32 tExit  = std::min(tExitX, tExitY);

    // No intersection in [0, 1]:
    //   tEnter > tExit : slabs miss in time
    //   tEnter > 1     : contact happens past the supplied delta
    //   tExit  < 0     : contact already happened in the past
    if (tEnter > tExit || tEnter > 1.0f || tExit < 0.0f) return {};

    Result r{};
    r.Hit = true;
    r.ToI = std::max(tEnter, 0.0f);

    // The axis with the later entry time decides which face was hit; normal
    // points opposite to the mover's motion on that axis.
    if (tEnterX > tEnterY)
        r.Normal = {delta.X > 0.0f ? -1.0f : 1.0f, 0.0f};
    else
        r.Normal = {0.0f, delta.Y > 0.0f ? -1.0f : 1.0f};
    return r;
}

Result Substep(const Collider2D& mover, const Vec2 origin, const Vec2 delta,
               const Collider2D& other, const Vec2 otherPos,
               const u32         substeps) noexcept
{
    if (substeps == 0) return {};

    // Already overlapping at t = 0: touching follows ShallowContact's rules;
    // genuinely inside means the mover got in some legitimate way (through a
    // pass-through face of a directional collider, or placed there), and
    // the obstacle doesn't block it again until it has fully left. A
    // directional arc gates getting *in*, so blocking moves made from inside
    // would trap the mover (it used to: a NorthOnly gate entered from the
    // south could only be left through its north face).
    if (Narrowphase::Overlap(mover, origin, other, otherPos))
        return ShallowContact(mover, origin, delta, other, otherPos).value_or(Result{});

    // Walk the delta in N equal steps. The first step where the boolean
    // narrowphase fires gives ToI = (k-1)/N — the position just before contact.
    for (u32 k = 1; k <= substeps; ++k) {
        const f32  t       = static_cast<f32>(k) / static_cast<f32>(substeps);
        const Vec2 testPos = {origin.X + delta.X * t, origin.Y + delta.Y * t};
        if (!Narrowphase::Overlap(mover, testPos, other, otherPos)) continue;

        Result r{};
        r.Hit = true;
        r.ToI = static_cast<f32>(k - 1) / static_cast<f32>(substeps);

        // Normal: the penetration axis at the first overlapping step. The
        // mover is at most one substep deep, so the least-penetration axis is
        // the face it just crossed — the wall's face normal for a rotated
        // wall, the corner direction at a corner. (Pre-22E this was the
        // obstacle-center → mover-center vector, which points nowhere near
        // the face on a long wall and broke sliding.)
        const Narrowphase::Contact c = Narrowphase::ComputeContact(mover, testPos, other, otherPos);
        if (c.Hit) {
            // Grazing the surface without moving into it (sliding along a face
            // at exactly touching distance) isn't a hit.
            if (c.Depth <= TouchTolerance && Dot(delta, c.Normal) >= 0.0f) continue;
            r.Normal = c.Normal;
            return r;
        }

        // Unreachable in practice (Overlap just fired); fall back to the
        // opposite of motion rather than returning a zero normal.
        const f32 dl = Length(delta);
        r.Normal     = dl > SweepEpsilon ? Vec2{-delta.X / dl, -delta.Y / dl} : Vec2{1.0f, 0.0f};
        return r;
    }
    return {};
}

Result SweepAgainst(const Collider2D& mover, const Vec2 origin, const Vec2 delta,
                    const Collider2D& other, const Vec2 otherPos) noexcept
{
    const bool axisAlignedBoxA = (mover.Shape == ColliderShape::Box && mover.Rotation == 0.0f);
    const bool axisAlignedBoxB = (other.Shape == ColliderShape::Box && other.Rotation == 0.0f);

    if (axisAlignedBoxA && axisAlignedBoxB) {
        // Both axis-aligned boxes — slab method is exact and cheapest.
        const AABB moverAABB = PhysicsWorld::ComputeWorldAABB(mover, origin);
        const AABB otherAABB = PhysicsWorld::ComputeWorldAABB(other, otherPos);
        return MovingAABBvsAABB(moverAABB, delta, otherAABB);
    }

    // Anything involving a circle or a rotated box — sub-step.
    return Substep(mover, origin, delta, other, otherPos, SubstepCount(mover, delta, other));
}

} // namespace Arcbit::Sweep
