#include "TestSupport.h"

#include <arcbit/physics/AABB.h>
#include <arcbit/physics/OBB.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32 Tol = 1e-4f;

void CheckNear(const Vec2 actual, const Vec2 expected)
{
    CHECK_THAT(actual.X, WithinAbs(expected.X, Tol));
    CHECK_THAT(actual.Y, WithinAbs(expected.Y, Tol));
}
} // namespace

TEST_CASE("AABB construction and derived values", "[physics][geometry]")
{
    constexpr AABB box = AABB::FromCenterHalfExtents({ 10.0f, -4.0f }, { 3.0f, 2.0f });
    STATIC_CHECK(box.Min == Vec2{ 7.0f, -6.0f });
    STATIC_CHECK(box.Max == Vec2{ 13.0f, -2.0f });
    CHECK(box.Center() == Vec2{ 10.0f, -4.0f });
    CHECK(box.HalfExtents() == Vec2{ 3.0f, 2.0f });
    CHECK(box.Size() == Vec2{ 6.0f, 4.0f });
    CHECK(box.IsValid());
    CHECK_FALSE(AABB::FromMinMax({ 1.0f, 0.0f }, { 0.0f, 1.0f }).IsValid());
}

TEST_CASE("AABB overlap and containment are inclusive at the edges", "[physics][geometry]")
{
    constexpr AABB box = AABB::FromMinMax({ 0.0f, 0.0f }, { 10.0f, 10.0f });
    CHECK(box.Overlaps(AABB::FromMinMax({ 10.0f, 0.0f }, { 20.0f, 10.0f })));       // shared edge
    CHECK(box.Overlaps(AABB::FromMinMax({ 10.0f, 10.0f }, { 20.0f, 20.0f })));      // shared corner
    CHECK_FALSE(box.Overlaps(AABB::FromMinMax({ 10.1f, 0.0f }, { 20.0f, 10.0f })));
    CHECK(box.Overlaps(AABB::FromMinMax({ 2.0f, 2.0f }, { 3.0f, 3.0f })));          // fully inside

    CHECK(box.Contains({ 0.0f, 0.0f }));
    CHECK(box.Contains({ 10.0f, 10.0f }));
    CHECK_FALSE(box.Contains({ 10.01f, 5.0f }));
}

TEST_CASE("OBB from an unrotated collider is axis-aligned", "[physics][geometry][obb]")
{
    const Collider2D col{ .Shape = ColliderShape::Box, .Offset = { 2.0f, 3.0f }, .HalfExtents = { 5.0f, 1.0f } };
    const OBB        box = OBB::FromCollider(col, { 10.0f, 10.0f });
    CHECK(box.Center == Vec2{ 12.0f, 13.0f }); // offset applied
    CHECK(box.AxisX == Vec2{ 1.0f, 0.0f });
    CHECK(box.AxisY == Vec2{ 0.0f, 1.0f });
    CHECK(box.ProjectedRadius({ 1.0f, 0.0f }) == 5.0f);
    CHECK(box.ProjectedRadius({ 0.0f, 1.0f }) == 1.0f);
}

TEST_CASE("OBB axes follow the engine rotation convention", "[physics][geometry][obb]")
{
    // +90° (clockwise on screen, +Y down): local +X points world +Y.
    const OBB box = OBB::FromCollider(Collider2D{ .Shape = ColliderShape::Box, .Rotation = HalfPi,
                                                  .HalfExtents = { 4.0f, 2.0f } }, {});
    CheckNear(box.AxisX, { 0.0f, 1.0f });
    CheckNear(box.AxisY, { -1.0f, 0.0f });
    CHECK_THAT(Dot(box.AxisX, box.AxisY), WithinAbs(0.0f, Tol)); // orthonormal
    CHECK_THAT(box.ProjectedRadius({ 0.0f, 1.0f }), WithinAbs(4.0f, Tol));
}

TEST_CASE("OBB local and world transforms are inverses", "[physics][geometry][obb]")
{
    const OBB  box = OBB::FromCollider(Collider2D{ .Shape = ColliderShape::Box, .Rotation = 0.6f,
                                                   .HalfExtents = { 4.0f, 2.0f } }, { 5.0f, -3.0f });
    const Vec2 local{ 3.0f, -1.5f };
    const Vec2 world = box.Center + box.ToWorldDir(local);
    CheckNear(box.ToLocal(world), local);
    CHECK_THAT(Length(box.ToWorldDir({ 1.0f, 0.0f })), WithinAbs(1.0f, Tol)); // rotation preserves length
}

TEST_CASE("OBB projected radius at 45 degrees", "[physics][geometry][obb]")
{
    // A square rotated 45° projects to half-diagonal on the world axes.
    const OBB box = OBB::FromCollider(Collider2D{ .Shape = ColliderShape::Box, .Rotation = QuarterPi,
                                                  .HalfExtents = { 1.0f, 1.0f } }, {});
    CHECK_THAT(box.ProjectedRadius({ 1.0f, 0.0f }), WithinAbs(1.41421356f, Tol));
}
