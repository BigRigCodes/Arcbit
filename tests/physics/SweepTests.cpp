#include <arcbit/physics/OBB.h>
#include <arcbit/physics/Sweep.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Arcbit;
using namespace Arcbit::Sweep;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32  Tol   = 1e-3f;
constexpr AABB Mover = AABB::FromCenterHalfExtents({ 0.0f, 0.0f }, { 5.0f, 5.0f });
} // namespace

TEST_CASE("Slab sweep: head-on and diagonal hits", "[physics][sweep]")
{
    SECTION("head-on X")
    {
        const Result r = MovingAABBvsAABB(Mover, { 40.0f, 0.0f }, AABB::FromCenterHalfExtents({ 30.0f, 0.0f }, { 5.0f, 5.0f }));
        REQUIRE(r.Hit);
        CHECK_THAT(r.ToI, WithinAbs(0.5f, Tol));
        CHECK(r.Normal == Vec2{ -1.0f, 0.0f });
    }
    SECTION("head-on Y")
    {
        const Result r = MovingAABBvsAABB(Mover, { 0.0f, -40.0f }, AABB::FromCenterHalfExtents({ 0.0f, -30.0f }, { 5.0f, 5.0f }));
        REQUIRE(r.Hit);
        CHECK_THAT(r.ToI, WithinAbs(0.5f, Tol));
        CHECK(r.Normal == Vec2{ 0.0f, 1.0f });
    }
    SECTION("diagonal: the later-entering axis picks the face")
    {
        // X needs to travel 20 to enter (t = 0.5), Y only 2 (t = 0.1).
        const Result r = MovingAABBvsAABB(Mover, { 40.0f, 20.0f }, AABB::FromCenterHalfExtents({ 30.0f, 12.0f }, { 5.0f, 5.0f }));
        REQUIRE(r.Hit);
        CHECK_THAT(r.ToI, WithinAbs(0.5f, Tol));
        CHECK(r.Normal == Vec2{ -1.0f, 0.0f });
    }
}

TEST_CASE("Slab sweep: misses", "[physics][sweep]")
{
    const Result above = MovingAABBvsAABB(Mover, { 40.0f, 0.0f }, AABB::FromCenterHalfExtents({ 30.0f, 100.0f }, { 5.0f, 5.0f }));
    CHECK_FALSE(above.Hit);
    CHECK(above.ToI == 1.0f);
    CHECK_FALSE(MovingAABBvsAABB(Mover, { 40.0f, 0.0f }, AABB::FromCenterHalfExtents({ 30.0f, 12.0f }, { 5.0f, 5.0f })).Hit);
}

TEST_CASE("Slab sweep: boxes already touching", "[physics][sweep]")
{
    constexpr AABB wall = AABB::FromCenterHalfExtents({ 10.0f, 0.0f }, { 5.0f, 5.0f });

    SECTION("pushing into the face is a hit at ToI 0 with the face normal")
    {
        const Result r = MovingAABBvsAABB(Mover, { 1.0f, 0.0f }, wall);
        REQUIRE(r.Hit);
        CHECK(r.ToI == 0.0f);
        CHECK(r.Normal == Vec2{ -1.0f, 0.0f });
    }
    SECTION("moving away or sliding along the face is free")
    {
        CHECK_FALSE(MovingAABBvsAABB(Mover, { -1.0f, 0.0f }, wall).Hit);
        CHECK_FALSE(MovingAABBvsAABB(Mover, { 0.0f, 3.0f }, wall).Hit);
    }
    SECTION("float noise from a resolved hit doesn't trap the mover")
    {
        constexpr AABB nudgedIn  = AABB::FromCenterHalfExtents({ 0.00001f, 0.0f }, { 5.0f, 5.0f });
        constexpr AABB nudgedOut = AABB::FromCenterHalfExtents({ -0.00001f, 0.0f }, { 5.0f, 5.0f });
        CHECK_FALSE(MovingAABBvsAABB(nudgedIn, { -1.0f, 0.0f }, wall).Hit);
        CHECK_FALSE(MovingAABBvsAABB(nudgedOut, { 0.0f, -3.0f }, wall).Hit);
        CHECK(MovingAABBvsAABB(nudgedOut, { 1.0f, 0.0f }, wall).Hit);
    }
    SECTION("corner contact blocks only a move into both faces")
    {
        constexpr AABB corner = AABB::FromCenterHalfExtents({ 10.0f, 10.0f }, { 5.0f, 5.0f });
        CHECK_FALSE(MovingAABBvsAABB(Mover, { 1.0f, 0.0f }, corner).Hit);
        CHECK(MovingAABBvsAABB(Mover, { 1.0f, 1.0f }, corner).Hit);
    }
}

TEST_CASE("Substep sweep: circle into box", "[physics][sweep]")
{
    const Collider2D circle{ .Shape = ColliderShape::Circle, .Radius = 4.0f };
    const Collider2D box{ .Shape = ColliderShape::Box, .HalfExtents = { 5.0f, 5.0f } };
    const Result     r = SweepAgainst(circle, { 0.0f, 0.0f }, { 40.0f, 0.0f }, box, { 30.0f, 0.0f });
    REQUIRE(r.Hit);
    CHECK(r.ToI < 1.0f);
    CHECK(r.Normal.X < 0.0f);
}

TEST_CASE("Substep sweep: rotated wall reports its face normal", "[physics][sweep][obb]")
{
    // Box mover moving straight up into the underside of a 30° wall.
    const Collider2D wall{ .Shape = ColliderShape::Box, .Rotation = 30.0f * DegToRad, .HalfExtents = { 100.0f, 4.0f } };
    const OBB        wallBox = OBB::FromCollider(wall, {});
    const Collider2D mover{ .Shape = ColliderShape::Box, .HalfExtents = { 8.0f, 8.0f } };

    const Result r = SweepAgainst(mover, wallBox.ToWorldDir({ 50.0f, 30.0f }), { 0.0f, -40.0f }, wall, {});
    REQUIRE(r.Hit);
    CHECK(r.ToI > 0.0f);
    CHECK(r.ToI < 1.0f);
    CHECK_THAT(r.Normal.X, WithinAbs(wallBox.AxisY.X, Tol));
    CHECK_THAT(r.Normal.Y, WithinAbs(wallBox.AxisY.Y, Tol));
}

TEST_CASE("Substep sweep: adaptive step count catches thin planks", "[physics][sweep][obb]")
{
    // A tiny mover crossing a 2 px-thick 45° plank in one 64 px move (a tile
    // step). Fixed 8 substeps straddle the plank; the adaptive count must not.
    const Collider2D plank{ .Shape = ColliderShape::Box, .Rotation = QuarterPi, .HalfExtents = { 40.0f, 1.0f } };
    const Collider2D speck{ .Shape = ColliderShape::Box, .HalfExtents = { 1.0f, 1.0f } };
    const Vec2       from = { -36.0f, 0.0f };
    const Vec2       step = { 64.0f, 0.0f };

    REQUIRE_FALSE(Substep(speck, from, step, plank, {}, 8u).Hit); // setup: 8 steps tunnel
    CHECK(SweepAgainst(speck, from, step, plank, {}).Hit);
}
