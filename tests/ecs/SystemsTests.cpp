// Integration tests for the built-in ECS systems, driven through a real Scene
// the same way Application does: set component state, call Scene::Update.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/physics/PhysicsCallbacks.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32 Dt       = 1.0f / 60.0f;
constexpr f32 TileSize = 32.0f; // TileMap default

void Step(Scene& scene, const int ticks = 1)
{
    for (int i = 0; i < ticks; ++i) scene.Update(Dt);
}

Vec2 PositionOf(Scene& scene, const Entity e) { return scene.GetWorld().GetTransform(e)->Position; }

// Entity at `pos` with a registered collider.
Entity AddBody(Scene& scene, const Vec2 pos, const Collider2D& col)
{
    World&       world          = scene.GetWorld();
    const Entity e              = world.CreateEntity();
    world.GetTransform(e)->Position = pos;
    world.AddComponent<Collider2D>(e, col);
    (void)scene.GetPhysics().RegisterCollider(e, col, pos);
    return e;
}

Collider2D Box(const f32 half, const BodyKind kind = BodyKind::Kinematic)
{
    return Collider2D{ .Shape = ColliderShape::Box, .HalfExtents = { half, half }, .Kind = kind };
}

// Kinematic box driven by FreeMovement (frictionless, so velocity holds).
Entity AddFreeMover(Scene& scene, const Vec2 pos, const f32 half = 8.0f)
{
    const Entity e = AddBody(scene, pos, Box(half));
    scene.GetWorld().AddComponent<FreeMovement>(e, FreeMovement{ .Friction = 0.0f });
    scene.GetWorld().AddComponent<PendingMove>(e);
    return e;
}

void SetVelocity(Scene& scene, const Entity e, const Vec2 v)
{
    scene.GetWorld().GetComponent<FreeMovement>(e)->Velocity = v;
}
} // namespace

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

TEST_CASE("FreeMovement integrates velocity, clamps MaxSpeed, and applies friction", "[ecs][systems]")
{
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    world.AddComponent<FreeMovement>(e, FreeMovement{ .Velocity = { 120.0f, 0.0f }, .Friction = 0.0f });
    world.AddComponent<PendingMove>(e); // no collider: the resolver applies the delta directly

    Step(scene, 60);
    CHECK_THAT(PositionOf(scene, e).X, WithinAbs(120.0f, 1e-2f)); // one second at 120 px/s

    auto* fm     = world.GetComponent<FreeMovement>(e);
    fm->Velocity = { 1000.0f, 0.0f };
    fm->MaxSpeed = 100.0f;
    Step(scene);
    CHECK_THAT(fm->Velocity.X, WithinAbs(100.0f, 1e-3f));

    fm->MaxSpeed = 0.0f;
    fm->Friction = 8.0f;
    fm->Velocity = { 100.0f, 0.0f };
    Step(scene);
    CHECK_THAT(fm->Velocity.X, WithinAbs(100.0f * std::exp(-8.0f * Dt), 1e-3f));
}

TEST_CASE("SmoothTileMovement walks at a constant rate across tile boundaries", "[ecs][systems]")
{
    // Regression: arriving on a tile used to spend one tick planning without
    // moving, so walking stalled once per tile.
    Scene        scene;
    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    world.AddComponent<SmoothTileMovement>(e, SmoothTileMovement{ .Speed = 4.0f });

    std::vector<f32> steps;
    for (int i = 0; i < 60; ++i) {
        auto* stm      = world.GetComponent<SmoothTileMovement>(e);
        stm->QueuedDir = { 1.0f, 0.0f };
        stm->HasQueued = true;
        const f32 before = PositionOf(scene, e).X;
        Step(scene);
        steps.push_back(PositionOf(scene, e).X - before);
    }
    for (const f32 dx : steps) CHECK_THAT(dx, WithinAbs(TileSize * 4.0f * Dt, 1e-3f));
    CHECK_THAT(PositionOf(scene, e).X, WithinAbs(TileSize * 4.0f, 1e-2f)); // 4 tiles in one second
}

TEST_CASE("SmoothTileMovement stops at solid tiles", "[ecs][systems]")
{
    Scene scene;
    TileDef wall{};
    wall.Solid = true;
    scene.GetTileMap().RegisterTile(1, wall);
    scene.GetTileMap().SetTile(1, 0, 1, 1); // solid tile directly east of the origin

    const Entity e = AddBody(scene, { 0.0f, 0.0f }, Box(12.0f));
    scene.GetWorld().AddComponent<SmoothTileMovement>(e, SmoothTileMovement{ .Speed = 4.0f });

    auto queue = [&](const Vec2 dir) {
        auto* stm      = scene.GetWorld().GetComponent<SmoothTileMovement>(e);
        stm->QueuedDir = dir;
        stm->HasQueued = true;
    };

    for (int i = 0; i < 30; ++i) {
        queue({ 1.0f, 0.0f });
        Step(scene);
    }
    CHECK(PositionOf(scene, e) == Vec2{ 0.0f, 0.0f });

    for (int i = 0; i < 15; ++i) {
        queue({ 0.0f, 1.0f });
        Step(scene);
    }
    CHECK_THAT(PositionOf(scene, e).Y, WithinAbs(TileSize, 1e-2f));
}

// ---------------------------------------------------------------------------
// Collision resolution
// ---------------------------------------------------------------------------

TEST_CASE("A box mover stops at a box wall, then can leave or slide along it", "[ecs][systems][physics]")
{
    // Regression: two touching axis-aligned boxes used to report a hit for
    // every direction, trapping the mover against the wall.
    Scene scene;
    (void)AddBody(scene, { 100.0f, 0.0f }, Box(20.0f, BodyKind::Static));
    const Entity mover = AddFreeMover(scene, { 0.0f, 0.0f });

    SetVelocity(scene, mover, { 240.0f, 0.0f });
    Step(scene, 60);
    CHECK_THAT(PositionOf(scene, mover).X, WithinAbs(100.0f - 28.0f, 1e-3f)); // flush against the wall face

    SetVelocity(scene, mover, { 0.0f, 120.0f }); // slide along the face
    Step(scene, 30);
    CHECK_THAT(PositionOf(scene, mover).Y, WithinAbs(60.0f, 1e-2f));

    SetVelocity(scene, mover, { -120.0f, 0.0f }); // back away
    Step(scene, 30);
    CHECK_THAT(PositionOf(scene, mover).X, WithinAbs(72.0f - 60.0f, 1e-2f));
}

TEST_CASE("Diagonal motion into a wall slides along it", "[ecs][systems][physics]")
{
    Scene scene;
    (void)AddBody(scene, { 100.0f, 0.0f }, Collider2D{ .Shape = ColliderShape::Box, .HalfExtents = { 20.0f, 500.0f },
                                                       .Kind = BodyKind::Static });
    const Entity mover = AddFreeMover(scene, { 70.0f, 0.0f }); // 2 px from the wall face at x = 80

    SetVelocity(scene, mover, { 120.0f, 120.0f });
    Step(scene, 30);
    CHECK_THAT(PositionOf(scene, mover).X, WithinAbs(72.0f, 1e-3f)); // pinned to the face
    CHECK(PositionOf(scene, mover).Y > 55.0f);                       // kept moving along it
}

// ---------------------------------------------------------------------------
// Contact events
// ---------------------------------------------------------------------------

namespace
{
struct EventLog
{
    int Enter = 0, Stay = 0, Exit = 0;
};

TriggerCallback Recording(EventLog& log)
{
    return TriggerCallback{
        .OnEnter = [&log](Entity, Entity) { ++log.Enter; },
        .OnStay  = [&log](Entity, Entity) { ++log.Stay; },
        .OnExit  = [&log](Entity, Entity) { ++log.Exit; },
    };
}

Entity AddTriggerZone(Scene& scene, const Vec2 pos, const f32 half)
{
    return AddBody(scene, pos, Collider2D{ .Shape = ColliderShape::Box, .HalfExtents = { half, half },
                                           .Kind = BodyKind::Static, .IsTrigger = true });
}
} // namespace

TEST_CASE("Triggers report Enter, Stay, and Exit to both sides", "[ecs][systems][contacts]")
{
    Scene        scene;
    const Entity zone  = AddTriggerZone(scene, { 100.0f, 0.0f }, 20.0f);
    const Entity mover = AddFreeMover(scene, { 0.0f, 0.0f });

    EventLog zoneLog, moverLog;
    scene.GetWorld().AddComponent<TriggerCallback>(zone, Recording(zoneLog));
    scene.GetWorld().AddComponent<TriggerCallback>(mover, Recording(moverLog));

    Entity seenSelf, seenOther;
    scene.GetWorld().GetComponent<TriggerCallback>(zone)->OnEnter = [&](const Entity self, const Entity other) {
        ++zoneLog.Enter;
        seenSelf  = self;
        seenOther = other;
    };

    SetVelocity(scene, mover, { 120.0f, 0.0f });
    Step(scene, 120); // crosses the zone completely and leaves

    CHECK(zoneLog.Enter == 1);
    CHECK(zoneLog.Exit == 1);
    CHECK(zoneLog.Stay > 0);
    CHECK(moverLog.Enter == 1); // the entrant hears about it too
    CHECK(moverLog.Exit == 1);
    CHECK(seenSelf == zone);
    CHECK(seenOther == mover);
}

TEST_CASE("Triggers never block movement", "[ecs][systems][contacts]")
{
    Scene scene;
    (void)AddTriggerZone(scene, { 50.0f, 0.0f }, 20.0f);
    const Entity mover = AddFreeMover(scene, { 0.0f, 0.0f });
    SetVelocity(scene, mover, { 120.0f, 0.0f });
    Step(scene, 60);
    CHECK_THAT(PositionOf(scene, mover).X, WithinAbs(120.0f, 1e-2f));
}

TEST_CASE("Disabling an entity inside a trigger fires Exit", "[ecs][systems][contacts]")
{
    Scene        scene;
    const Entity zone   = AddTriggerZone(scene, { 0.0f, 0.0f }, 20.0f);
    const Entity inside = AddBody(scene, { 0.0f, 0.0f }, Box(4.0f));
    EventLog     log;
    scene.GetWorld().AddComponent<TriggerCallback>(zone, Recording(log));

    Step(scene);
    REQUIRE(log.Enter == 1);
    scene.GetWorld().AddComponent<Disabled>(inside);
    Step(scene);
    CHECK(log.Exit == 1);
}

TEST_CASE("Trigger callbacks may destroy entities safely", "[ecs][systems][contacts]")
{
    Scene        scene;
    const Entity zone  = AddTriggerZone(scene, { 0.0f, 0.0f }, 20.0f);
    const Entity first = AddBody(scene, { 5.0f, 0.0f }, Box(4.0f));
    (void)AddBody(scene, { -5.0f, 0.0f }, Box(4.0f));

    int entered = 0;
    scene.GetWorld().AddComponent<TriggerCallback>(zone, TriggerCallback{
        .OnEnter = [&](Entity, const Entity other) {
            ++entered;
            if (other == first) {
                scene.GetPhysics().UnregisterCollider(scene.GetPhysics().FindColliderForEntity(other));
                scene.GetWorld().DestroyEntity(other);
            }
        },
    });

    Step(scene, 3);
    CHECK(entered == 2);
    CHECK_FALSE(scene.GetWorld().IsValid(first));
}

TEST_CASE("Blocking contacts report Enter with a normal, persist while touching, and Exit", "[ecs][systems][contacts]")
{
    Scene        scene;
    const Entity wall  = AddBody(scene, { 100.0f, 0.0f }, Box(20.0f, BodyKind::Static));
    const Entity mover = AddFreeMover(scene, { 0.0f, 0.0f });

    int  enters = 0, exits = 0;
    Vec2 wallNormal;
    scene.GetWorld().AddComponent<CollisionCallback>(wall, CollisionCallback{
        .OnEnter = [&](Entity, Entity, const Vec2 n) { ++enters; wallNormal = n; },
        .OnExit  = [&](Entity, Entity) { ++exits; },
    });

    SetVelocity(scene, mover, { 240.0f, 0.0f });
    Step(scene, 30);
    CHECK(enters == 1);
    CHECK(wallNormal == Vec2{ 1.0f, 0.0f }); // from the mover toward the wall (self)

    SetVelocity(scene, mover, { 0.0f, 0.0f });
    Step(scene, 10); // resting against it — still touching
    CHECK(exits == 0);

    SetVelocity(scene, mover, { -240.0f, 0.0f });
    Step(scene, 10);
    CHECK(exits == 1);
    CHECK(enters == 1);
}

// ---------------------------------------------------------------------------
// Scene cleanup
// ---------------------------------------------------------------------------

TEST_CASE("Every way a collider leaves drops its broadphase record", "[ecs][systems][physics]")
{
    // Regression: only LifetimeSystem unregistered colliders, so an entity
    // destroyed any other way kept blocking movers as an invisible wall.
    Scene         scene;
    PhysicsWorld& physics = scene.GetPhysics();

    SECTION("DestroyEntity")
    {
        const Entity e = AddBody(scene, {}, Box(8.0f));
        REQUIRE(physics.FindColliderForEntity(e) != PhysicsWorld::InvalidId);
        scene.GetWorld().DestroyEntity(e);
        CHECK(physics.FindColliderForEntity(e) == PhysicsWorld::InvalidId);
    }
    SECTION("RemoveComponent<Collider2D>")
    {
        const Entity e = AddBody(scene, {}, Box(8.0f));
        scene.GetWorld().RemoveComponent<Collider2D>(e);
        CHECK(physics.FindColliderForEntity(e) == PhysicsWorld::InvalidId);
        CHECK(scene.GetWorld().IsValid(e));
    }
    SECTION("a mover passes where a destroyed wall stood")
    {
        const Entity wall  = AddBody(scene, { 32.0f, 0.0f }, Box(8.0f, BodyKind::Static));
        const Entity mover = AddFreeMover(scene, {});
        scene.GetWorld().DestroyEntity(wall);
        SetVelocity(scene, mover, { 240.0f, 0.0f });
        Step(scene, 30);
        CHECK(PositionOf(scene, mover).X > 64.0f);
    }
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

TEST_CASE("Expired entities are destroyed and leave the broadphase", "[ecs][systems]")
{
    Scene        scene;
    const Entity e = AddBody(scene, { 0.0f, 0.0f }, Box(4.0f));
    scene.GetWorld().AddComponent<Lifetime>(e, Lifetime{ .Remaining = 0.05f });
    const usize before = scene.GetPhysics().ColliderCount();

    Step(scene, 2);
    CHECK(scene.GetWorld().IsValid(e));
    Step(scene, 2);
    CHECK_FALSE(scene.GetWorld().IsValid(e));
    CHECK(scene.GetPhysics().ColliderCount() == before - 1);
    CHECK(scene.GetPhysics().FindColliderForEntity(e) == PhysicsWorld::InvalidId);
}
