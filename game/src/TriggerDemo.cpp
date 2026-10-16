// ---------------------------------------------------------------------------
// Phase 22F trigger + collision-event demo.
//
// Three event sources, each exercising a different callback:
//
//   - **Warp door** (yellow, east) — small trigger at a tile center.
//     TriggerCallback::OnEnter teleports the player to the landing spot
//     (blue marker, north-west). The landing tile is not a trigger, so the
//     player doesn't bounce straight back.
//
//   - **Hazard zone** (red, south) — 3×2-tile trigger. OnEnter tints the
//     player red, OnStay logs a damage tick every 0.5 s, OnExit restores.
//
//   - **NPC** (purple, west) — static blocking box with a CollisionCallback.
//     Bumping it flashes it white and logs; it reverts on OnExit.
//
//   - **Whirlpool tile** (north-east of spawn) — an Objects-layer tile over
//     the grass whose TileDef is a two-frame flip-book (and `flat`, so it
//     draws under entities) loaded from water.tileatlas.json. No entity
//     involved; the tilemap renderer cycles the frames.
//
// The player starts on SmoothTileMovement; M swaps to FreeMovement and back,
// so both blocking-contact paths (QueryTileBlocked and the swept resolver)
// are covered. Tile mode: the NPC un-flashes the tick after you stop
// pushing. Free mode: it stays flashed while you touch it.
//
// Keys: WASD move, M movement style, F2 collider outlines, G grid.
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

#include <cmath>

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

constexpr f32 TileSize        = 64.0f;
constexpr u32 GrassTileId     = 1;
constexpr u32 WaterBaseId     = 401;               // Demo::LoadGrassAndWaterAtlases
constexpr u32 WhirlpoolTileId = WaterBaseId + 156; // flip-book tile, see water.tileatlas.json
constexpr i32 GrassExtent     = 8;
constexpr f32 WalkSpeed       = 4.0f;   // tiles/s (tile mode)
constexpr f32 FreeSpeed       = 240.0f; // px/s (free mode)

// Hazard OnStay fires once per fixed tick (60 Hz); log every 30 → every 0.5 s.
constexpr u32 HazardLogInterval = 30;

constexpr Color PlayerColor = {0.3f, 0.5f, 1.0f, 1.0f};
constexpr Color HurtColor   = {1.0f, 0.25f, 0.25f, 1.0f};
constexpr Color NpcColor    = {0.6f, 0.35f, 0.8f, 1.0f};

constexpr Vec2 TileCenter(const i32 tx, const i32 ty) { return {static_cast<f32>(tx) * TileSize, static_cast<f32>(ty) * TileSize}; }
} // namespace

class TriggerDemo : public Scene
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
                      "TriggerDemo: failed to load tile atlases");
        for (i32 ty = -GrassExtent; ty <= GrassExtent; ++ty)
            for (i32 tx = -GrassExtent; tx <= GrassExtent; ++tx)
                tm.SetTile(tx, ty, 0, GrassTileId);
        tm.SetTile(2, -2, 1, WhirlpoolTileId); // Objects layer: the frames are transparent around the swirl

        GetPhysics().SetDebugDraw(true);

        CreatePlayer();
        CreateWarpDoor();
        CreateHazardZone();
        CreateNpc();

        LOG_INFO(Game, "TriggerDemo ready — WASD move, M movement style, F2 colliders, G grid");
    }


    // Sprite-only entity; returns it so callers can attach more components.
    Entity CreateBlock(const char* tag, const Vec2 pos, const Vec2 size, const Color tint, const i32 layer)
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
                                               .Layer   = layer,
                                           });
        return e;
    }

    void AttachCollider(const Entity e, const Collider2D& col)
    {
        GetWorld().AddComponent<Collider2D>(e, col);
        (void)GetPhysics().RegisterCollider(e, col, GetWorld().GetTransform(e)->Position);
    }

    void CreatePlayer()
    {
        _player     = CreateBlock("Player", TileCenter(0, 0), {TileSize * 0.7f, TileSize * 0.7f}, PlayerColor, 0);
        auto& world = GetWorld();
        world.AddComponent<CameraTarget>(_player, CameraTarget{.Lag = 0.05f});
        world.AddComponent<SmoothTileMovement>(_player, SmoothTileMovement{.Speed = WalkSpeed});
        world.AddComponent<InputMovement>(_player, InputMovement{
                                              .MoveLeft = ActionMoveLeft, .MoveRight = ActionMoveRight,
                                              .MoveUp   = ActionMoveUp, .MoveDown    = ActionMoveDown
                                          });
        AttachCollider(_player, Collider2D{
                           .HalfExtents = {TileSize * 0.4f, TileSize * 0.4f},
                           .Kind        = BodyKind::Kinematic,
                           .Layer       = CollisionLayers::Player,
                       });
    }

    // Small trigger at a tile center, so it fires as the player arrives on the
    // tile rather than the moment their edge grazes it.
    void CreateWarpDoor()
    {
        const Entity door = CreateBlock("WarpDoor", TileCenter(5, 0), {TileSize * 0.8f, TileSize * 0.8f},
                                        {0.95f, 0.85f, 0.2f, 1.0f}, -999998);
        AttachCollider(door, Collider2D{
                           .HalfExtents = {TileSize * 0.1f, TileSize * 0.1f},
                           .Kind        = BodyKind::Static,
                           .Layer       = CollisionLayers::Trigger,
                           .Mask        = CollisionLayers::Player,
                           .IsTrigger   = true,
                       });
        (void)CreateBlock("WarpLanding", TileCenter(-5, -4), {TileSize * 0.8f, TileSize * 0.8f},
                          {0.3f, 0.6f, 0.95f, 0.5f}, -999998);

        GetWorld().AddComponent<TriggerCallback>(door, TriggerCallback{
                                                     .OnEnter = [this](Entity, const Entity other) {
                                                         LOG_INFO(Game, "Warp door: whoosh");
                                                         Teleport(other, TileCenter(-5, -4));
                                                     },
                                                 });
    }

    void CreateHazardZone()
    {
        const Vec2   center = {TileSize * 1.0f, TileSize * 3.5f}; // covers tiles x 0..2, y 3..4
        const Vec2   size   = {TileSize * 3.0f, TileSize * 2.0f};
        const Entity zone   = CreateBlock("Hazard", center, size, {0.9f, 0.2f, 0.15f, 0.35f}, -999998);
        AttachCollider(zone, Collider2D{
                           .HalfExtents = {size.X * 0.5f, size.Y * 0.5f},
                           .Kind        = BodyKind::Static,
                           .Layer       = CollisionLayers::Trigger,
                           .Mask        = CollisionLayers::Player,
                           .IsTrigger   = true,
                       });
        GetWorld().AddComponent<TriggerCallback>(zone, TriggerCallback{
                                                     .OnEnter = [this](Entity, const Entity other) {
                                                         SetTint(other, HurtColor);
                                                         _hazardTicks = 0;
                                                     },
                                                     .OnStay = [this](Entity, Entity) {
                                                         if (++_hazardTicks % HazardLogInterval == 0)
                                                             LOG_INFO(Game, "Hazard: ouch (tick {})", _hazardTicks);
                                                     },
                                                     .OnExit = [this](Entity, const Entity other) { SetTint(other, PlayerColor); },
                                                 });
    }

    void CreateNpc()
    {
        const Vec2   pos = TileCenter(-3, 0);
        const Entity npc = CreateBlock("Npc", pos, {TileSize * 0.8f, TileSize * 0.8f}, NpcColor,
                                       static_cast<i32>(pos.Y));
        AttachCollider(npc, Collider2D{
                           .HalfExtents = {TileSize * 0.4f, TileSize * 0.4f},
                           .Kind        = BodyKind::Static,
                           .Layer       = CollisionLayers::NPC,
                       });
        GetWorld().AddComponent<CollisionCallback>(npc, CollisionCallback{
                                                       .OnEnter = [this](const Entity self, Entity, const Vec2 normal) {
                                                           LOG_INFO(Game, "NPC: hey! (pushed toward ({:.2f}, {:.2f}))", normal.X, normal.Y);
                                                           SetTint(self, Color::White());
                                                       },
                                                       .OnExit = [this](const Entity self, Entity) { SetTint(self, NpcColor); },
                                                   });
    }

    void SetTint(const Entity e, const Color tint) { if (auto* sr = GetWorld().GetComponent<SpriteRenderer>(e)) sr->Tint = tint; }

    // Move an entity instantly: cancel any in-flight movement, refresh its
    // broadphase record, and cut the camera so it doesn't pan across the map.
    void Teleport(const Entity e, const Vec2 pos)
    {
        auto& world                     = GetWorld();
        world.GetTransform(e)->Position = pos;
        if (auto* stm = world.GetComponent<SmoothTileMovement>(e)) {
            stm->OriginWorld = pos;
            stm->TargetWorld = pos;
            stm->Progress    = 1.0f;
            stm->HasQueued   = false;
        }
        if (auto* fm = world.GetComponent<FreeMovement>(e)) fm->Velocity = {};
        if (auto* pm = world.GetComponent<PendingMove>(e)) pm->DesiredDelta = {};

        PhysicsWorld& physics = GetPhysics();
        physics.UpdateCollider(physics.FindColliderForEntity(e), *world.GetComponent<Collider2D>(e), pos);
        GetCamera().Position = pos;
    }

    // Swap SmoothTileMovement ↔ FreeMovement (+ PendingMove). Going back to
    // tile mode snaps to the nearest tile center so steps stay grid-aligned.
    void ToggleMovementStyle()
    {
        auto& world = GetWorld();
        if (world.GetComponent<SmoothTileMovement>(_player)) {
            world.RemoveComponent<SmoothTileMovement>(_player);
            world.AddComponent<FreeMovement>(_player, FreeMovement{.Speed = FreeSpeed});
            world.AddComponent<PendingMove>(_player, PendingMove{});
            LOG_INFO(Game, "Movement: free");
            return;
        }
        world.RemoveComponent<FreeMovement>(_player);
        world.RemoveComponent<PendingMove>(_player);
        world.AddComponent<SmoothTileMovement>(_player, SmoothTileMovement{.Speed = WalkSpeed});
        const Vec2 p = world.GetTransform(_player)->Position;
        Teleport(_player, {std::round(p.X / TileSize) * TileSize, std::round(p.Y / TileSize) * TileSize});
        LOG_INFO(Game, "Movement: tile");
    }

    void OnUpdate(const f32 /*dt*/) override
    {
        if (GetApp().GetInput().JustPressed(ActionToggleStyle)) ToggleMovementStyle();

        if (GetApp().GetInput().JustPressed(ActionDebugPhysics)) {
            PhysicsWorld& physics = GetPhysics();
            physics.SetDebugDraw(!physics.GetDebugDraw());
        }
        if (GetApp().GetInput().JustPressed(ActionToggleGrid)) _showGrid = !_showGrid;
    }

    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor = {1.0f, 1.0f, 1.0f, 1.0f};
        GetPhysics().CollectDebugDraw(packet, _whiteTex, _sampler);
        if (!_showGrid) return;

        const Camera2D& cam    = GetCamera();
        const Vec2      camPos = cam.GetEffectivePosition();
        const Vec2      halfView{(1920.0f / cam.Zoom) * 0.5f, (1080.0f / cam.Zoom) * 0.5f};
        const AABB      viewAABB  = AABB::FromCenterHalfExtents(camPos, halfView);
        const f32       thickness = std::max(1.0f, 1.0f / cam.Zoom);
        GetTileMap().CollectGridDebugDraw(packet, viewAABB, _whiteTex, _sampler, thickness);
    }

    void OnUnload() override
    {
        if (_whiteTex.IsValid()) GetApp().GetDevice().DestroyTexture(_whiteTex);
        if (_sampler.IsValid()) GetApp().GetDevice().DestroySampler(_sampler);
    }

private:
    SamplerHandle _sampler;
    TextureHandle _whiteTex;
    Entity        _player      = Entity::Invalid();
    u32           _hazardTicks = 0;
    bool          _showGrid    = false;
};

std::unique_ptr<Scene> Demo::CreateTriggerDemo() { return std::make_unique<TriggerDemo>(); }
