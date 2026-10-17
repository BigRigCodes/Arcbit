// Physics responses (Phase 22I): external velocity on FreeMovement, push-out
// of colliders that moved into a mover, and Pushable objects. Driven through
// a real Scene the way Application does.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/physics/DirectionArc.h>
#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32 Dt = 1.0f / 60.0f;

void Step(Scene& scene, const int ticks = 1)
{
    for (int i = 0; i < ticks; ++i) scene.Update(Dt);
}

Collider2D Box(const f32 halfX, const f32 halfY, const BodyKind kind = BodyKind::Kinematic)
{
    return Collider2D{ .Shape = ColliderShape::Box, .HalfExtents = { halfX, halfY }, .Kind = kind };
}

Entity AddBody(Scene& scene, const Vec2 pos, const Collider2D& col)
{
    World&       world              = scene.GetWorld();
    const Entity e                  = world.CreateEntity();
    world.GetTransform(e)->Position = pos;
    world.AddComponent<Collider2D>(e, col);
    (void)scene.GetPhysics().RegisterCollider(e, col, pos);
    return e;
}

Entity AddFreeMover(Scene& scene, const Vec2 pos, const f32 half = 8.0f)
{
    const Entity e = AddBody(scene, pos, Box(half, half));
    scene.GetWorld().AddComponent<FreeMovement>(e, FreeMovement{ .Friction = 0.0f });
    scene.GetWorld().AddComponent<PendingMove>(e);
    return e;
}

Vec2 PositionOf(Scene& scene, const Entity e) { return scene.GetWorld().GetTransform(e)->Position; }

FreeMovement& Movement(Scene& scene, const Entity e) { return *scene.GetWorld().GetComponent<FreeMovement>(e); }

// Move a body to `pos` the way game code teleports or animates a prop.
void Place(Scene& scene, const Entity e, const Vec2 pos)
{
    scene.GetWorld().GetTransform(e)->Position = pos;
    PhysicsWorld& physics                      = scene.GetPhysics();
    physics.UpdateCollider(physics.FindColliderForEntity(e), *scene.GetWorld().GetComponent<Collider2D>(e), pos);
}

f32 Depth(Scene& scene, const Entity a, const Entity b)
{
    World&                     w = scene.GetWorld();
    const Narrowphase::Contact c = Narrowphase::ComputeContact(*w.GetComponent<Collider2D>(a), PositionOf(scene, a),
                                                               *w.GetComponent<Collider2D>(b), PositionOf(scene, b));
    return c.Hit ? c.Depth : 0.0f;
}
} // namespace

// ---------------------------------------------------------------------------
// External velocity
// ---------------------------------------------------------------------------

TEST_CASE("An impulse moves an intent-driven mover and dies out with friction", "[ecs][physics][22i]")
{
    // Regression for the gap 22I closes: an intent-driven mover's Velocity is
    // rewritten every tick, so an impulse written to Velocity vanished.
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    world.AddComponent<FreeMovement>(e, FreeMovement{ .Friction = 8.0f, .Speed = 120.0f });
    world.AddComponent<PendingMove>(e);
    world.AddComponent<MoveIntent>(e);
    world.GetComponent<MoveIntent>(e)->Set({ 0.0f, 1.0f });

    Movement(scene, e).AddImpulse({ 400.0f, 0.0f });
    Step(scene, 60);
    CHECK_THAT(PositionOf(scene, e).Y, WithinAbs(120.0f, 0.01f)); // intent unaffected
    CHECK(PositionOf(scene, e).X > 45.0f);                       // about 400 / Friction
    CHECK(PositionOf(scene, e).X < 56.0f);

    Step(scene, 60);
    CHECK(Movement(scene, e).External == Vec2{}); // died out and snapped to rest
}

TEST_CASE("MaxSpeed clamps Velocity but not an external push", "[ecs][physics][22i]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    world.AddComponent<FreeMovement>(e, FreeMovement{ .Friction = 0.0f, .MaxSpeed = 50.0f });
    world.AddComponent<PendingMove>(e);

    Movement(scene, e).AddImpulse({ 300.0f, 0.0f });
    Step(scene, 30);
    CHECK_THAT(PositionOf(scene, e).X, WithinAbs(150.0f, 0.01f)); // 0.5 s at 300 px/s, no friction
}

// ---------------------------------------------------------------------------
// Push-out
// ---------------------------------------------------------------------------

TEST_CASE("A collider placed onto a free mover pushes it out", "[ecs][physics][22i]")
{
    Scene        scene;
    const Entity mover = AddFreeMover(scene, {});
    const Entity prop  = AddBody(scene, { 100.0f, 0.0f }, Box(16.0f, 16.0f, BodyKind::Static));
    Place(scene, prop, { 20.0f, 2.0f }); // now 4 px deep on X

    Step(scene);
    CHECK(Depth(scene, mover, prop) <= 1e-3f);
    CHECK_THAT(PositionOf(scene, mover).X, WithinAbs(-4.0f, 0.01f)); // out along the shallow axis
    CHECK_THAT(PositionOf(scene, mover).Y, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("Push-out never shoves a mover through a wall", "[ecs][physics][22i]")
{
    Scene        scene;
    const Entity mover = AddFreeMover(scene, {});
    const Entity wall  = AddBody(scene, { -18.0f, 0.0f }, Box(8.0f, 32.0f, BodyKind::Static)); // 2 px gap on the left
    const Entity prop  = AddBody(scene, { 100.0f, 0.0f }, Box(16.0f, 16.0f, BodyKind::Static));
    Place(scene, prop, { 20.0f, 0.0f }); // 4 px deep, pushes left toward the wall

    Step(scene, 3);
    CHECK(PositionOf(scene, mover).X >= -2.0f - 1e-3f); // stopped at the wall
    CHECK(Depth(scene, mover, wall) <= 1e-3f);
}

TEST_CASE("A rotating box sweeps a mover aside instead of swallowing it", "[ecs][physics][22i]")
{
    // ObbDemo's gate used to stall whenever it would touch the player; with
    // push-out it can keep turning.
    Scene        scene;
    Collider2D   gateCol = Box(64.0f, 8.0f, BodyKind::Static);
    const Entity gate    = AddBody(scene, {}, gateCol);
    const Entity mover   = AddFreeMover(scene, { 40.0f, 17.0f }); // resting just below the gate's right arm

    PhysicsWorld& physics = scene.GetPhysics();
    for (int i = 0; i < 30; ++i) {
        gateCol.Rotation += 0.02f; // clockwise on screen: the right arm swings down into the mover
        *scene.GetWorld().GetComponent<Collider2D>(gate) = gateCol;
        physics.UpdateCollider(physics.FindColliderForEntity(gate), gateCol, {});
        Step(scene);
        INFO("tick " << i);
        CHECK(Depth(scene, mover, gate) <= 1e-3f);
    }
    CHECK(PositionOf(scene, mover).Y > 17.0f); // it was pushed, not just left in place
}

TEST_CASE("Leaving a directional collider ends the overlap it allowed", "[ecs][physics][22i]")
{
    // Walking in through a pass-through face is the mover's own doing and
    // isn't pushed; once it has walked out, the collider moving onto it is.
    Scene            scene;
    const Collider2D gateCol{ .Shape = ColliderShape::Box, .HalfExtents = { 32.0f, 8.0f }, .Kind = BodyKind::Static,
                              .BlockedFrom = DirectionArc::NorthOnly() };
    const Entity     gate  = AddBody(scene, {}, gateCol);
    const Entity     mover = AddFreeMover(scene, { 0.0f, 40.0f });

    Movement(scene, mover).Velocity = { 0.0f, -240.0f };
    Step(scene, 10); // y 40 → 0: inside the gate
    CHECK_THAT(PositionOf(scene, mover).Y, WithinAbs(0.0f, 0.01f));
    Movement(scene, mover).Velocity = {};
    Step(scene, 5);
    CHECK_THAT(PositionOf(scene, mover).Y, WithinAbs(0.0f, 0.01f)); // left alone while inside

    Movement(scene, mover).Velocity = { 0.0f, -240.0f };
    Step(scene, 10); // y 0 → -40: out through the north face
    Movement(scene, mover).Velocity = {};
    Place(scene, gate, { 0.0f, -36.0f }); // the gate moves onto it
    Step(scene);
    CHECK(Depth(scene, mover, gate) <= 1e-3f);
}

// ---------------------------------------------------------------------------
// Pushable
// ---------------------------------------------------------------------------

TEST_CASE("A free mover shoves a Pushable at a share of its speed set by resistance", "[ecs][physics][22i]")
{
    Scene        scene;
    const Entity mover = AddFreeMover(scene, {});
    const Entity crate = AddBody(scene, { 24.0f, 0.0f }, Box(8.0f, 8.0f)); // 8 px gap
    scene.GetWorld().AddComponent<Pushable>(crate, Pushable{ .Resistance = 0.5f });

    Movement(scene, mover).Velocity = { 240.0f, 0.0f };
    Step(scene, 2); // closes the gap
    const f32 crateStart = PositionOf(scene, crate).X;
    Step(scene, 30);
    CHECK_THAT(PositionOf(scene, crate).X - crateStart, WithinAbs(60.0f, 2.0f)); // half of 240 px/s for 0.5 s
    CHECK_THAT(PositionOf(scene, crate).X - PositionOf(scene, mover).X, WithinAbs(16.0f, 0.01f)); // still touching
}

TEST_CASE("A shoved Pushable stops at walls and a resistance of 1 doesn't move", "[ecs][physics][22i]")
{
    SECTION("stops at a wall, and so does the pusher")
    {
        Scene        scene;
        const Entity mover = AddFreeMover(scene, {});
        const Entity crate = AddBody(scene, { 16.0f, 0.0f }, Box(8.0f, 8.0f));
        scene.GetWorld().AddComponent<Pushable>(crate, Pushable{ .Resistance = 0.0f });
        (void)AddBody(scene, { 64.0f, 0.0f }, Box(8.0f, 32.0f, BodyKind::Static)); // wall face at x = 56

        Movement(scene, mover).Velocity = { 240.0f, 0.0f };
        Step(scene, 60);
        CHECK_THAT(PositionOf(scene, crate).X, WithinAbs(48.0f, 0.01f));
        CHECK_THAT(PositionOf(scene, mover).X, WithinAbs(32.0f, 0.01f));
    }
    SECTION("resistance 1")
    {
        Scene        scene;
        const Entity mover = AddFreeMover(scene, {});
        const Entity crate = AddBody(scene, { 16.0f, 0.0f }, Box(8.0f, 8.0f));
        scene.GetWorld().AddComponent<Pushable>(crate, Pushable{ .Resistance = 1.0f });

        Movement(scene, mover).Velocity = { 240.0f, 0.0f };
        Step(scene, 30);
        CHECK(PositionOf(scene, crate).X == 16.0f);
        CHECK(PositionOf(scene, mover).X == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Tile pushing (Sokoban-style)
// ---------------------------------------------------------------------------

namespace
{
constexpr f32 Ts = 32.0f; // TileMap default

Entity AddTileMover(Scene& scene, const Vec2 pos)
{
    const Entity e = AddBody(scene, pos, Box(12.0f, 12.0f));
    scene.GetWorld().AddComponent<MoveIntent>(e);
    scene.GetWorld().GetComponent<MoveIntent>(e)->Set({ 1.0f, 0.0f });
    return e;
}

Entity AddCrate(Scene& scene, const Vec2 pos, const f32 resistance = 0.0f)
{
    const Entity e = AddBody(scene, pos, Box(12.0f, 12.0f));
    scene.GetWorld().AddComponent<Pushable>(e, Pushable{ .Resistance = resistance });
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

TEST_CASE("A snap mover pushes a crate one tile per step until a wall", "[ecs][physics][22i]")
{
    Scene        scene;
    const Entity mover = AddTileMover(scene, {});
    scene.GetWorld().AddComponent<SnapTileMovement>(mover, SnapTileMovement{ .StepInterval = 0.1f });
    const Entity crate = AddCrate(scene, { Ts, 0.0f });
    AddSolidTile(scene, 4, 0);

    Step(scene);
    CHECK(PositionOf(scene, mover) == Vec2{ Ts, 0.0f });
    CHECK(PositionOf(scene, crate) == Vec2{ Ts * 2.0f, 0.0f });

    Step(scene, 60); // crate reaches tile 3, against the wall at tile 4
    CHECK(PositionOf(scene, crate) == Vec2{ Ts * 3.0f, 0.0f });
    CHECK(PositionOf(scene, mover) == Vec2{ Ts * 2.0f, 0.0f });
}

TEST_CASE("A smooth mover carries a pushed crate in lockstep without stalling", "[ecs][physics][22i]")
{
    // A crate that moved on its own could lag the mover by a tick and still
    // be mid-step when the mover arrived, stalling a held push at every tile.
    Scene        scene;
    const Entity mover = AddTileMover(scene, {});
    scene.GetWorld().AddComponent<SmoothTileMovement>(mover, SmoothTileMovement{ .Speed = 4.0f });
    const Entity crate = AddCrate(scene, { Ts, 0.0f });

    for (int i = 0; i < 60; ++i) {
        const f32 before = PositionOf(scene, mover).X;
        Step(scene);
        INFO("tick " << i);
        CHECK_THAT(PositionOf(scene, mover).X - before, WithinAbs(Ts * 4.0f / 60.0f, 1e-3f));
        CHECK_THAT(PositionOf(scene, crate).X - PositionOf(scene, mover).X, WithinAbs(Ts, 1e-3f));
    }
    CHECK_THAT(PositionOf(scene, crate).X, WithinAbs(Ts * 5.0f, 1e-2f)); // 4 tiles pushed in one second
}

TEST_CASE("Tile pushes don't chain, and a resistance of 1 can't be pushed", "[ecs][physics][22i]")
{
    SECTION("two crates in a row")
    {
        Scene        scene;
        const Entity mover = AddTileMover(scene, {});
        scene.GetWorld().AddComponent<SnapTileMovement>(mover);
        (void)AddCrate(scene, { Ts, 0.0f });
        (void)AddCrate(scene, { Ts * 2.0f, 0.0f });
        Step(scene, 10);
        CHECK(PositionOf(scene, mover) == Vec2{});
    }
    SECTION("resistance 1")
    {
        Scene        scene;
        const Entity mover = AddTileMover(scene, {});
        scene.GetWorld().AddComponent<SnapTileMovement>(mover);
        const Entity crate = AddCrate(scene, { Ts, 0.0f }, 1.0f);
        Step(scene, 10);
        CHECK(PositionOf(scene, mover) == Vec2{});
        CHECK(PositionOf(scene, crate) == Vec2{ Ts, 0.0f });
    }
}
