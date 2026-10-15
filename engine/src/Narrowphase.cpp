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

} // namespace Arcbit::Narrowphase
