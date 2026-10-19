// Movement controllers (Phase 22H): the input → direction resolver, and the
// MoveIntent path into each movement style, driven through a real Scene.
// InputMovementSystem itself only samples IsPressed (SDL-polled) into
// ResolveInputDirection, so the resolver is tested directly.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/physics/PhysicsCallbacks.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32 Dt       = 1.0f / 60.0f;
constexpr f32 TileSize = 32.0f; // TileMap default

constexpr MoveKeys None{};
constexpr MoveKeys Left{ .Left = true };
constexpr MoveKeys Right{ .Right = true };
constexpr MoveKeys Up{ .Up = true };
constexpr MoveKeys Down{ .Down = true };
constexpr MoveKeys RightUp{ .Right = true, .Up = true };

void Step(Scene& scene, const int ticks = 1)
{
    for (int i = 0; i < ticks; ++i) scene.Update(Dt);
}

Vec2 PositionOf(Scene& scene, const Entity e) { return scene.GetWorld().GetTransform(e)->Position; }

void SetIntent(Scene& scene, const Entity e, const Vec2 dir)
{
    scene.GetWorld().GetComponent<MoveIntent>(e)->Set(dir);
}

// Entity with a registered box collider and a MoveIntent.
Entity AddMover(Scene& scene, const Vec2 pos)
{
    World&           world          = scene.GetWorld();
    const Entity     e              = world.CreateEntity();
    const Collider2D col{ .Shape = ColliderShape::Box, .HalfExtents = { 12.0f, 12.0f } };
    world.GetTransform(e)->Position = pos;
    world.AddComponent<Collider2D>(e, col);
    world.AddComponent<MoveIntent>(e);
    (void)scene.GetPhysics().RegisterCollider(e, col, pos);
    return e;
}

void AddSolidTile(Scene& scene, const i32 x, const i32 y)
{
    TileDef wall{};
    wall.Solid = true;
    scene.GetTileMap().RegisterTile(1, wall);
    scene.GetTileMap().SetTile(x, y, 1, 1);
}
} // namespace

// ---------------------------------------------------------------------------
// ResolveInputDirection
// ---------------------------------------------------------------------------

TEST_CASE("Single keys map to unit directions", "[ecs][movement]")
{
    InputMovement im;
    CHECK(ResolveInputDirection(im, None, false) == Vec2{});
    CHECK(ResolveInputDirection(im, Left, false) == Vec2{ -1.0f, 0.0f });
    CHECK(ResolveInputDirection(im, Right, false) == Vec2{ 1.0f, 0.0f });
    CHECK(ResolveInputDirection(im, Up, false) == Vec2{ 0.0f, -1.0f }); // +Y is down
    CHECK(ResolveInputDirection(im, Down, true) == Vec2{ 0.0f, 1.0f });
}

TEST_CASE("Opposite keys: the most recently pressed one wins", "[ecs][movement]")
{
    constexpr MoveKeys LeftRight{ .Left = true, .Right = true };
    constexpr MoveKeys UpDown{ .Up = true, .Down = true };
    InputMovement      im;
    const bool         cardinal = GENERATE(false, true); // same rule for free and tile movers

    SECTION("Holding right, then pressing left, turns left; releasing left resumes right")
    {
        CHECK(ResolveInputDirection(im, Right, cardinal) == Vec2{ 1.0f, 0.0f });
        CHECK(ResolveInputDirection(im, LeftRight, cardinal) == Vec2{ -1.0f, 0.0f });
        CHECK(ResolveInputDirection(im, LeftRight, cardinal) == Vec2{ -1.0f, 0.0f });
        CHECK(ResolveInputDirection(im, Right, cardinal) == Vec2{ 1.0f, 0.0f });
    }
    SECTION("Holding left, then pressing right, turns right")
    {
        CHECK(ResolveInputDirection(im, Left, cardinal) == Vec2{ -1.0f, 0.0f });
        CHECK(ResolveInputDirection(im, LeftRight, cardinal) == Vec2{ 1.0f, 0.0f });
    }
    SECTION("The vertical pair follows the same rule")
    {
        CHECK(ResolveInputDirection(im, Down, cardinal) == Vec2{ 0.0f, 1.0f });
        CHECK(ResolveInputDirection(im, UpDown, cardinal) == Vec2{ 0.0f, -1.0f });
        CHECK(ResolveInputDirection(im, Down, cardinal) == Vec2{ 0.0f, 1.0f });
    }
    SECTION("Both pressed on the same tick still move")
    {
        CHECK(ResolveInputDirection(im, LeftRight, cardinal) != Vec2{});
    }
}

TEST_CASE("Opposite keys plus a third: each axis resolves on its own", "[ecs][movement]")
{
    InputMovement im;
    CHECK(ResolveInputDirection(im, Right, false) == Vec2{ 1.0f, 0.0f });
    const Vec2 dir = ResolveInputDirection(im, MoveKeys{ .Left = true, .Right = true, .Down = true }, false);
    CHECK_THAT(dir.X, WithinAbs(-0.70710678f, 1e-6f)); // left (pressed last on X) + down
    CHECK_THAT(dir.Y, WithinAbs(0.70710678f, 1e-6f));
}

TEST_CASE("Free movers get a normalized diagonal", "[ecs][movement]")
{
    InputMovement im;
    const Vec2    dir = ResolveInputDirection(im, RightUp, false);
    CHECK_THAT(Length(dir), WithinAbs(1.0f, 1e-6f));
    CHECK_THAT(dir.X, WithinAbs(0.70710678f, 1e-6f));
    CHECK_THAT(dir.Y, WithinAbs(-0.70710678f, 1e-6f));
}

TEST_CASE("Tile movers follow the most recently pressed axis", "[ecs][movement]")
{
    InputMovement im;

    SECTION("Holding right, then pressing up, turns up; releasing up resumes right")
    {
        CHECK(ResolveInputDirection(im, Right, true) == Vec2{ 1.0f, 0.0f });
        CHECK(ResolveInputDirection(im, RightUp, true) == Vec2{ 0.0f, -1.0f });
        CHECK(ResolveInputDirection(im, RightUp, true) == Vec2{ 0.0f, -1.0f }); // still held: stays up
        CHECK(ResolveInputDirection(im, Right, true) == Vec2{ 1.0f, 0.0f });
    }
    SECTION("Holding up, then pressing right, turns right")
    {
        // Regression: the demos' "dominant axis" check always picked X, so
        // this worked but the case above didn't.
        CHECK(ResolveInputDirection(im, Up, true) == Vec2{ 0.0f, -1.0f });
        CHECK(ResolveInputDirection(im, RightUp, true) == Vec2{ 1.0f, 0.0f });
    }
    SECTION("Both axes pressed on the same tick prefer X")
    {
        CHECK(ResolveInputDirection(im, Up, true) == Vec2{ 0.0f, -1.0f });
        CHECK(ResolveInputDirection(im, None, true) == Vec2{});
        CHECK(ResolveInputDirection(im, RightUp, true) == Vec2{ 1.0f, 0.0f });
    }
    SECTION("Re-pressing a key on the held axis takes the preference back")
    {
        CHECK(ResolveInputDirection(im, Right, true) == Vec2{ 1.0f, 0.0f });
        CHECK(ResolveInputDirection(im, RightUp, true) == Vec2{ 0.0f, -1.0f });
        CHECK(ResolveInputDirection(im, MoveKeys{ .Up = true }, true) == Vec2{ 0.0f, -1.0f });
        CHECK(ResolveInputDirection(im, RightUp, true) == Vec2{ 1.0f, 0.0f });
    }
}

// ---------------------------------------------------------------------------
// MoveIntent
// ---------------------------------------------------------------------------

TEST_CASE("MoveIntent::Set keeps the last nonzero direction as Facing", "[ecs][movement]")
{
    MoveIntent intent;
    CHECK(intent.Facing == Vec2{ 0.0f, 1.0f }); // down until told otherwise
    intent.Set({ -1.0f, 0.0f });
    intent.Set({});
    CHECK(intent.Direction == Vec2{});
    CHECK(intent.Facing == Vec2{ -1.0f, 0.0f });
}

TEST_CASE("Without an InputManager, InputMovement leaves intent alone", "[ecs][movement]")
{
    Scene        scene; // no SetInput — the test / tool configuration
    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    world.AddComponent<InputMovement>(e);
    world.AddComponent<MoveIntent>(e);
    SetIntent(scene, e, { 1.0f, 0.0f });

    Step(scene);
    CHECK(world.GetComponent<MoveIntent>(e)->Direction == Vec2{ 1.0f, 0.0f });
}

TEST_CASE("InputMovement adds a MoveIntent and samples released keys as idle", "[ecs][movement][input]")
{
    InputManager input; // never Update()d, so every action reads as released
    Scene        scene;
    scene.SetInput(&input);

    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    world.AddComponent<InputMovement>(e, InputMovement{ MakeAction("L"), MakeAction("R"), MakeAction("U"),
                                                        MakeAction("D") });
    world.AddComponent<FreeMovement>(e, FreeMovement{ .Velocity = { 50.0f, 0.0f } });
    world.AddComponent<PendingMove>(e);

    Step(scene);
    const MoveIntent* intent = world.GetComponent<MoveIntent>(e);
    REQUIRE(intent != nullptr);
    CHECK(intent->Direction == Vec2{});

    Step(scene); // now intent-driven: zero intent holds the mover still
    CHECK(world.GetComponent<FreeMovement>(e)->Velocity == Vec2{});
}

// ---------------------------------------------------------------------------
// Intent into each style
// ---------------------------------------------------------------------------

TEST_CASE("Intent drives a free mover at its Speed and stops it instantly", "[ecs][movement]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = AddMover(scene, {});
    world.AddComponent<FreeMovement>(e, FreeMovement{ .Friction = 8.0f, .Speed = 120.0f });
    world.AddComponent<PendingMove>(e);

    SetIntent(scene, e, { 1.0f, 0.0f });
    Step(scene, 60);
    CHECK_THAT(PositionOf(scene, e).X, WithinAbs(120.0f, 1e-2f)); // friction doesn't fight held input

    SetIntent(scene, e, {});
    Step(scene);
    const f32 stoppedAt = PositionOf(scene, e).X;
    Step(scene, 10);
    CHECK(PositionOf(scene, e).X == stoppedAt); // no friction slide after release
}

TEST_CASE("Intent drives a smooth tile mover along the dominant axis", "[ecs][movement]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = AddMover(scene, {});
    world.AddComponent<SmoothTileMovement>(e, SmoothTileMovement{ .Speed = 4.0f });

    SetIntent(scene, e, { 0.3f, 0.8f }); // e.g. an AI heading — reduced to one tile step down
    Step(scene, 60);
    CHECK_THAT(PositionOf(scene, e).X, WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(PositionOf(scene, e).Y, WithinAbs(TileSize * 4.0f, 1e-2f));

    // The arrival tick already chained into a fifth step; releasing lets that
    // step finish at the next tile center, then the mover stays put.
    SetIntent(scene, e, {});
    Step(scene, 30);
    CHECK_THAT(PositionOf(scene, e).Y, WithinAbs(TileSize * 5.0f, 1e-2f));
    Step(scene, 30);
    CHECK_THAT(PositionOf(scene, e).Y, WithinAbs(TileSize * 5.0f, 1e-2f));
}

TEST_CASE("Snap movers step at once, then repeat every StepInterval while held", "[ecs][movement]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = AddMover(scene, {});
    world.AddComponent<SnapTileMovement>(e, SnapTileMovement{ .StepInterval = 0.15f });

    SetIntent(scene, e, { 1.0f, 0.0f });
    Step(scene);
    CHECK(PositionOf(scene, e) == Vec2{ TileSize, 0.0f }); // first tick, no lerp

    Step(scene, 8);
    CHECK(PositionOf(scene, e).X == TileSize); // 0.15 s = 9 ticks: not yet
    Step(scene);
    CHECK(PositionOf(scene, e).X == TileSize * 2.0f);

    Step(scene, 50); // ticks 10..59: steps on ticks 18, 27, 36, 45, 54
    CHECK(PositionOf(scene, e).X == TileSize * 7.0f);
}

TEST_CASE("Snap movers step on every fresh press", "[ecs][movement]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = AddMover(scene, {});
    world.AddComponent<SnapTileMovement>(e, SnapTileMovement{ .StepInterval = 1.0f });

    for (int i = 0; i < 3; ++i) {
        SetIntent(scene, e, { 0.0f, -1.0f });
        Step(scene);
        SetIntent(scene, e, {});
        Step(scene); // releasing clears the cooldown
    }
    CHECK(PositionOf(scene, e) == Vec2{ 0.0f, -TileSize * 3.0f });
    CHECK(world.GetComponent<MoveIntent>(e)->Facing == Vec2{ 0.0f, -1.0f });
}

TEST_CASE("Snap movers stop at solid tiles", "[ecs][movement]")
{
    Scene scene;
    AddSolidTile(scene, 2, 0);
    const Entity e = AddMover(scene, {});
    scene.GetWorld().AddComponent<SnapTileMovement>(e, SnapTileMovement{ .StepInterval = 0.1f });

    SetIntent(scene, e, { 1.0f, 0.0f });
    Step(scene, 60);
    CHECK(PositionOf(scene, e) == Vec2{ TileSize, 0.0f });

    SetIntent(scene, e, { 0.0f, 1.0f }); // turning away is immediate: no cooldown debt from the wall
    Step(scene);
    CHECK(PositionOf(scene, e) == Vec2{ TileSize, TileSize });
}

TEST_CASE("A snap mover blocked by an entity raises a collision contact", "[ecs][movement][physics]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity mover = AddMover(scene, {});
    world.AddComponent<SnapTileMovement>(mover);

    const Entity     wall = world.CreateEntity();
    const Collider2D box{ .Shape = ColliderShape::Box, .HalfExtents = { 16.0f, 16.0f }, .Kind = BodyKind::Static };
    world.GetTransform(wall)->Position = { TileSize, 0.0f };
    world.AddComponent<Collider2D>(wall, box);
    (void)scene.GetPhysics().RegisterCollider(wall, box, { TileSize, 0.0f });

    int enters = 0;
    world.AddComponent<CollisionCallback>(mover, CollisionCallback{
        .OnEnter = [&](Entity, Entity, Vec2) { ++enters; },
    });

    SetIntent(scene, mover, { 1.0f, 0.0f });
    Step(scene, 20);
    CHECK(PositionOf(scene, mover) == Vec2{});
    CHECK(enters == 1);
}

TEST_CASE("A held entity stands still, keeps its facing, and moves once released", "[ecs][movement]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = AddMover(scene, {});
    world.AddComponent<FreeMovement>(e, FreeMovement{ .Speed = 120.0f });
    world.AddComponent<PendingMove>(e);

    Hold(world, e);
    Hold(world, e); // two holders: a dialog and a cutscene, say
    for (int i = 0; i < 30; ++i) {
        SetIntent(scene, e, { 1.0f, 0.0f }); // a controller or script keeps asking
        Step(scene);
    }
    CHECK(PositionOf(scene, e).X == 0.0f);
    CHECK(world.GetComponent<MoveIntent>(e)->Facing == Vec2{ 1.0f, 0.0f });

    Release(world, e);
    CHECK(IsHeld(world, e)); // one hold left
    SetIntent(scene, e, { 1.0f, 0.0f });
    Step(scene);
    CHECK(PositionOf(scene, e).X == 0.0f);

    Release(world, e);
    CHECK_FALSE(IsHeld(world, e));
    SetIntent(scene, e, { 1.0f, 0.0f });
    Step(scene, 30);
    CHECK(PositionOf(scene, e).X > 0.0f);
    Release(world, e); // releasing what isn't held does nothing
}
