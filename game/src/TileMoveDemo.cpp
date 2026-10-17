// ---------------------------------------------------------------------------
// Phase 22C tile-movement collision demo.
//
// Sister demo to PhysicsDemo (free movement) — exercises the plan-then-commit
// path through SmoothTileMoveSystem + PhysicsWorld::QueryTileBlocked:
//   - 17×17 grass field
//   - A 7-tile vertical wall of solid water tiles at column +3
//   - One static box "rock" south of the player so entity-vs-entity blocking
//     is verifiable separately from tile blocking
//   - Player as a small box collider with InputMovement; WASD becomes a
//     MoveIntent, the tile-movement system rejects the step when the path
//     is blocked. Holding two directions follows the one pressed last.
//   - Two Pushable crates (22I), Sokoban-style: walking into one pushes it a
//     tile if the cell beyond is free. One sits next to the water wall, so
//     it jams after a single push; crates don't push each other.
//
// M swaps SmoothTileMovement (lerp between tiles) and SnapTileMovement
// (instant steps, repeating while held) at the same tiles-per-second pace.
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
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/WorldConfig.h>
#include <arcbit/tilemap/TileMap.h>

#include <cmath>
#include <vector>

using namespace Arcbit;

namespace
{
constexpr ActionID ActionMoveLeft     = MakeAction("Move_Left");
constexpr ActionID ActionMoveRight    = MakeAction("Move_Right");
constexpr ActionID ActionMoveUp       = MakeAction("Move_Up");
constexpr ActionID ActionMoveDown     = MakeAction("Move_Down");
constexpr ActionID ActionDebugPhysics = MakeAction("Debug_Physics");
constexpr ActionID ActionToggleGrid   = MakeAction("Debug_ToggleGrid");
constexpr ActionID ActionToggleStyle  = MakeAction("Demo_ToggleMovementStyle");

constexpr f32 TileSize    = 64.0f;
constexpr u32 GrassTileId = 1;   // base of grass.tileatlas.json
constexpr u32 WaterTileId = 409; // base of water.tileatlas.json
constexpr i32 GrassExtent = 8;   // grass field is [-8..+8] tiles each axis
constexpr i32 WallColumn  = 3;   // vertical wall of water tiles at tx=3
constexpr i32 WallMin     = -3;  // wall spans ty=-3..+3 (7 tiles)
constexpr i32 WallMax     = 3;
constexpr f32 WalkSpeed   = 4.0f; // tiles/sec
constexpr f32 SnapStep    = 1.0f / WalkSpeed; // same pace in snap mode
} // namespace

class TileMoveDemo : public Scene
{
protected:
    void OnLoad() override
    {
        _sampler  = Demo::CreateNearestRepeatSampler(GetApp().GetDevice());
        _whiteTex = Demo::CreateWhiteTexture(GetApp().GetDevice());

        GetConfig().TileSize = TileSize;
        GetCamera().Zoom     = 1.5f;

        TileMap& tm = GetTileMap();
        tm.SetTileSize(TileSize);
        ARCBIT_VERIFY(Demo::LoadGrassAndWaterAtlases(tm, GetApp().GetTextures(), _sampler),
                      "TileMoveDemo: failed to load tile atlases");

        // Mark the water tile id as solid so the greedy mesher picks it up.
        TileDef waterDef{};
        waterDef.Solid = true;
        tm.RegisterTile(WaterTileId, waterDef);

        // Grass everywhere; water wall at column WallColumn.
        for (i32 ty = -GrassExtent; ty <= GrassExtent; ++ty)
            for (i32 tx = -GrassExtent; tx <= GrassExtent; ++tx)
                tm.SetTile(tx, ty, 0, GrassTileId);
        for (i32 ty = WallMin; ty <= WallMax; ++ty)
            tm.SetTile(WallColumn, ty, 0, WaterTileId);


        PhysicsWorld& physics = GetPhysics();
        physics.SetDebugDraw(true);

        auto& world = GetWorld();
        CreatePlayer(world, physics);
        CreateRocks(world, physics);
        CreateCrate({ TileSize * 1.0f, -TileSize * 1.0f }); // one push from the water wall
        CreateCrate({ -TileSize * 2.0f, 0.0f });

        LOG_INFO(Game, "TileMoveDemo ready - WASD to step, M movement style, F2 collider overlay");
    }


    // Player: blue square, SmoothTileMovement-driven. ECS-side Collider2D so
    // the QueryTileBlocked path picks it up; broadphase registration so the
    // resolver / future free-movement entities see it.
    void CreatePlayer(World& world, PhysicsWorld& physics)
    {
        _player                                 = world.CreateEntity();
        world.GetComponent<Tag>(_player)->Value = "Player";
        auto* t                                 = world.GetTransform(_player);
        t->Position                             = {0.0f, 0.0f}; // tile (0,0)
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

        // Half-extents slightly less than half a tile so corner approaches don't
        // get caught on the rounded broadphase AABB of a circle obstacle.
        const Collider2D playerCollider{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {TileSize * 0.4f, TileSize * 0.4f},
            .Kind        = BodyKind::Kinematic,
            .Layer       = CollisionLayers::Player,
        };
        world.AddComponent<Collider2D>(_player, playerCollider);
        // Re-fetch: each AddComponent above moved the entity to a new archetype,
        // so `t` no longer points at its Transform2D.
        physics.RegisterCollider(_player, playerCollider, world.GetTransform(_player)->Position);
    }

    // One static rock south of the player so entity-vs-entity blocking is
    // exercised separately from the tile wall.
    void CreateRocks(World& world, PhysicsWorld& physics)
    {
        const Vec2   rockPos{0.0f, TileSize * 3.0f};
        const Vec2   rockSize{TileSize, TileSize};
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = "Rock";
        auto* t                           = world.GetTransform(e);
        t->Position                       = rockPos;
        t->Scale                          = rockSize;
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.55f, 0.45f, 0.35f, 1.0f},
                                               .Layer   = static_cast<i32>(rockPos.Y + rockSize.Y * 0.5f),
                                           });
        const Collider2D rockCol{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {rockSize.X * 0.5f, rockSize.Y * 0.5f},
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Prop,
        };
        world.AddComponent<Collider2D>(e, rockCol);
        (void)physics.RegisterCollider(e, rockCol, rockPos);
    }

    // A Pushable crate the size of the player's collider, so it fits a tile.
    void CreateCrate(const Vec2 pos)
    {
        auto&        world                = GetWorld();
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = "Crate";
        auto* t                           = world.GetTransform(e);
        t->Position                       = pos;
        t->Scale                          = {TileSize * 0.8f, TileSize * 0.8f};
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.7f, 0.5f, 0.25f, 1.0f},
                                               .Layer   = 0,
                                           });
        const Collider2D col{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {TileSize * 0.4f, TileSize * 0.4f},
            .Kind        = BodyKind::Kinematic,
            .Layer       = CollisionLayers::Prop,
        };
        world.AddComponent<Collider2D>(e, col);
        world.AddComponent<Pushable>(e);
        (void)GetPhysics().RegisterCollider(e, col, pos);
        _crates.push_back(e);
    }

    // Round an entity to its nearest tile center and refresh its collider.
    void SnapToGrid(const Entity e)
    {
        auto& world = GetWorld();
        Vec2& p     = world.GetTransform(e)->Position;
        p           = {std::round(p.X / TileSize) * TileSize, std::round(p.Y / TileSize) * TileSize};
        PhysicsWorld& physics = GetPhysics();
        physics.UpdateCollider(physics.FindColliderForEntity(e), *world.GetComponent<Collider2D>(e), p);
    }

    // Swap SmoothTileMovement <-> SnapTileMovement. Leaving smooth mode
    // mid-step lands the player (and any crate it was pushing) on the nearest
    // tile center so snap steps stay aligned.
    void ToggleMovementStyle()
    {
        auto& world = GetWorld();
        if (world.GetComponent<SnapTileMovement>(_player)) {
            world.RemoveComponent<SnapTileMovement>(_player);
            world.AddComponent<SmoothTileMovement>(_player, SmoothTileMovement{.Speed = WalkSpeed});
            LOG_INFO(Game, "Movement: smooth");
            return;
        }
        world.RemoveComponent<SmoothTileMovement>(_player);
        world.AddComponent<SnapTileMovement>(_player, SnapTileMovement{.StepInterval = SnapStep});
        SnapToGrid(_player);
        for (const Entity crate : _crates) SnapToGrid(crate);
        LOG_INFO(Game, "Movement: snap");
    }

    void OnUpdate(const f32 /*dt*/) override
    {
        PhysicsWorld& physics = GetPhysics();
        if (GetApp().GetInput().JustPressed(ActionToggleStyle)) ToggleMovementStyle();

        if (GetApp().GetInput().JustPressed(ActionDebugPhysics)) {
            physics.SetDebugDraw(!physics.GetDebugDraw());
            LOG_INFO(Game, "Physics debug draw: {}", physics.GetDebugDraw() ? "ON" : "OFF");
        }
        if (GetApp().GetInput().JustPressed(ActionToggleGrid)) _showGrid = !_showGrid;
    }

    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor = {1.0f, 1.0f, 1.0f, 1.0f};
        GetPhysics().CollectDebugDraw(packet, _whiteTex, _sampler);

        if (_showGrid) {
            const Camera2D& cam    = GetCamera();
            const Vec2      camPos = cam.GetEffectivePosition();
            const Vec2      halfView{
                (1920.0f / cam.Zoom) * 0.5f,
                (1080.0f / cam.Zoom) * 0.5f
            };
            const AABB viewAABB  = AABB::FromCenterHalfExtents(camPos, halfView);
            const f32  thickness = std::max(1.0f, 1.0f / cam.Zoom);
            GetTileMap().CollectGridDebugDraw(
                packet, viewAABB, _whiteTex, _sampler, thickness);
        }
    }

    void OnUnload() override
    {
        if (_whiteTex.IsValid()) GetApp().GetDevice().DestroyTexture(_whiteTex);
        if (_sampler.IsValid()) GetApp().GetDevice().DestroySampler(_sampler);
    }

private:
    SamplerHandle       _sampler;
    TextureHandle       _whiteTex;
    Entity              _player   = Entity::Invalid();
    std::vector<Entity> _crates;
    bool                _showGrid = false;
};

std::unique_ptr<Scene> Demo::CreateTileMoveDemo() { return std::make_unique<TileMoveDemo>(); }
