// Y-sort (SpriteRenderer::YSort): a Y-sorted sprite draws at its bottom
// edge's Y, on the same scale as Objects-layer tiles, so a character sorts
// against a tree's trunk by the row it stands in. Driven through a real
// Scene's render systems.

#include "TestSupport.h"

#include <arcbit/ecs/Components.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/scene/Scene.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Arcbit;

namespace
{
Entity AddSprite(Scene& scene, const Vec2 pos, const Vec2 size, SpriteRenderer sprite)
{
    World&       world = scene.GetWorld();
    const Entity e     = world.CreateEntity();
    auto*        t     = world.GetTransform(e);
    t->Position        = pos;
    t->Scale           = size;
    world.AddComponent<SpriteRenderer>(e, std::move(sprite));
    return e;
}

// Each sprite is told apart by its tint's red.
i32 LayerOf(Scene& scene, const f32 red)
{
    FramePacket packet{ .Width = 1920, .Height = 1080 };
    scene.CollectRenderData(packet);
    const auto it = std::ranges::find_if(packet.Sprites, [red](const Sprite& s) { return s.Tint.R == red; });
    REQUIRE(it != packet.Sprites.end());
    return it->Layer;
}

Color Red(const f32 r) { return { r, 0.0f, 0.0f, 1.0f }; }
} // namespace

TEST_CASE("Y-sorted sprites draw at their bottom edge; flat ones keep their layer", "[ecs][ysort]")
{
    Scene scene;
    scene.GetConfig().YSortSprites = true; // the project default (Application sets it)
    AddSprite(scene, { 0.0f, 100.0f }, { 32.0f, 64.0f }, { .Tint = Red(0.1f), .Layer = 7 });
    AddSprite(scene, { 0.0f, 50.0f }, { 32.0f, 32.0f }, { .Pivot = { 0.5f, 1.0f }, .Tint = Red(0.2f) }); // feet at Y
    AddSprite(scene, { 0.0f, 100.0f }, { 32.0f, 32.0f }, { .Tint = Red(0.3f), .Layer = -5, .YSort = false });
    const Entity sky = AddSprite(scene, { 0.0f, 100.0f }, { 32.0f, 32.0f }, { .Tint = Red(0.4f), .Layer = -9 });
    scene.GetWorld().AddComponent<Parallax>(sky);

    CHECK(LayerOf(scene, 0.1f) == 132); // 100 + 64 / 2
    CHECK(LayerOf(scene, 0.2f) == 50);  // the pivot is at its bottom edge
    CHECK(LayerOf(scene, 0.3f) == -5);  // "y_sort": false
    CHECK(LayerOf(scene, 0.4f) == -9);  // parallax: not Y-sorted unless it says

    scene.GetConfig().YSortSprites = false; // a project with it off: Layer, unless a sprite asks
    CHECK(LayerOf(scene, 0.1f) == 7);
    scene.GetWorld().GetComponent<SpriteRenderer>(sky)->YSort = true;
    CHECK(LayerOf(scene, 0.4f) == 116);
}

TEST_CASE("A character sorts against an Objects tile by the row it stands in", "[ecs][ysort][tilemap]")
{
    Scene          scene;
    const TileMap& map = scene.GetTileMap();
    const f32      ts  = map.GetTileSize();
    scene.GetConfig().YSortSprites = true;

    // A tile-sized character, centered on its tile, in the row below a
    // trunk at row 3, in the trunk's row, and in the row above it.
    AddSprite(scene, map.TileToWorld(0, 4), { ts, ts }, { .Tint = Red(0.1f) });
    AddSprite(scene, map.TileToWorld(0, 3), { ts, ts }, { .Tint = Red(0.2f) });
    AddSprite(scene, map.TileToWorld(0, 2), { ts, ts }, { .Tint = Red(0.3f) });
    // A taller one (1.5 tiles), feet on the tile's bottom edge, a row above.
    const Vec2 feet = map.TileToWorld(0, 2) + Vec2{ 0.0f, ts * 0.5f };
    AddSprite(scene, feet, { ts, ts * 1.5f }, { .Pivot = { 0.5f, 1.0f }, .Tint = Red(0.4f) });

    const i32 trunk = map.ObjectSortLayer(3);
    CHECK(LayerOf(scene, 0.1f) > trunk); // in front
    CHECK(LayerOf(scene, 0.2f) < trunk); // behind
    CHECK(LayerOf(scene, 0.3f) < trunk);
    CHECK(LayerOf(scene, 0.4f) < trunk);
    CHECK(LayerOf(scene, 0.3f) < map.ObjectSortLayer(2)); // and behind a canopy tile in its own row
}
