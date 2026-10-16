// ---------------------------------------------------------------------------
// Phase 22D tile-movement + directional collision demo.
//
// Sister demo to TileMoveDemo. Exercises BlockedFrom arcs riding on TileDef
// (the path through PhysicsWorld::QueryTileBlocked → tile collider rects),
// not entity colliders. Three patterns:
//
//   - **Tree pair (column +3)** — two adjacent solid tiles. Canopy at row -1
//     uses SouthOnly(); trunk at row 0 uses NorthOnly(). Walking N from the
//     south stops one tile south of the canopy ("in front of the trunk").
//     Walking S from the north stops one tile north of the trunk ("behind
//     the canopy"). E/W passes through both rows freely. Captures the
//     classic RPG tree at tile granularity without sub-tile colliders.
//
//   - **Bush (column -3, row 2)** — single solid tile with Horizontal()
//     arcs. Blocks E/W approaches, lets N/S pass.
//
//   - **Cliff strip (row -3, columns 1..3)** — three solid tiles with
//     NorthOnly() arcs. Greedy mesher merges them into one rect (matching
//     collision class). Player can walk south off the strip but can't
//     push north onto it from below.
//
// Press F2 to toggle physics debug draw (defaults to ON for instant feedback).
// G toggles the grid overlay.
// ---------------------------------------------------------------------------

#include "Common.h"

#include <arcbit/app/Application.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/World.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/physics/DirectionArc.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/WorldConfig.h>
#include <arcbit/tilemap/TileMap.h>

using namespace Arcbit;

namespace
{
constexpr ActionID ActionMoveLeft     = MakeAction("Move_Left");
constexpr ActionID ActionMoveRight    = MakeAction("Move_Right");
constexpr ActionID ActionMoveUp       = MakeAction("Move_Up");
constexpr ActionID ActionMoveDown     = MakeAction("Move_Down");
constexpr ActionID ActionDebugPhysics = MakeAction("Debug_Physics");
constexpr ActionID ActionToggleGrid   = MakeAction("Debug_ToggleGrid");

constexpr f32 TileSize     = 64.0f;
constexpr u32 GrassTileId  = 1;
constexpr u32 ObjectsLayer = 1;
constexpr i32 GrassExtent  = 8;
constexpr f32 WalkSpeed    = 4.0f;

// Distinct tile IDs in the water atlas range (base 401), each registered with
// its own TileDef. The underlying atlas UV is irrelevant — overlay entities
// paint the visible color.
constexpr u32 CanopyTileId = 410; // SouthOnly
constexpr u32 TrunkTileId  = 411; // NorthOnly
constexpr u32 BushTileId   = 412; // Horizontal
constexpr u32 CliffTileId  = 413; // NorthOnly
} // namespace

class TileMoveArcDemo : public Application
{
public:
    TileMoveArcDemo() : Application({
        .Title = "Arcbit — Tile Move + Arcs (F2 toggles colliders)",
        .Width = 1920, .Height = 1080
    }) {}

protected:
    void OnStart() override
    {
        _sampler  = Demo::CreateNearestRepeatSampler(GetDevice());
        _whiteTex = Demo::CreateWhiteTexture(GetDevice());

        GetScene().GetConfig().TileSize = TileSize;
        GetScene().GetCamera().Zoom     = 1.5f;

        TileMap& tm = GetScene().GetTileMap();
        tm.SetTileSize(TileSize);
        ARCBIT_VERIFY(Demo::LoadGrassAndWaterAtlases(tm, GetTextures(), _sampler),
                      "TileMoveArcDemo: failed to load tile atlases");

        RegisterDirectionalTiles(tm);
        BuildMap(tm);

        SetupInput();

        PhysicsWorld& physics = GetScene().GetPhysics();
        physics.SetDebugDraw(true);

        auto& world = GetScene().GetWorld();
        CreatePlayer(world, physics);
        CreateOverlaySprites(world);

        LOG_INFO(Game,
                 "TileMoveArcDemo ready — WASD to step, F2 toggles colliders, G toggles grid");
    }

    static void RegisterDirectionalTiles(TileMap& tm)
    {
        TileDef canopy{};
        canopy.Solid       = true;
        canopy.BlockedFrom = DirectionArc::SouthOnly();
        tm.RegisterTile(CanopyTileId, canopy);

        TileDef trunk{};
        trunk.Solid       = true;
        trunk.BlockedFrom = DirectionArc::NorthOnly();
        tm.RegisterTile(TrunkTileId, trunk);

        TileDef bush{};
        bush.Solid       = true;
        bush.BlockedFrom = DirectionArc::Horizontal();
        tm.RegisterTile(BushTileId, bush);

        TileDef cliff{};
        cliff.Solid       = true;
        cliff.BlockedFrom = DirectionArc::NorthOnly();
        tm.RegisterTile(CliffTileId, cliff);
    }

    static void BuildMap(TileMap& tm)
    {
        for (i32 ty = -GrassExtent; ty <= GrassExtent; ++ty)
            for (i32 tx = -GrassExtent; tx <= GrassExtent; ++tx)
                tm.SetTile(tx, ty, 0, GrassTileId);

        // Tree pair (canopy + trunk).
        tm.SetTile(3, -1, ObjectsLayer, CanopyTileId);
        tm.SetTile(3,  0, ObjectsLayer, TrunkTileId);

        // Bush.
        tm.SetTile(-3, 2, ObjectsLayer, BushTileId);

        // Cliff strip — three tiles wide; greedy-mesher merges into one rect.
        for (i32 cx = 1; cx <= 3; ++cx)
            tm.SetTile(cx, -3, ObjectsLayer, CliffTileId);
    }

    void SetupInput()
    {
        GetInput().RegisterAction(ActionMoveLeft, "Move_Left", "Move Left", "Movement");
        GetInput().RegisterAction(ActionMoveRight, "Move_Right", "Move Right", "Movement");
        GetInput().RegisterAction(ActionMoveUp, "Move_Up", "Move Up", "Movement");
        GetInput().RegisterAction(ActionMoveDown, "Move_Down", "Move Down", "Movement");
        GetInput().RegisterAction(ActionDebugPhysics, "Debug_Physics", "Toggle Physics Debug", "Debug");
        GetInput().RegisterAction(ActionToggleGrid, "Debug_ToggleGrid", "Toggle Grid", "Debug");

        GetInput().BindKey(ActionMoveLeft, Key::A);
        GetInput().BindKey(ActionMoveRight, Key::D);
        GetInput().BindKey(ActionMoveUp, Key::W);
        GetInput().BindKey(ActionMoveDown, Key::S);
        GetInput().BindKey(ActionDebugPhysics, Key::F2);
        GetInput().BindKey(ActionToggleGrid, Key::G);
    }

    // Player: blue square, SmoothTileMovement so each WASD press is a one-tile
    // hop. ECS Collider2D feeds QueryTileBlocked; broadphase registration so
    // the resolver / future free-mover entities also see it.
    void CreatePlayer(World& world, PhysicsWorld& physics)
    {
        _player                                 = world.CreateEntity();
        world.GetComponent<Tag>(_player)->Value = "Player";
        auto* t                                 = world.GetTransform(_player);
        t->Position                             = {0.0f, 0.0f};
        t->Scale                                = {TileSize * 0.7f, TileSize * 0.7f};
        world.AddComponent<SpriteRenderer>(_player, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.3f, 0.5f, 1.0f, 1.0f},
                                               .Layer   = 0,
                                           });
        world.AddComponent<CameraTarget>(_player, CameraTarget{.Lag = 0.05f});
        world.AddComponent<SmoothTileMovement>(_player, SmoothTileMovement{.Speed = WalkSpeed});
        world.AddComponent<InputMovement>(_player, InputMovement{ActionMoveLeft, ActionMoveRight,
                                                                 ActionMoveUp, ActionMoveDown});

        const Collider2D playerCollider{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {TileSize * 0.4f, TileSize * 0.4f},
            .Kind        = BodyKind::Kinematic,
            .Layer       = CollisionLayers::Player,
        };
        world.AddComponent<Collider2D>(_player, playerCollider);
        physics.RegisterCollider(_player, playerCollider, t->Position);
    }

    // Visual overlays: each directional tile renders whatever water-atlas UV
    // its ID happens to map to, which isn't very tree-like. Cover with a
    // colored sprite (no collider) so the geometry is unambiguous on screen.
    void CreateOverlaySprites(World& world)
    {
        AddOverlay(world, "Canopy", {3.0f * TileSize, -1.0f * TileSize},
                   {0.2f, 0.6f, 0.25f, 1.0f}); // green
        AddOverlay(world, "Trunk",  {3.0f * TileSize,  0.0f * TileSize},
                   {0.45f, 0.3f, 0.15f, 1.0f}); // brown
        AddOverlay(world, "Bush",   {-3.0f * TileSize, 2.0f * TileSize},
                   {0.4f, 0.7f, 0.4f, 1.0f}); // light green
        for (i32 cx = 1; cx <= 3; ++cx)
            AddOverlay(world, "Cliff",
                       {static_cast<f32>(cx) * TileSize, -3.0f * TileSize},
                       {0.7f, 0.6f, 0.3f, 1.0f}); // tan
    }

    void AddOverlay(World& world, const char* tag, const Vec2 pos, const Color color)
    {
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = tag;
        auto* t                           = world.GetTransform(e);
        t->Position                       = pos;
        t->Scale                          = {TileSize, TileSize};
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = color,
                                               .Layer   = static_cast<i32>(pos.Y + TileSize * 0.5f),
                                           });
    }

    void OnUpdate(const f32 /*dt*/) override
    {
        PhysicsWorld& physics = GetScene().GetPhysics();

        if (GetInput().JustPressed(ActionDebugPhysics)) {
            physics.SetDebugDraw(!physics.GetDebugDraw());
            LOG_INFO(Game, "Physics debug draw: {}", physics.GetDebugDraw() ? "ON" : "OFF");
        }
        if (GetInput().JustPressed(ActionToggleGrid)) _showGrid = !_showGrid;
    }

    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor = {1.0f, 1.0f, 1.0f, 1.0f};
        GetScene().GetPhysics().CollectDebugDraw(packet, _whiteTex, _sampler);

        if (_showGrid) {
            const Camera2D& cam    = GetScene().GetCamera();
            const Vec2      camPos = cam.GetEffectivePosition();
            const Vec2      halfView{
                (1920.0f / cam.Zoom) * 0.5f,
                (1080.0f / cam.Zoom) * 0.5f
            };
            const AABB viewAABB  = AABB::FromCenterHalfExtents(camPos, halfView);
            const f32  thickness = std::max(1.0f, 1.0f / cam.Zoom);
            GetScene().GetTileMap().CollectGridDebugDraw(
                packet, viewAABB, _whiteTex, _sampler, thickness);
        }
    }

    void OnShutdown() override
    {
        if (_whiteTex.IsValid()) GetDevice().DestroyTexture(_whiteTex);
        if (_sampler.IsValid()) GetDevice().DestroySampler(_sampler);
    }

private:
    SamplerHandle _sampler;
    TextureHandle _whiteTex;
    Entity        _player   = Entity::Invalid();
    bool          _showGrid = false;
};

// Entry point — main.cpp calls this when ARCBIT_DEMO_TILEARC is selected.
void RunTileMoveArcDemo()
{
    TileMoveArcDemo demo;
    demo.Run();
}
