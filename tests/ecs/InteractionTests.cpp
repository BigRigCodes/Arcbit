// Which Interactable an Interactor uses: the faced tile for tile movers, the
// nearest one in front (else the nearest in reach) for free movers.

#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/ecs/World.h>

#include <catch2/catch_test_macros.hpp>

using namespace Arcbit;

namespace
{
constexpr f32 Tile = 64.0f;

Entity Place(World& world, const Vec2 at, const bool interactable = true)
{
    const Entity e                  = world.CreateEntity();
    world.GetTransform(e)->Position = at;
    if (interactable) world.AddComponent<Interactable>(e);
    return e;
}

Entity Player(World& world, const Vec2 facing, const bool onTiles)
{
    const Entity e = Place(world, { 0, 0 }, false);
    world.AddComponent<Interactor>(e);
    MoveIntent intent;
    intent.Facing = facing;
    world.AddComponent<MoveIntent>(e, intent);
    if (onTiles) world.AddComponent<SmoothTileMovement>(e);
    return e;
}
} // namespace

TEST_CASE("A tile mover uses exactly the tile it faces", "[interaction]")
{
    World        world;
    const Entity sign  = Place(world, { 0, Tile });   // in front (facing down)
    const Entity chest = Place(world, { Tile, 0 });   // beside

    CHECK(FindInteractTarget(world, Player(world, { 0, 1 }, true), Tile) == sign);
    CHECK(FindInteractTarget(world, Player(world, { 1, 0 }, true), Tile) == chest);
    CHECK_FALSE(FindInteractTarget(world, Player(world, { 0, -1 }, true), Tile).IsValid()); // nothing there
}

TEST_CASE("A free mover prefers what's in front, then the nearest in reach", "[interaction]")
{
    World        world;
    const Entity chest = Place(world, { 30, 0 }); // beside, nearer
    const Entity sign  = Place(world, { 0, 50 }); // in front
    (void)Place(world, { 0, 500 });               // in front but out of reach (1.25 tiles)

    CHECK(FindInteractTarget(world, Player(world, { 0, 1 }, false), Tile) == sign);
    CHECK(FindInteractTarget(world, Player(world, { -1, 0 }, false), Tile) == chest); // nothing ahead: nearest

    world.AddComponent<Disabled>(sign); // a disabled one is skipped
    CHECK(FindInteractTarget(world, Player(world, { 0, 1 }, false), Tile) == chest);
}
