#include <arcbit/physics/DirectionArc.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace Arcbit;

namespace
{
// Cardinal unit vectors (screen coords: +Y is south).
constexpr Vec2 East  = { 1.0f, 0.0f };
constexpr Vec2 West  = { -1.0f, 0.0f };
constexpr Vec2 North = { 0.0f, -1.0f };
constexpr Vec2 South = { 0.0f, 1.0f };

Vec2 FromDegrees(const f32 degrees)
{
    return { std::cos(degrees * DegToRad), std::sin(degrees * DegToRad) };
}
} // namespace

TEST_CASE("Empty and full-coverage arc lists block every direction", "[physics][arcs]")
{
    CHECK(IsContactBlocked({}, North, 0.0f));
    CHECK(IsContactBlocked({}, East, 0.0f));
    CHECK(IsContactBlocked(DirectionArc::AllDirections(), East, 0.0f));
}

TEST_CASE("Two-sided presets block their axis only", "[physics][arcs]")
{
    const auto v = DirectionArc::Vertical();
    CHECK(IsContactBlocked(v, North, 0.0f));
    CHECK(IsContactBlocked(v, South, 0.0f));
    CHECK_FALSE(IsContactBlocked(v, East, 0.0f));
    CHECK_FALSE(IsContactBlocked(v, West, 0.0f));

    const auto h = DirectionArc::Horizontal();
    CHECK(IsContactBlocked(h, East, 0.0f));
    CHECK(IsContactBlocked(h, West, 0.0f));
    CHECK_FALSE(IsContactBlocked(h, North, 0.0f));
    CHECK_FALSE(IsContactBlocked(h, South, 0.0f));
}

TEST_CASE("One-sided presets block their face only", "[physics][arcs]")
{
    const auto wide   = DirectionArc::NorthOnly();      // ±90°
    const auto narrow = DirectionArc::NorthOnly(45.0f); // ±45°
    CHECK(IsContactBlocked(wide, North, 0.0f));
    CHECK_FALSE(IsContactBlocked(wide, South, 0.0f));
    // ±90° puts E/W exactly on the boundary; the strict `<` leaves the
    // boundary outside, so a perpendicular contact passes.
    CHECK_FALSE(IsContactBlocked(wide, East, 0.0f));
    CHECK_FALSE(IsContactBlocked(wide, West, 0.0f));
    CHECK_FALSE(IsContactBlocked(narrow, East, 0.0f));
    CHECK_FALSE(IsContactBlocked(narrow, West, 0.0f));
}

TEST_CASE("Off-axis contacts respect the arc half-width", "[physics][arcs]")
{
    const auto v = DirectionArc::Vertical(); // ±60° around N and S
    CHECK(IsContactBlocked(v, FromDegrees(-60.0f), 0.0f));       // 30° off north
    CHECK_FALSE(IsContactBlocked(v, FromDegrees(-15.0f), 0.0f)); // 75° off north
}

TEST_CASE("Arcs rotate with the obstacle", "[physics][arcs]")
{
    // Rotated 90° clockwise, local "vertical" is world horizontal.
    const auto v = DirectionArc::Vertical();
    CHECK(IsContactBlocked(v, East, HalfPi));
    CHECK_FALSE(IsContactBlocked(v, North, HalfPi));
}

TEST_CASE("An arc centered at 180 degrees wraps to accept -180", "[physics][arcs]")
{
    const std::vector<DirectionArc> west{ { 180.0f, 30.0f } };
    CHECK(IsContactBlocked(west, West, 0.0f));
}
