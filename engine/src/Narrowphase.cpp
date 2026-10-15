#include <arcbit/physics/Narrowphase.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/physics/PhysicsWorld.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Arcbit::Narrowphase
{
bool AABBOverlap(const AABB& a, const AABB& b) noexcept
{
    return a.Overlaps(b);
}

bool AABBCircleOverlap(const AABB& box, const Vec2 center, const f32 radius) noexcept
{
    // Closest point on the box to the circle's center, then distance squared.
    const f32 cx = std::max(box.Min.X, std::min(center.X, box.Max.X));
    const f32 cy = std::max(box.Min.Y, std::min(center.Y, box.Max.Y));
    const f32 dx = center.X - cx;
    const f32 dy = center.Y - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}

bool CircleOverlap(const Vec2 centerA, const f32 radiusA,
                   const Vec2 centerB, const f32 radiusB) noexcept
{
    const f32 dx  = centerB.X - centerA.X;
    const f32 dy  = centerB.Y - centerA.Y;
    const f32 sum = radiusA + radiusB;
    return (dx * dx + dy * dy) <= (sum * sum);
}

namespace
{
    constexpr f32 ContactEpsilon = 1e-6f;

    [[nodiscard]] f32 SignOrPositive(const f32 v) noexcept { return v < 0.0f ? -1.0f : 1.0f; }

    // SAT over the four face axes. The tightest axis is the contact normal,
    // flipped so it points from b toward a.
    Contact BoxBoxContact(const OBB& a, const OBB& b) noexcept
    {
        const Vec2 axes[4] = { a.AxisX, a.AxisY, b.AxisX, b.AxisY };
        const Vec2 d       = a.Center - b.Center;

        Contact best{};
        best.Depth = std::numeric_limits<f32>::max();
        for (const Vec2 axis : axes) {
            const f32 dist    = Dot(d, axis);
            const f32 overlap = a.ProjectedRadius(axis) + b.ProjectedRadius(axis) - std::abs(dist);
            if (overlap < 0.0f) return {}; // separating axis found
            if (overlap < best.Depth) {
                best.Depth  = overlap;
                best.Normal = axis * SignOrPositive(dist);
            }
        }
        best.Hit = true;
        return best;
    }

    // Closest point on the box to the circle center, in box-local space.
    // Normal points from the box toward the circle.
    Contact BoxCircleContact(const OBB& box, const Vec2 center, const f32 radius) noexcept
    {
        const Vec2 local   = box.ToLocal(center);
        const Vec2 he      = box.HalfExtents;
        const Vec2 clamped = { std::clamp(local.X, -he.X, he.X), std::clamp(local.Y, -he.Y, he.Y) };
        const Vec2 diff    = local - clamped;
        const f32  dist2   = LengthSquared(diff);
        if (dist2 > radius * radius) return {};

        Contact c{};
        c.Hit = true;
        if (dist2 > ContactEpsilon) {
            const f32 dist = std::sqrt(dist2);
            c.Normal       = box.ToWorldDir(diff / dist);
            c.Depth        = radius - dist;
            return c;
        }

        // Center inside the box — push out through the nearest face.
        const f32 penX = he.X - std::abs(local.X);
        const f32 penY = he.Y - std::abs(local.Y);
        if (penX < penY) {
            c.Normal = box.ToWorldDir({ SignOrPositive(local.X), 0.0f });
            c.Depth  = penX + radius;
        }
        else {
            c.Normal = box.ToWorldDir({ 0.0f, SignOrPositive(local.Y) });
            c.Depth  = penY + radius;
        }
        return c;
    }

    Contact CircleCircleContact(const Vec2 centerA, const f32 radiusA,
                                const Vec2 centerB, const f32 radiusB) noexcept
    {
        const Vec2 d     = centerA - centerB;
        const f32  sum   = radiusA + radiusB;
        const f32  dist2 = LengthSquared(d);
        if (dist2 > sum * sum) return {};

        const f32 dist = std::sqrt(dist2);
        Contact   c{};
        c.Hit    = true;
        c.Depth  = sum - dist;
        c.Normal = dist > ContactEpsilon ? d / dist : Vec2{ 1.0f, 0.0f };
        return c;
    }
} // anonymous namespace

bool OBBOverlap(const OBB& a, const OBB& b) noexcept { return BoxBoxContact(a, b).Hit; }

bool OBBCircleOverlap(const OBB& box, const Vec2 center, const f32 radius) noexcept
{
    const AABB localBox = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, box.HalfExtents);
    return AABBCircleOverlap(localBox, box.ToLocal(center), radius);
}

bool Overlap(const Collider2D& a, const Vec2 worldPosA,
             const Collider2D& b, const Vec2 worldPosB) noexcept
{
    const Vec2 centerA = { worldPosA.X + a.Offset.X, worldPosA.Y + a.Offset.Y };
    const Vec2 centerB = { worldPosB.X + b.Offset.X, worldPosB.Y + b.Offset.Y };

    const bool circleA = (a.Shape == ColliderShape::Circle);
    const bool circleB = (b.Shape == ColliderShape::Circle);

    // Circle vs Circle — exact.
    if (circleA && circleB) return CircleOverlap(centerA, a.Radius, centerB, b.Radius);

    // Box vs Circle — AABB test for an unrotated box, OBB test otherwise.
    if (circleA || circleB) {
        const Collider2D& box      = circleA ? b : a;
        const Vec2        boxPos   = circleA ? worldPosB : worldPosA;
        const Vec2        center   = circleA ? centerA : centerB;
        const f32         radius   = circleA ? a.Radius : b.Radius;
        if (box.Rotation == 0.0f)
            return AABBCircleOverlap(PhysicsWorld::ComputeWorldAABB(box, boxPos), center, radius);
        return OBBCircleOverlap(OBB::FromCollider(box, boxPos), center, radius);
    }

    // Box vs Box — fast path when both are axis-aligned, SAT otherwise.
    if (a.Rotation == 0.0f && b.Rotation == 0.0f)
        return AABBOverlap(PhysicsWorld::ComputeWorldAABB(a, worldPosA),
                           PhysicsWorld::ComputeWorldAABB(b, worldPosB));
    return OBBOverlap(OBB::FromCollider(a, worldPosA), OBB::FromCollider(b, worldPosB));
}

Contact ComputeContact(const Collider2D& a, const Vec2 worldPosA,
                       const Collider2D& b, const Vec2 worldPosB) noexcept
{
    const Vec2 centerA = { worldPosA.X + a.Offset.X, worldPosA.Y + a.Offset.Y };
    const Vec2 centerB = { worldPosB.X + b.Offset.X, worldPosB.Y + b.Offset.Y };

    const bool circleA = (a.Shape == ColliderShape::Circle);
    const bool circleB = (b.Shape == ColliderShape::Circle);

    if (circleA && circleB) return CircleCircleContact(centerA, a.Radius, centerB, b.Radius);
    if (circleA) return BoxCircleContact(OBB::FromCollider(b, worldPosB), centerA, a.Radius);
    if (circleB) {
        // BoxCircleContact's normal points box → circle, i.e. a → b here; flip.
        Contact c = BoxCircleContact(OBB::FromCollider(a, worldPosA), centerB, b.Radius);
        c.Normal  = c.Normal * -1.0f;
        return c;
    }
    return BoxBoxContact(OBB::FromCollider(a, worldPosA), OBB::FromCollider(b, worldPosB));
}

// ---------------------------------------------------------------------------
// SelfTest — overlap correctness on hand-picked positive and negative cases.
// ---------------------------------------------------------------------------

#ifdef ARCBIT_DEBUG
namespace
{
    bool NearlyEqual(const Vec2 a, const Vec2 b, const f32 tol = 1e-3f)
    {
        return std::abs(a.X - b.X) < tol && std::abs(a.Y - b.Y) < tol;
    }

    // Rotated-box overlap: a thin 45° plank must not claim the empty corners
    // of its bounding AABB (the pre-22E conservative behavior).
    void OBBOverlapSelfTest()
    {
        const Collider2D plank{ .Shape = ColliderShape::Box, .Rotation = QuarterPi, .HalfExtents = { 40.0f, 2.0f } };
        const Collider2D smallBox{ .Shape = ColliderShape::Box, .HalfExtents = { 4.0f, 4.0f } };
        const Collider2D circle{ .Shape = ColliderShape::Circle, .Radius = 5.0f };
        const Vec2       corner = { 22.0f, -22.0f }; // inside the plank's bounding AABB, off the plank

        ARCBIT_ASSERT(AABBOverlap(PhysicsWorld::ComputeWorldAABB(plank, {}),
                                  PhysicsWorld::ComputeWorldAABB(smallBox, corner)),
                      "Narrowphase: test setup — corner should sit inside the plank's bounding AABB");
        ARCBIT_ASSERT(!Overlap(plank, {}, smallBox, corner), "Narrowphase: OBB claimed its bounding-AABB corner");
        ARCBIT_ASSERT(!Overlap(smallBox, corner, plank, {}), "Narrowphase: OBB corner miss not symmetric");
        ARCBIT_ASSERT(Overlap(plank, {}, smallBox, { 20.0f, 20.0f }), "Narrowphase: box on the plank's span missed");
        ARCBIT_ASSERT(!Overlap(plank, {}, circle, corner), "Narrowphase: OBB-circle claimed AABB corner");
        ARCBIT_ASSERT(!Overlap(circle, corner, plank, {}), "Narrowphase: OBB-circle corner miss not symmetric");
        ARCBIT_ASSERT(Overlap(circle, { 20.0f, 20.0f }, plank, {}), "Narrowphase: circle on the plank missed");
    }

    // ComputeContact must report the struck face's normal, not the
    // center-to-center direction — that's what lets movers slide along a
    // rotated wall.
    void ContactSelfTest()
    {
        const Collider2D wall{ .Shape = ColliderShape::Box, .Rotation = 30.0f * DegToRad, .HalfExtents = { 100.0f, 4.0f } };
        const OBB        wallBox = OBB::FromCollider(wall, {});
        const Vec2       faceN   = wallBox.AxisY; // the wall's +Y face, in world space

        // Unrotated box mover, 1 px into the +Y face, well away from the wall center.
        const Collider2D mover{ .Shape = ColliderShape::Box, .HalfExtents = { 8.0f, 8.0f } };
        const f32        moverRadius = OBB::FromCollider(mover, {}).ProjectedRadius(faceN);
        const Vec2       moverPos    = wallBox.ToWorldDir({ 50.0f, 4.0f + moverRadius - 1.0f });
        const Contact    boxHit      = ComputeContact(mover, moverPos, wall, {});
        ARCBIT_ASSERT(boxHit.Hit && NearlyEqual(boxHit.Normal, faceN), "Narrowphase: box-OBB contact not the face normal");
        ARCBIT_ASSERT(std::abs(boxHit.Depth - 1.0f) < 1e-3f, "Narrowphase: box-OBB contact depth wrong");

        // Circle mover, same face; then the mirrored argument order.
        const Collider2D ball{ .Shape = ColliderShape::Circle, .Radius = 8.0f };
        const Vec2       ballPos = wallBox.ToWorldDir({ 50.0f, 4.0f + 7.0f });
        const Contact    ballHit = ComputeContact(ball, ballPos, wall, {});
        ARCBIT_ASSERT(ballHit.Hit && NearlyEqual(ballHit.Normal, faceN), "Narrowphase: circle-OBB contact not the face normal");
        const Contact flipped = ComputeContact(wall, {}, ball, ballPos);
        ARCBIT_ASSERT(flipped.Hit && NearlyEqual(flipped.Normal, faceN * -1.0f), "Narrowphase: contact normal not b → a");

        // Circle center inside a box resolves through the nearest face.
        const Collider2D slab{ .Shape = ColliderShape::Box, .HalfExtents = { 10.0f, 4.0f } };
        const Contact    inside = ComputeContact(ball, { 0.0f, 1.0f }, slab, {});
        ARCBIT_ASSERT(inside.Hit && NearlyEqual(inside.Normal, { 0.0f, 1.0f }), "Narrowphase: embedded circle pushed out the wrong face");

        ARCBIT_ASSERT(!ComputeContact(mover, { 0.0f, 200.0f }, wall, {}).Hit, "Narrowphase: contact reported for separated shapes");
    }
} // anonymous namespace
#endif

void SelfTest()
{
#ifdef ARCBIT_DEBUG
    OBBOverlapSelfTest();
    ContactSelfTest();

    // ---- AABB vs AABB --------------------------------------------------------
    constexpr AABB a = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, { 10.0f, 10.0f });
    constexpr AABB b = AABB::FromCenterHalfExtents({ 5.0f, 5.0f }, { 10.0f, 10.0f });   // overlaps
    constexpr AABB c = AABB::FromCenterHalfExtents({ 30.0f, 0.0f }, { 5.0f, 5.0f });    // disjoint
    constexpr AABB d = AABB::FromCenterHalfExtents({ 20.0f, 0.0f }, { 10.0f, 10.0f });  // edge-touch

    ARCBIT_ASSERT(AABBOverlap(a, b),      "Narrowphase: AABBOverlap missed clear overlap");
    ARCBIT_ASSERT(!AABBOverlap(a, c),     "Narrowphase: AABBOverlap reported false positive");
    ARCBIT_ASSERT(AABBOverlap(a, d),      "Narrowphase: edge-touching AABBs should report overlap");

    // ---- AABB vs Circle ------------------------------------------------------
    constexpr AABB box = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, { 10.0f, 10.0f });
    // Center inside box → overlap.
    ARCBIT_ASSERT(AABBCircleOverlap(box, { 0.0f, 0.0f }, 1.0f),
                  "Narrowphase: circle inside box must overlap");
    // Center outside box but radius reaches a corner → overlap.
    ARCBIT_ASSERT(AABBCircleOverlap(box, { 12.0f, 12.0f }, 5.0f),
                  "Narrowphase: corner-grazing circle should overlap");
    // Center outside box and radius too small to reach → no overlap.
    ARCBIT_ASSERT(!AABBCircleOverlap(box, { 20.0f, 20.0f }, 5.0f),
                  "Narrowphase: distant circle should not overlap");
    // Edge contact (closest point on box exactly at radius) — counts as overlap (<=).
    ARCBIT_ASSERT(AABBCircleOverlap(box, { 15.0f, 0.0f }, 5.0f),
                  "Narrowphase: just-touching circle and box edge should overlap");

    // ---- Circle vs Circle ----------------------------------------------------
    ARCBIT_ASSERT(CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 4.0f, 0.0f }, 5.0f),
                  "Narrowphase: clear circle overlap missed");
    ARCBIT_ASSERT(!CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 20.0f, 0.0f }, 5.0f),
                  "Narrowphase: distant circles reported as overlapping");
    // Just-touching: distance == r1+r2 → overlap (<=).
    ARCBIT_ASSERT(CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 10.0f, 0.0f }, 5.0f),
                  "Narrowphase: tangent circles should report overlap");

    // ---- Dispatcher ----------------------------------------------------------
    const Collider2D box1{
        .Shape       = ColliderShape::Box,
        .HalfExtents = { 8.0f, 8.0f },
    };
    const Collider2D box2{
        .Shape       = ColliderShape::Box,
        .HalfExtents = { 8.0f, 8.0f },
    };
    const Collider2D circle1{
        .Shape  = ColliderShape::Circle,
        .Radius = 6.0f,
    };

    // Box-vs-Box: positions chosen so AABBs overlap.
    ARCBIT_ASSERT(Overlap(box1, { 0.0f, 0.0f }, box2, { 4.0f, 4.0f }),
                  "Narrowphase: dispatcher missed Box-Box overlap");
    ARCBIT_ASSERT(!Overlap(box1, { 0.0f, 0.0f }, box2, { 100.0f, 100.0f }),
                  "Narrowphase: dispatcher reported false Box-Box overlap");

    // Circle-vs-Box: circle just inside box.
    ARCBIT_ASSERT(Overlap(circle1, { 5.0f, 5.0f }, box1, { 0.0f, 0.0f }),
                  "Narrowphase: dispatcher missed Circle-Box overlap (circle first)");
    ARCBIT_ASSERT(Overlap(box1, { 0.0f, 0.0f }, circle1, { 5.0f, 5.0f }),
                  "Narrowphase: dispatcher missed Circle-Box overlap (box first)");
    ARCBIT_ASSERT(!Overlap(circle1, { 50.0f, 50.0f }, box1, { 0.0f, 0.0f }),
                  "Narrowphase: dispatcher reported false Circle-Box overlap");

    // Circle-vs-Circle through dispatcher.
    const Collider2D circle2{
        .Shape  = ColliderShape::Circle,
        .Radius = 4.0f,
    };
    ARCBIT_ASSERT(Overlap(circle1, { 0.0f, 0.0f }, circle2, { 8.0f, 0.0f }),
                  "Narrowphase: dispatcher missed Circle-Circle overlap");
    ARCBIT_ASSERT(!Overlap(circle1, { 0.0f, 0.0f }, circle2, { 50.0f, 0.0f }),
                  "Narrowphase: dispatcher reported false Circle-Circle overlap");

    // Offset is honored: identical shapes at the same world position but with
    // opposite Offsets push them apart.
    const Collider2D leftBox{
        .Shape       = ColliderShape::Box,
        .Offset      = { -10.0f, 0.0f },
        .HalfExtents = { 4.0f, 4.0f },
    };
    const Collider2D rightBox{
        .Shape       = ColliderShape::Box,
        .Offset      = { 10.0f, 0.0f },
        .HalfExtents = { 4.0f, 4.0f },
    };
    ARCBIT_ASSERT(!Overlap(leftBox, { 0.0f, 0.0f }, rightBox, { 0.0f, 0.0f }),
                  "Narrowphase: dispatcher ignored collider Offset");

    LOG_DEBUG(Engine, "Narrowphase::SelfTest passed");
#endif
}
} // namespace Arcbit::Narrowphase
