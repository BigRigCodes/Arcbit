#include "TestSupport.h"

#include <arcbit/render/Camera2D.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr Vec2 Viewport{ 1920.0f, 1080.0f };

void CheckNear(const Vec2 actual, const Vec2 expected, const f32 tol = 1e-3f)
{
    CHECK_THAT(actual.X, WithinAbs(expected.X, tol));
    CHECK_THAT(actual.Y, WithinAbs(expected.Y, tol));
}
} // namespace

TEST_CASE("Camera position maps to the viewport center", "[render][camera]")
{
    Camera2D cam;
    cam.Position = { 300.0f, -50.0f };
    cam.Zoom     = 2.0f;
    CheckNear(cam.WorldToScreen(cam.Position, Viewport), { 960.0f, 540.0f });
    CheckNear(cam.ScreenToWorld({ 960.0f, 540.0f }, Viewport), cam.Position);
}

TEST_CASE("Zoom scales screen distance", "[render][camera]")
{
    Camera2D cam;
    cam.Zoom = 2.0f;
    // 10 world px right of the camera lands 20 screen px right of center.
    CheckNear(cam.WorldToScreen({ 10.0f, 0.0f }, Viewport), { 980.0f, 540.0f });
}

TEST_CASE("ScreenToWorld inverts WorldToScreen at any zoom and rotation", "[render][camera]")
{
    Camera2D cam;
    cam.Position = { 120.0f, 75.0f };
    cam.Zoom     = GENERATE(0.5f, 1.0f, 3.0f);
    cam.Rotation = GENERATE(0.0f, 0.4f, -2.0f);

    const Vec2 world{ -40.0f, 310.0f };
    CheckNear(cam.ScreenToWorld(cam.WorldToScreen(world, Viewport), Viewport), world);
}

TEST_CASE("Follow converges frame-rate independently", "[render][camera]")
{
    Camera2D oneStep;
    Camera2D twoSteps;
    oneStep.Follow({ 100.0f, 0.0f }, 8.0f, 0.1f);
    twoSteps.Follow({ 100.0f, 0.0f }, 8.0f, 0.05f);
    twoSteps.Follow({ 100.0f, 0.0f }, 8.0f, 0.05f);
    CheckNear(oneStep.Position, twoSteps.Position); // exponential decay composes

    for (int i = 0; i < 200; ++i) oneStep.Follow({ 100.0f, 0.0f }, 8.0f, 1.0f / 60.0f);
    CheckNear(oneStep.Position, { 100.0f, 0.0f });
}

TEST_CASE("ClampToBounds keeps the view inside a larger world", "[render][camera]")
{
    Camera2D cam;
    cam.Position = { -500.0f, 5000.0f };
    cam.ClampToBounds({ 0.0f, 0.0f }, { 4000.0f, 4000.0f }, Viewport);
    CheckNear(cam.Position, { 960.0f, 4000.0f - 540.0f });

    SECTION("an axis smaller than the viewport is left alone")
    {
        Camera2D small;
        small.Position = { 77.0f, 1000.0f };
        small.ClampToBounds({ 0.0f, 0.0f }, { 100.0f, 4000.0f }, Viewport); // world narrower than view
        CHECK(small.Position.X == 77.0f);
        CHECK(small.Position.Y == 1000.0f);
    }
}

TEST_CASE("Screen shake decays and never moves the base position", "[render][camera]")
{
    Camera2D cam;
    cam.Position = { 10.0f, 10.0f };
    CHECK(cam.GetEffectivePosition() == cam.Position); // no trauma, no shake

    cam.AddTrauma(5.0f); // clamps to 1
    cam.Update(0.05f);
    CHECK(cam.GetEffectivePosition() != cam.Position);
    CHECK(cam.Position == Vec2{ 10.0f, 10.0f });

    for (int i = 0; i < 120; ++i) cam.Update(1.0f / 60.0f); // 2 s ≫ 1 / decay rate
    CHECK(cam.GetEffectivePosition() == cam.Position);
}

TEST_CASE("Visibility culling", "[render][camera]")
{
    Camera2D cam;
    // Half-view is 960 × 540 at zoom 1.
    CHECK(cam.IsVisible({ 0.0f, 0.0f }, { 10.0f, 10.0f }, Viewport));
    CHECK(cam.IsVisible({ 964.0f, 0.0f }, { 10.0f, 10.0f }, Viewport));        // straddles the edge
    CHECK_FALSE(cam.IsVisible({ 980.0f, 0.0f }, { 10.0f, 10.0f }, Viewport));

    cam.Zoom = 2.0f; // half-view shrinks to 480 × 270
    CHECK_FALSE(cam.IsVisible({ 600.0f, 0.0f }, { 10.0f, 10.0f }, Viewport));

    cam.Zoom = 1.0f;
    CHECK(cam.IsLightVisible({ 1000.0f, 0.0f }, 50.0f, Viewport));       // 40 px outside, radius 50
    CHECK_FALSE(cam.IsLightVisible({ 1100.0f, 0.0f }, 50.0f, Viewport));
}

TEST_CASE("Rotation widens the culling bounds instead of culling visible sprites", "[render][camera]")
{
    Camera2D cam;
    cam.Rotation = HalfPi; // the 1920 × 1080 view now spans 1080 wide, 1920 tall in world space
    CHECK(cam.IsVisible({ 0.0f, 900.0f }, { 2.0f, 2.0f }, Viewport));
    CHECK_FALSE(cam.IsVisible({ 900.0f, 0.0f }, { 2.0f, 2.0f }, Viewport));

    // At 45° the test uses the rotated view's bounding box: (1000, 0) lies
    // outside the true view but inside its AABB, so it is kept — culling may
    // over-include, never drop a visible sprite.
    cam.Rotation = QuarterPi;
    CHECK(cam.IsVisible({ 1000.0f, 0.0f }, { 2.0f, 2.0f }, Viewport));
}
