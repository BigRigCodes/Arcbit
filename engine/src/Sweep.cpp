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
            // Stationary on this axis — overlap decides "always open" vs "never enterable".
            if (mMax < sMin || mMin > sMax) return false;
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

    // Boxes closer than this (gap or overlap, in px) count as touching. Covers
    // the float noise left by `position + delta * ToI` after a resolved hit.
    constexpr f32 TouchTolerance = 1e-3f;

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
} // anonymous namespace

Result MovingAABBvsAABB(const AABB& moving, const Vec2 delta,
                        const AABB& stationary) noexcept
{
    if (const std::optional<Result> touch = TouchingContact(moving, delta, stationary)) return *touch;

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

    // If the mover already overlaps the obstacle at t=0, the obstacle-center
    // → mover-position vector (the normal we'd compute below) is essentially
    // noise — the mover could be anywhere inside the obstacle. Match the
    // slab method's convention instead: report a normal pointing back along
    // the motion direction (the axis-equivalent of "the face the mover is
    // moving toward"). Without this, a circle mover inside a directional
    // box collider (e.g. walked through a Vertical-arc tree from the side)
    // would get spurious off-axis normals that fall inside the arc and trap
    // it; with this fix, the arc check sees the actual motion direction.
    if (Narrowphase::Overlap(mover, origin, other, otherPos)) {
        Result r{};
        r.Hit       = true;
        r.ToI       = 0.0f;
        const f32 dl = Length(delta);
        r.Normal    = dl > SweepEpsilon
                          ? Vec2{-delta.X / dl, -delta.Y / dl}
                          : Vec2{1.0f, 0.0f};
        return r;
    }

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

// ---------------------------------------------------------------------------
// SelfTest — slab-method correctness on hand-picked cases.
// ---------------------------------------------------------------------------

#ifdef ARCBIT_DEBUG
namespace
{
    // Rotated-obstacle sweeps (22E): the substep normal must be the struck
    // face's normal, and the adaptive substep count must catch thin planks a
    // fixed 8-step sweep tunnels through.
    void RotatedSweepSelfTest()
    {
        constexpr f32 tol = 0.001f;

        // Box mover moving straight up into the underside of a 30° wall. The
        // contact normal is the wall's face normal, not straight down and not
        // center-to-center.
        const Collider2D wall{ .Shape = ColliderShape::Box, .Rotation = 30.0f * DegToRad, .HalfExtents = { 100.0f, 4.0f } };
        const Vec2       faceN = OBB::FromCollider(wall, {}).AxisY;
        const Collider2D mover{ .Shape = ColliderShape::Box, .HalfExtents = { 8.0f, 8.0f } };
        const Vec2       start = OBB::FromCollider(wall, {}).ToWorldDir({ 50.0f, 30.0f });

        const Result hit = SweepAgainst(mover, start, { 0.0f, -40.0f }, wall, {});
        ARCBIT_ASSERT(hit.Hit && hit.ToI > 0.0f && hit.ToI < 1.0f, "Sweep::SelfTest: rotated wall missed");
        ARCBIT_ASSERT(std::abs(hit.Normal.X - faceN.X) < tol && std::abs(hit.Normal.Y - faceN.Y) < tol,
                      "Sweep::SelfTest: rotated wall normal is not the face normal");

        // Tiny mover crossing a 2 px-thick 45° plank in one 64 px move (a tile
        // step). Sample points at 8 substeps straddle the plank; adaptive
        // substepping must not.
        const Collider2D plank{ .Shape = ColliderShape::Box, .Rotation = QuarterPi, .HalfExtents = { 40.0f, 1.0f } };
        const Collider2D speck{ .Shape = ColliderShape::Box, .HalfExtents = { 1.0f, 1.0f } };
        const Vec2       from = { -36.0f, 0.0f };
        const Vec2       step = { 64.0f, 0.0f };
        ARCBIT_ASSERT(!Substep(speck, from, step, plank, {}, 8u).Hit,
                      "Sweep::SelfTest: test setup — 8 substeps should tunnel this plank");
        ARCBIT_ASSERT(SweepAgainst(speck, from, step, plank, {}).Hit,
                      "Sweep::SelfTest: adaptive substeps tunneled through a thin plank");
    }
} // anonymous namespace
#endif

void SelfTest()
{
    #ifdef ARCBIT_DEBUG
    RotatedSweepSelfTest();
    constexpr f32 tol = 0.001f;

    // Head-on X: closing distance 20, delta.x 40 -> ToI = 0.5, normal = (-1, 0).
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 0.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {40.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: head-on X miss");
        ARCBIT_ASSERT(std::abs(r.ToI - 0.5f) < tol, "Sweep::SelfTest: head-on X ToI wrong");
        ARCBIT_ASSERT(r.Normal.X < 0.0f && std::abs(r.Normal.Y) < tol,
                      "Sweep::SelfTest: head-on X normal wrong");
    }

    // No-contact: stationary box well above the mover's path.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 100.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {40.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(!r.Hit, "Sweep::SelfTest: false positive on miss");
        ARCBIT_ASSERT(r.ToI == 1.0f, "Sweep::SelfTest: ToI should be 1.0 on miss");
    }

    // Tangent at start: edges already touching. Moving into the face is a Hit
    // at ToI=0 with the face normal; moving away or along the face is free.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({10.0f, 0.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {1.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: tangent contact missed");
        ARCBIT_ASSERT(r.ToI == 0.0f, "Sweep::SelfTest: tangent contact should be ToI=0");
        ARCBIT_ASSERT(r.Normal.X < 0.0f, "Sweep::SelfTest: tangent contact normal wrong");

        ARCBIT_ASSERT(!MovingAABBvsAABB(mover, {-1.0f, 0.0f}, stationary).Hit,
                      "Sweep::SelfTest: moving away from a touching box was blocked");
        ARCBIT_ASSERT(!MovingAABBvsAABB(mover, {0.0f, 3.0f}, stationary).Hit,
                      "Sweep::SelfTest: sliding along a touching face was blocked");

        // Same, with the float noise a resolved hit leaves behind.
        constexpr AABB nudgedIn  = AABB::FromCenterHalfExtents({0.00001f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB nudgedOut = AABB::FromCenterHalfExtents({-0.00001f, 0.0f}, {5.0f, 5.0f});
        ARCBIT_ASSERT(!MovingAABBvsAABB(nudgedIn, {-1.0f, 0.0f}, stationary).Hit,
                      "Sweep::SelfTest: micro-overlap trapped a mover moving away");
        ARCBIT_ASSERT(!MovingAABBvsAABB(nudgedOut, {0.0f, -3.0f}, stationary).Hit,
                      "Sweep::SelfTest: micro-gap blocked sliding along the face");
        ARCBIT_ASSERT(MovingAABBvsAABB(nudgedOut, {1.0f, 0.0f}, stationary).Hit,
                      "Sweep::SelfTest: micro-gap let the mover push into the face");

        // Corner-to-corner: only a move into both faces is blocked.
        constexpr AABB corner = AABB::FromCenterHalfExtents({10.0f, 10.0f}, {5.0f, 5.0f});
        ARCBIT_ASSERT(!MovingAABBvsAABB(mover, {1.0f, 0.0f}, corner).Hit,
                      "Sweep::SelfTest: grazing past a touching corner was blocked");
        ARCBIT_ASSERT(MovingAABBvsAABB(mover, {1.0f, 1.0f}, corner).Hit,
                      "Sweep::SelfTest: moving into a touching corner was not blocked");
    }

    // Glancing: mover travels along X but stationary is offset on Y → no overlap.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 12.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {40.0f, 0.0f}, stationary);
        ARCBIT_ASSERT(!r.Hit, "Sweep::SelfTest: glancing horizontal pass should miss");
    }

    // Head-on Y selecting Y normal: closing 20 down, delta.y -40 -> ToI 0.5, normal (0, +1).
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({0.0f, -30.0f}, {5.0f, 5.0f});
        const Result   r          = MovingAABBvsAABB(mover, {0.0f, -40.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: head-on Y miss");
        ARCBIT_ASSERT(std::abs(r.ToI - 0.5f) < tol, "Sweep::SelfTest: head-on Y ToI wrong");
        ARCBIT_ASSERT(r.Normal.Y > 0.0f && std::abs(r.Normal.X) < tol,
                      "Sweep::SelfTest: head-on Y normal wrong");
    }

    // Diagonal hit where X-entry > Y-entry: corner approach selects X normal.
    {
        constexpr AABB mover      = AABB::FromCenterHalfExtents({0.0f, 0.0f}, {5.0f, 5.0f});
        constexpr AABB stationary = AABB::FromCenterHalfExtents({30.0f, 12.0f}, {5.0f, 5.0f});
        // Move {40, 20}. X needs to travel 20 to enter (t=0.5); Y needs 2 to enter (t=0.1).
        // tEnterX > tEnterY → X normal selected.
        const Result r = MovingAABBvsAABB(mover, {40.0f, 20.0f}, stationary);
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: diagonal hit missed");
        ARCBIT_ASSERT(std::abs(r.ToI - 0.5f) < tol, "Sweep::SelfTest: diagonal ToI wrong");
        ARCBIT_ASSERT(r.Normal.X < 0.0f && std::abs(r.Normal.Y) < tol,
                      "Sweep::SelfTest: diagonal normal axis wrong");
    }

    // Sub-step circle vs box: circle moving rightward into a box should report
    // a hit with the normal pointing back toward the mover.
    {
        const Collider2D circle{
            .Shape  = ColliderShape::Circle,
            .Radius = 4.0f,
        };
        const Collider2D box{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {5.0f, 5.0f},
        };
        const Result r = SweepAgainst(circle, {0.0f, 0.0f}, {40.0f, 0.0f},
                                      box, {30.0f, 0.0f});
        ARCBIT_ASSERT(r.Hit, "Sweep::SelfTest: substep circle-box missed contact");
        ARCBIT_ASSERT(r.ToI < 1.0f, "Sweep::SelfTest: substep circle-box ToI should be < 1");
        ARCBIT_ASSERT(r.Normal.X < 0.0f, "Sweep::SelfTest: substep circle-box normal direction wrong");
    }

    LOG_DEBUG(Engine, "Sweep::SelfTest passed");
    #endif
}
} // namespace Arcbit::Sweep
