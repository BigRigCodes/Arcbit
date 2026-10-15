#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/tilemap/TileMap.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32  Tol          = 1e-3f;
constexpr u32  ObjectsLayer = 1;
constexpr AABB Everywhere   = AABB::FromMinMax({ -1000.0f, -1000.0f }, { 1000.0f, 1000.0f });

AABB Around(const Vec2 center) { return AABB::FromCenterHalfExtents(center, { 8.0f, 8.0f }); }

// 32 px tiles, with tile id 1 registered as a plain solid wall.
void SetupWallTiles(TileMap& tilemap)
{
    tilemap.SetTileSize(32.0f);
    TileDef wall{};
    wall.Solid = true;
    tilemap.RegisterTile(1, wall);
}
} // namespace

TEST_CASE("PhysicsWorld register / query / update / unregister", "[physics][world]")
{
    PhysicsWorld     world(32.0f);
    const Collider2D rockShape{ .Shape = ColliderShape::Box, .HalfExtents = { 6.0f, 6.0f }, .Kind = BodyKind::Static };

    const PhysicsWorld::ColliderId rock = world.RegisterCollider(Entity{ .Index = 1 }, rockShape, { 16.0f, 16.0f });
    (void)world.RegisterCollider(Entity{ .Index = 2 }, rockShape, { 200.0f, 16.0f });
    REQUIRE(world.ColliderCount() == 2);

    std::vector<PhysicsWorld::ColliderId> result;
    world.QueryAABB(Around({ 16.0f, 16.0f }), result);
    CHECK(result == std::vector{ rock });

    SECTION("update moves the collider between cells")
    {
        world.UpdateCollider(rock, rockShape, { 1000.0f, 1000.0f });
        world.QueryAABB(Around({ 16.0f, 16.0f }), result);
        CHECK(result.empty());
        world.QueryAABB(Around({ 1000.0f, 1000.0f }), result);
        CHECK(result == std::vector{ rock });
    }

    SECTION("unregister frees the slot for reuse")
    {
        world.UnregisterCollider(rock);
        CHECK(world.ColliderCount() == 1);
        world.QueryAABB(Around({ 16.0f, 16.0f }), result);
        CHECK(result.empty());
        CHECK(world.RegisterCollider(Entity{ .Index = 3 }, rockShape, { 50.0f, 50.0f }) == rock);
    }
}

TEST_CASE("ComputeWorldAABB for circles and rotated boxes", "[physics][world]")
{
    const AABB circle = PhysicsWorld::ComputeWorldAABB(Collider2D{ .Shape = ColliderShape::Circle, .Radius = 10.0f }, {});
    CHECK(circle.Min.X == -10.0f);
    CHECK(circle.Max.X == 10.0f);

    // 90° rotation swaps the half-extents.
    const AABB rotated = PhysicsWorld::ComputeWorldAABB(
        Collider2D{ .Shape = ColliderShape::Box, .Rotation = HalfPi, .HalfExtents = { 4.0f, 16.0f } }, {});
    CHECK_THAT(rotated.HalfExtents().X, WithinAbs(16.0f, Tol));
    CHECK_THAT(rotated.HalfExtents().Y, WithinAbs(4.0f, Tol));
}

TEST_CASE("Tile colliders are greedy-meshed per chunk", "[physics][world][tiles]")
{
    TileMap tilemap;
    SetupWallTiles(tilemap);
    PhysicsWorld pw(32.0f);
    pw.SetTileMap(&tilemap);
    std::vector<TileColliderRect> rects;

    pw.QueryTileColliders(Everywhere, rects);
    CHECK(rects.empty());

    SECTION("a row of tiles merges into one rect; disjoint shapes stay separate")
    {
        for (i32 x = 0; x <= 2; ++x) tilemap.SetTile(x, 0, ObjectsLayer, 1);
        pw.QueryTileColliders(Everywhere, rects);
        REQUIRE(rects.size() == 1);
        CHECK_THAT(rects[0].WorldAABB.Size().X, WithinAbs(96.0f, Tol));
        CHECK_THAT(rects[0].WorldAABB.Size().Y, WithinAbs(32.0f, Tol));

        for (i32 y = 0; y <= 1; ++y)
            for (i32 x = 5; x <= 6; ++x) tilemap.SetTile(x, y, ObjectsLayer, 1);
        pw.QueryTileColliders(Everywhere, rects);
        REQUIRE(rects.size() == 2);
        f32 area = 0.0f;
        for (const auto& r : rects) area += r.WorldAABB.Size().X * r.WorldAABB.Size().Y;
        CHECK_THAT(area, WithinAbs(7.0f * 32.0f * 32.0f, Tol));

        // SetTile invalidates the chunk's cached mesh.
        for (i32 x = 0; x <= 2; ++x) tilemap.SetTile(x, 0, ObjectsLayer, 0);
        pw.QueryTileColliders(Everywhere, rects);
        REQUIRE(rects.size() == 1);
        CHECK_THAT(rects[0].WorldAABB.Size().X, WithinAbs(64.0f, Tol));
        CHECK_THAT(rects[0].WorldAABB.Size().Y, WithinAbs(64.0f, Tol));
    }

    SECTION("adjacent tiles with different collision layers don't merge")
    {
        TileDef prop{};
        prop.Solid = true;
        prop.Layer = CollisionLayers::Prop;
        tilemap.RegisterTile(2, prop);
        tilemap.SetTile(10, 0, ObjectsLayer, 1);
        tilemap.SetTile(11, 0, ObjectsLayer, 2);
        pw.QueryTileColliders(Everywhere, rects);
        CHECK(rects.size() == 2);
    }

    SECTION("a chunk boundary splits the mesh")
    {
        // ChunkSize 16: tile x 15 is chunk 0, x 16 is chunk 1.
        tilemap.SetTile(15, 5, ObjectsLayer, 1);
        tilemap.SetTile(16, 5, ObjectsLayer, 1);
        pw.QueryTileColliders(Everywhere, rects);
        CHECK(rects.size() == 2);
    }
}

TEST_CASE("QueryTileBlocked plan-then-commit checks", "[physics][world][tiles]")
{
    TileMap tilemap;
    SetupWallTiles(tilemap);
    PhysicsWorld pw(32.0f);
    pw.SetTileMap(&tilemap);

    const Collider2D moverCol{ .Shape = ColliderShape::Box, .HalfExtents = { 4.0f, 4.0f }, .Layer = CollisionLayers::Player };
    const Entity     mover{ .Index = 10 };
    pw.RegisterCollider(mover, moverCol, { 0.0f, 0.0f });

    const Collider2D boxCol{ .Shape = ColliderShape::Box, .HalfExtents = { 6.0f, 6.0f }, .Kind = BodyKind::Static,
                             .Layer = CollisionLayers::Prop };
    pw.RegisterCollider(Entity{ .Index = 11 }, boxCol, { 50.0f, 0.0f });

    const Collider2D triggerCol{ .Shape = ColliderShape::Box, .HalfExtents = { 6.0f, 6.0f }, .Kind = BodyKind::Static,
                                 .Layer = CollisionLayers::Trigger, .IsTrigger = true };
    pw.RegisterCollider(Entity{ .Index = 12 }, triggerCol, { 0.0f, 50.0f });

    tilemap.SetTile(6, 0, ObjectsLayer, 1); // solid tile centered at (192, 0)

    CHECK_FALSE(pw.QueryTileBlocked(mover, moverCol, { 0.0f, 0.0f }, { 0.0f, -32.0f })); // clear path
    CHECK(pw.QueryTileBlocked(mover, moverCol, { 0.0f, 0.0f }, { 60.0f, 0.0f }));         // static box
    CHECK(pw.QueryTileBlocked(mover, moverCol, { 160.0f, 0.0f }, { 220.0f, 0.0f }));      // solid tile
    CHECK_FALSE(pw.QueryTileBlocked(mover, moverCol, { 0.0f, 0.0f }, { 0.0f, 60.0f }));   // trigger
    CHECK_FALSE(pw.QueryTileBlocked(mover, moverCol, { 0.0f, 0.0f }, { 1.0f, 0.0f }));    // self

    SECTION("directional entity collider")
    {
        const Collider2D tree{ .Shape = ColliderShape::Box, .HalfExtents = { 6.0f, 6.0f }, .Kind = BodyKind::Static,
                               .Layer = CollisionLayers::Prop, .BlockedFrom = DirectionArc::Vertical() };
        pw.RegisterCollider(Entity{ .Index = 13 }, tree, { -200.0f, 0.0f });
        CHECK_FALSE(pw.QueryTileBlocked(mover, moverCol, { -180.0f, 0.0f }, { -200.0f, 0.0f })); // from the east
        CHECK(pw.QueryTileBlocked(mover, moverCol, { -200.0f, -20.0f }, { -200.0f, 0.0f }));     // from the north
    }

    SECTION("directional tile")
    {
        TileDef ledge{};
        ledge.Solid       = true;
        ledge.BlockedFrom = DirectionArc::NorthOnly();
        tilemap.RegisterTile(2, ledge);
        tilemap.SetTile(-6, 5, ObjectsLayer, 2); // centered at (-192, 160)
        CHECK_FALSE(pw.QueryTileBlocked(mover, moverCol, { -192.0f, 180.0f }, { -192.0f, 160.0f })); // from the south
        CHECK(pw.QueryTileBlocked(mover, moverCol, { -192.0f, 130.0f }, { -192.0f, 150.0f }));        // from the north
    }
}

TEST_CASE("QueryTileBlocked tests a rotated obstacle's real shape", "[physics][world][obb]")
{
    PhysicsWorld     pw(32.0f);
    const Collider2D moverCol{ .Shape = ColliderShape::Box, .HalfExtents = { 4.0f, 4.0f } };
    const Entity     mover{ .Index = 20 };
    pw.RegisterCollider(mover, moverCol, { 110.0f, -26.0f });

    const Collider2D plank{ .Shape = ColliderShape::Box, .Rotation = QuarterPi, .HalfExtents = { 40.0f, 2.0f },
                            .Kind = BodyKind::Static };
    const Entity plankEntity{ .Index = 21 };
    pw.RegisterCollider(plankEntity, plank, { 100.0f, 0.0f });

    REQUIRE(pw.GetRecord(pw.FindColliderForEntity(plankEntity)).WorldAABB.Contains({ 120.0f, -26.0f }));
    CHECK_FALSE(pw.QueryTileBlocked(mover, moverCol, { 110.0f, -26.0f }, { 130.0f, -26.0f })); // empty AABB corner

    PhysicsWorld::BlockingHit hit;
    CHECK(pw.QueryTileBlocked(mover, moverCol, { 100.0f, -20.0f }, { 100.0f, 20.0f }, &hit)); // across the plank
    CHECK(hit.Other == plankEntity);
}

TEST_CASE("CollectTriggerOverlaps and IsTouching", "[physics][world][contacts]")
{
    PhysicsWorld     pw(32.0f);
    const Collider2D zone{ .HalfExtents = { 16.0f, 16.0f }, .Kind = BodyKind::Static, .Layer = CollisionLayers::Trigger,
                           .Mask = CollisionLayers::Player, .IsTrigger = true };
    const Collider2D player{ .HalfExtents = { 4.0f, 4.0f }, .Layer = CollisionLayers::Player };
    const Collider2D npc{ .HalfExtents = { 4.0f, 4.0f }, .Layer = CollisionLayers::NPC };

    SECTION("triggers report (trigger, other) for unmasked, overlapping non-triggers only")
    {
        const Entity zoneE{ .Index = 30 }, inside{ .Index = 31 }, otherZone{ .Index = 34 };
        pw.RegisterCollider(zoneE, zone, { 0.0f, 0.0f });
        pw.RegisterCollider(inside, player, { 10.0f, 0.0f });
        pw.RegisterCollider(Entity{ .Index = 32 }, npc, { -10.0f, 0.0f });    // masked out
        pw.RegisterCollider(Entity{ .Index = 33 }, player, { 60.0f, 0.0f });  // outside
        pw.RegisterCollider(otherZone, zone, { 8.0f, 8.0f });                // trigger-vs-trigger ignored

        std::vector<EntityPair> pairs;
        pw.CollectTriggerOverlaps(pairs);
        CHECK(pairs.size() == 2);
        CHECK(std::ranges::find(pairs, EntityPair{ zoneE, inside }) != pairs.end());
        CHECK(std::ranges::find(pairs, EntityPair{ otherZone, inside }) != pairs.end());
    }

    SECTION("IsTouching uses the skin distance")
    {
        const Entity anchor{ .Index = 37 }, nearE{ .Index = 35 }, farE{ .Index = 36 };
        pw.RegisterCollider(anchor, player, { 10.0f, 100.0f });
        pw.RegisterCollider(nearE, player, { 18.5f, 100.0f }); // 0.5 px gap
        pw.RegisterCollider(farE, player, { 30.0f, 100.0f });  // 12 px gap
        CHECK(pw.IsTouching(anchor, nearE, 1.0f));
        CHECK_FALSE(pw.IsTouching(anchor, farE, 1.0f));
        CHECK_FALSE(pw.IsTouching(anchor, Entity{ .Index = 99 }, 1.0f)); // unregistered
    }
}
