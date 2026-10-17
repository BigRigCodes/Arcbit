// ---------------------------------------------------------------------------
// Phase 22B collision verification demo.
//
// Sets up a tiny scene to exercise the swept resolver end-to-end:
//   - 17×17 grass field
//   - 4×4 water square in the middle, marked Solid → tile collider mesh
//   - A pair of static box "rocks" so entity-vs-entity blocking + slide is
//     visible alongside tile blocking
//   - Player as a circle collider: InputMovement turns WASD into MoveIntent,
//     FreeMovement into velocity; the resolver clamps + slides against tiles
//     and props
//   - A pink bumper (22I): touching it knocks the player back with an
//     external-velocity impulse that fades out with friction
//   - A wooden crate (22I): Pushable, so walking into it shoves it along at
//     half speed until it hits the water or a rock
//
// Press F2 to toggle physics debug draw (defaults to ON for instant feedback).
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
#include <arcbit/physics/PhysicsCallbacks.h>
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

constexpr f32 TileSize    = 64.0f;
constexpr f32 PlayerSpeed = 240.0f; // world px/sec
constexpr u32 GrassTileId = 1;   // base of grass.tileatlas.json
constexpr u32 WaterTileId = 409; // base of water.tileatlas.json
constexpr i32 GrassExtent = 8;   // grass field is [-8..+8] tiles each axis
constexpr i32 WaterMin    = -2;  // water is [-2..+1] each axis (4×4)
constexpr i32 WaterMax    = 1;
constexpr f32 BumperKick  = 700.0f; // px/s impulse; with the default friction it carries ~90 px
} // namespace

class PhysicsDemo : public Scene
{
protected:
    void OnPrepare(ScenePreloader& preload) override { Demo::PreloadGrassAndWaterAtlases(preload); }

    void OnLoad() override
    {
        _sampler  = Demo::CreateNearestRepeatSampler(GetApp().GetDevice());
        _whiteTex = Demo::CreateWhiteTexture(GetApp().GetDevice());

        GetConfig().TileSize = TileSize;
        GetCamera().Zoom     = 1.5f;

        TileMap& tm = GetTileMap();
        tm.SetTileSize(TileSize);
        ARCBIT_VERIFY(Demo::LoadGrassAndWaterAtlases(tm, GetApp().GetTextures(), _sampler),
                      "PhysicsDemo: failed to load tile atlases");

        // Water tile id 409 is solid for this demo. Overrides whatever
        // water.tileatlas.json says (which is fine — TileDef::Solid is the
        // only field this demo cares about).
        TileDef waterDef{};
        waterDef.Solid = true;
        tm.RegisterTile(WaterTileId, waterDef);

        // Generate the scene: grass everywhere, water in the middle.
        for (i32 ty = -GrassExtent; ty <= GrassExtent; ++ty)
            for (i32 tx = -GrassExtent; tx <= GrassExtent; ++tx)
                tm.SetTile(tx, ty, 0, GrassTileId);

        for (i32 ty = WaterMin; ty <= WaterMax; ++ty)
            for (i32 tx = WaterMin; tx <= WaterMax; ++tx)
                tm.SetTile(tx, ty, 0, WaterTileId);

        // PhysicsWorld is owned by Scene; the first GetPhysics() call lazy-
        // constructs it using WorldConfig.TileSize as the spatial-hash cell
        // size and auto-wires the tilemap pointer.
        PhysicsWorld& physics = GetPhysics();
        physics.SetDebugDraw(true); // ON by default so the demo is self-explanatory

        auto& world = GetWorld();
        CreatePlayer(world, physics);
        CreateRocks();
        CreateBumper();
        CreateCrate();

        LOG_INFO(Game, "PhysicsDemo ready - WASD to move, F2 toggles collider overlay");
    }

    // Player: blue circle, FreeMovement-driven, ECS-side Collider2D so the
    // resolver can sweep it. Also registered with the broadphase so it shows
    // up in QueryAABB hits for other movers.
    void CreatePlayer(World& world, PhysicsWorld& physics)
    {
        _player                                 = world.CreateEntity();
        world.GetComponent<Tag>(_player)->Value = "Player";
        world.AddComponent<Persistent>(_player, Persistent{ .Id = "player", .Components = { "Transform2D" } }); // saved
        auto* t                                 = world.GetTransform(_player);
        t->Position                             = {TileSize * 5.0f, 0.0f};
        t->Scale                                = {TileSize * 0.7f, TileSize * 0.7f};
        world.AddComponent<SpriteRenderer>(_player, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.3f, 0.5f, 1.0f, 1.0f},
                                               .Layer   = 0,
                                           });
        world.AddComponent<CameraTarget>(_player, CameraTarget{.Lag = 0.05f});
        world.AddComponent<FreeMovement>(_player, FreeMovement{.Speed = PlayerSpeed});
        world.AddComponent<PendingMove>(_player, PendingMove{});
        world.AddComponent<InputMovement>(_player, InputMovement{ ActionMoveLeft, ActionMoveRight,
                                                                  ActionMoveUp, ActionMoveDown });

        const Collider2D playerCollider{
            .Shape  = ColliderShape::Circle,
            .Radius = TileSize * 0.35f,
            .Kind   = BodyKind::Kinematic,
            .Layer  = CollisionLayers::Player,
        };
        world.AddComponent<Collider2D>(_player, playerCollider);
        // Id discarded — the resolver looks it up by entity each tick. Re-fetch
        // the transform: each AddComponent above moved the entity to a new
        // archetype, so `t` no longer points at its Transform2D.
        physics.RegisterCollider(_player, playerCollider, world.GetTransform(_player)->Position);
    }

    // A sprite-backed box prop with a matching collider.
    Entity MakeBox(const char* tag, const Vec2 pos, const Vec2 size, const Color tint, const BodyKind kind)
    {
        auto&        world                = GetWorld();
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = tag;
        auto* t                           = world.GetTransform(e);
        t->Position                       = pos;
        t->Scale                          = size;
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = tint,
                                               .Layer   = static_cast<i32>(pos.Y + size.Y * 0.5f),
                                           });
        const Collider2D col{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {size.X * 0.5f, size.Y * 0.5f},
            .Kind        = kind,
            .Layer       = CollisionLayers::Prop,
        };
        world.AddComponent<Collider2D>(e, col);
        (void)GetPhysics().RegisterCollider(e, col, pos);
        return e;
    }

    // Two static box props east of the player so entity-vs-entity blocking +
    // slide is verifiable separately from the tile collider path.
    void CreateRocks()
    {
        constexpr Color RockTint = {0.55f, 0.45f, 0.35f, 1.0f};
        (void)MakeBox("Rock", {TileSize * 8.0f, -TileSize * 1.5f}, {TileSize, TileSize}, RockTint, BodyKind::Static);
        (void)MakeBox("Rock", {TileSize * 7.5f, TileSize * 2.5f}, {TileSize * 0.75f, TileSize * 1.5f}, RockTint,
                      BodyKind::Static);
    }

    // Touching the bumper kicks the toucher straight back out. The normal in
    // OnEnter points from the other entity toward the bumper, so the kick is
    // along its reverse.
    void CreateBumper()
    {
        const Entity bumper = MakeBox("Bumper", {TileSize * 5.0f, -TileSize * 4.0f}, {TileSize, TileSize},
                                      {0.95f, 0.35f, 0.75f, 1.0f}, BodyKind::Static);
        GetWorld().AddComponent<CollisionCallback>(bumper, CollisionCallback{
            .OnEnter = [this](Entity, const Entity other, const Vec2 normal) {
                if (auto* fm = GetWorld().GetComponent<FreeMovement>(other)) fm->AddImpulse(normal * -BumperKick);
            },
        });
    }

    // A crate the player can shove around; it stops at the water and rocks.
    void CreateCrate()
    {
        const Entity crate = MakeBox("Crate", {TileSize * 3.0f, TileSize * 4.0f}, {TileSize * 0.8f, TileSize * 0.8f},
                                     {0.7f, 0.5f, 0.25f, 1.0f}, BodyKind::Kinematic);
        GetWorld().AddComponent<Pushable>(crate, Pushable{ .Resistance = 0.5f });
        GetWorld().AddComponent<Persistent>(crate, Persistent{ .Id = "crate", .Components = { "Transform2D" } });
    }

    void OnUpdate(const f32 /*dt*/) override
    {
        PhysicsWorld& physics = GetPhysics();

        if (GetApp().GetInput().JustPressed(ActionDebugPhysics)) {
            physics.SetDebugDraw(!physics.GetDebugDraw());
            LOG_INFO(Game, "Physics debug draw: {}", physics.GetDebugDraw() ? "ON" : "OFF");
        }
        if (GetApp().GetInput().JustPressed(ActionToggleGrid)) _showGrid = !_showGrid;
    }

    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor = {1.0f, 1.0f, 1.0f, 1.0f};
        // Emit collider outlines on top of the scene. Sprites already collected
        // by the scene's Y-sort + tilemap render pipeline; we just append.
        GetPhysics().CollectDebugDraw(packet, _whiteTex, _sampler);

        if (_showGrid) {
            // Compute world-space view AABB from the camera + window viewport.
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
        // No need to UnregisterCollider — the Scene-owned PhysicsWorld is
        // destroyed in the Scene destructor, which tears down the spatial
        // hash + entry vectors wholesale.
        if (_whiteTex.IsValid()) GetApp().GetDevice().DestroyTexture(_whiteTex);
        if (_sampler.IsValid()) GetApp().GetDevice().DestroySampler(_sampler);
    }

private:
    SamplerHandle _sampler;
    TextureHandle _whiteTex;
    Entity        _player   = Entity::Invalid();
    bool          _showGrid = false;
};

std::unique_ptr<Scene> Demo::CreatePhysicsDemo() { return std::make_unique<PhysicsDemo>(); }
