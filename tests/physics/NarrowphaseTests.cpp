#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/PhysicsWorld.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Arcbit;
using namespace Arcbit::Narrowphase;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32 Tol = 1e-3f;

void CheckNear(const Vec2 actual, const Vec2 expected)
{
    CHECK_THAT(actual.X, WithinAbs(expected.X, Tol));
    CHECK_THAT(actual.Y, WithinAbs(expected.Y, Tol));
}
} // namespace

TEST_CASE("AABB overlap", "[physics][narrowphase]")
{
    constexpr AABB a = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, { 10.0f, 10.0f });
    CHECK(AABBOverlap(a, AABB::FromCenterHalfExtents({ 5.0f, 5.0f }, { 10.0f, 10.0f })));
    CHECK_FALSE(AABBOverlap(a, AABB::FromCenterHalfExtents({ 30.0f, 0.0f }, { 5.0f, 5.0f })));
    CHECK(AABBOverlap(a, AABB::FromCenterHalfExtents({ 20.0f, 0.0f }, { 10.0f, 10.0f }))); // edge touch counts
}

TEST_CASE("AABB vs circle overlap", "[physics][narrowphase]")
{
    constexpr AABB box = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, { 10.0f, 10.0f });
    CHECK(AABBCircleOverlap(box, { 0.0f, 0.0f }, 1.0f));          // center inside
    CHECK(AABBCircleOverlap(box, { 12.0f, 12.0f }, 5.0f));        // grazes the corner
    CHECK_FALSE(AABBCircleOverlap(box, { 20.0f, 20.0f }, 5.0f));
    CHECK(AABBCircleOverlap(box, { 15.0f, 0.0f }, 5.0f));         // exactly touching counts
}

TEST_CASE("Circle vs circle overlap", "[physics][narrowphase]")
{
    CHECK(CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 4.0f, 0.0f }, 5.0f));
    CHECK_FALSE(CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 20.0f, 0.0f }, 5.0f));
    CHECK(CircleOverlap({ 0.0f, 0.0f }, 5.0f, { 10.0f, 0.0f }, 5.0f)); // tangent counts
}

TEST_CASE("Overlap dispatcher picks the right test per shape pair", "[physics][narrowphase]")
{
    const Collider2D box{ .Shape = ColliderShape::Box, .HalfExtents = { 8.0f, 8.0f } };
    const Collider2D circle{ .Shape = ColliderShape::Circle, .Radius = 6.0f };
    const Collider2D smallCircle{ .Shape = ColliderShape::Circle, .Radius = 4.0f };

    CHECK(Overlap(box, { 0.0f, 0.0f }, box, { 4.0f, 4.0f }));
    CHECK_FALSE(Overlap(box, { 0.0f, 0.0f }, box, { 100.0f, 100.0f }));
    CHECK(Overlap(circle, { 5.0f, 5.0f }, box, { 0.0f, 0.0f }));
    CHECK(Overlap(box, { 0.0f, 0.0f }, circle, { 5.0f, 5.0f }));
    CHECK_FALSE(Overlap(circle, { 50.0f, 50.0f }, box, { 0.0f, 0.0f }));
    CHECK(Overlap(circle, { 0.0f, 0.0f }, smallCircle, { 8.0f, 0.0f }));
    CHECK_FALSE(Overlap(circle, { 0.0f, 0.0f }, smallCircle, { 50.0f, 0.0f }));
}

TEST_CASE("Overlap honors collider Offset", "[physics][narrowphase]")
{
    const Collider2D left{ .Shape = ColliderShape::Box, .Offset = { -10.0f, 0.0f }, .HalfExtents = { 4.0f, 4.0f } };
    const Collider2D right{ .Shape = ColliderShape::Box, .Offset = { 10.0f, 0.0f }, .HalfExtents = { 4.0f, 4.0f } };
    CHECK_FALSE(Overlap(left, { 0.0f, 0.0f }, right, { 0.0f, 0.0f }));
}

TEST_CASE("Rotated boxes collide on their real outline, not their bounding AABB", "[physics][narrowphase][obb]")
{
    const Collider2D plank{ .Shape = ColliderShape::Box, .Rotation = QuarterPi, .HalfExtents = { 40.0f, 2.0f } };
    const Collider2D smallBox{ .Shape = ColliderShape::Box, .HalfExtents = { 4.0f, 4.0f } };
    const Collider2D circle{ .Shape = ColliderShape::Circle, .Radius = 5.0f };
    const Vec2       corner = { 22.0f, -22.0f }; // inside the plank's bounding AABB, off the plank
    const Vec2       onSpan = { 20.0f, 20.0f };

    REQUIRE(AABBOverlap(PhysicsWorld::ComputeWorldAABB(plank, {}), PhysicsWorld::ComputeWorldAABB(smallBox, corner)));

    CHECK_FALSE(Overlap(plank, {}, smallBox, corner));
    CHECK_FALSE(Overlap(smallBox, corner, plank, {}));
    CHECK(Overlap(plank, {}, smallBox, onSpan));
    CHECK_FALSE(Overlap(plank, {}, circle, corner));
    CHECK_FALSE(Overlap(circle, corner, plank, {}));
    CHECK(Overlap(circle, onSpan, plank, {}));
}

TEST_CASE("ComputeContact reports the struck face's normal", "[physics][narrowphase][obb]")
{
    const Collider2D wall{ .Shape = ColliderShape::Box, .Rotation = 30.0f * DegToRad, .HalfExtents = { 100.0f, 4.0f } };
    const OBB        wallBox = OBB::FromCollider(wall, {});
    const Vec2       faceN   = wallBox.AxisY; // the wall's +Y face, in world space

    SECTION("box mover 1 px into the face")
    {
        const Collider2D mover{ .Shape = ColliderShape::Box, .HalfExtents = { 8.0f, 8.0f } };
        const f32        radius = OBB::FromCollider(mover, {}).ProjectedRadius(faceN);
        const Contact    c      = ComputeContact(mover, wallBox.ToWorldDir({ 50.0f, 4.0f + radius - 1.0f }), wall, {});
        REQUIRE(c.Hit);
        CheckNear(c.Normal, faceN);
        CHECK_THAT(c.Depth, WithinAbs(1.0f, Tol));
    }

    SECTION("circle mover, both argument orders")
    {
        const Collider2D ball{ .Shape = ColliderShape::Circle, .Radius = 8.0f };
        const Vec2       ballPos = wallBox.ToWorldDir({ 50.0f, 11.0f });
        const Contact    hit     = ComputeContact(ball, ballPos, wall, {});
        REQUIRE(hit.Hit);
        CheckNear(hit.Normal, faceN);

        const Contact flipped = ComputeContact(wall, {}, ball, ballPos);
        REQUIRE(flipped.Hit);
        CheckNear(flipped.Normal, faceN * -1.0f); // always points from b toward a
    }

    SECTION("circle center inside a box exits through the nearest face")
    {
        const Collider2D ball{ .Shape = ColliderShape::Circle, .Radius = 8.0f };
        const Collider2D slab{ .Shape = ColliderShape::Box, .HalfExtents = { 10.0f, 4.0f } };
        const Contact    c = ComputeContact(ball, { 0.0f, 1.0f }, slab, {});
        REQUIRE(c.Hit);
        CheckNear(c.Normal, { 0.0f, 1.0f });
    }

    SECTION("separated shapes report no contact")
    {
        const Collider2D mover{ .Shape = ColliderShape::Box, .HalfExtents = { 8.0f, 8.0f } };
        CHECK_FALSE(ComputeContact(mover, { 0.0f, 200.0f }, wall, {}).Hit);
    }
}
