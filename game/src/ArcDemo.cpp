// ---------------------------------------------------------------------------
// Phase 22D directional collision demo.
//
// Sister demo to PhysicsDemo (free movement). Three props demonstrate the
// two complementary patterns for "selective passability":
//
//   - **Tree** — small stump collider offset to the bottom of a tall canopy
//     visual. Demonstrates `Collider2D::Offset` for sub-sprite collider
//     placement. *No directional arcs* — the canopy area is just empty
//     space, so walking through it is free. Only the small stump blocks.
//
//   - **Bush** — full-size collider with `BlockedFrom = Horizontal()`.
//     Genuinely directional: blocks E/W approaches, lets N/S pass (you
//     hop over the bush from above/below). Geometry can't express this
//     because both axes are the same shape.
//
//   - **One-way platform** — long thin static box with
//     `BlockedFrom = SouthOnly()`. Player can drop south through it but
//     can't push north into it. Another genuine directional case.
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

constexpr f32 TileSize    = 64.0f;
constexpr u32 GrassTileId = 1;
constexpr i32 GrassExtent = 8;
constexpr f32 PlayerSpeed = 240.0f; // world px/sec
} // namespace

class ArcDemo : public Application
{
public:
    ArcDemo() : Application({
        .Title = "Arcbit — Arc Demo (F2 toggles colliders)",
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
                      "ArcDemo: failed to load tile atlases");

        // Plain grass everywhere — directional behaviour is on the entities,
        // not the tilemap, for this demo.
        for (i32 ty = -GrassExtent; ty <= GrassExtent; ++ty)
            for (i32 tx = -GrassExtent; tx <= GrassExtent; ++tx)
                tm.SetTile(tx, ty, 0, GrassTileId);

        SetupInput();

        PhysicsWorld& physics = GetScene().GetPhysics();
        physics.SetDebugDraw(true);

        auto& world = GetScene().GetWorld();
        CreatePlayer(world, physics);
        CreateTree(world, physics);
        CreateBush(world, physics);
        CreateLedge(world, physics);

        LOG_INFO(Game, "ArcDemo ready — WASD to move, F2 toggles collider overlay");
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

    // Player: blue circle, FreeMovement-driven (so off-axis approaches at
    // arbitrary angles are possible — that's the point of the arc check).
    void CreatePlayer(World& world, PhysicsWorld& physics)
    {
        _player                                 = world.CreateEntity();
        world.GetComponent<Tag>(_player)->Value = "Player";
        auto* t                                 = world.GetTransform(_player);
        t->Position                             = {-TileSize * 4.0f, 0.0f};
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
        (void)physics.RegisterCollider(_player, playerCollider, t->Position);
    }

    // Tree: tall green canopy (no collider) + small brown stump (collider only).
    // The stump uses Collider2D::Offset so the collider sits at the visual's
    // bottom while the entity's Transform stays at the canopy's center —
    // matches how RPG trees are typically authored. No directional arcs:
    // the canopy area has no collider, so walking through it is free; only
    // the small stump blocks, and it blocks all directions normally.
    void CreateTree(World& world, PhysicsWorld& physics)
    {
        const Vec2 canopyPos{0.0f, 0.0f};
        const Vec2 canopySize{TileSize, TileSize * 1.5f};
        const f32  canopyBottom = canopyPos.Y + canopySize.Y * 0.5f;

        // ---- Canopy entity: visual only, no collider --------------------
        const Entity canopy                    = world.CreateEntity();
        world.GetComponent<Tag>(canopy)->Value = "TreeCanopy";
        auto* ct                               = world.GetTransform(canopy);
        ct->Position                           = canopyPos;
        ct->Scale                              = canopySize;
        world.AddComponent<SpriteRenderer>(canopy, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.2f, 0.6f, 0.25f, 1.0f}, // green
                                               .Layer   = static_cast<i32>(canopyBottom),
                                           });

        // ---- Stump entity: small brown sprite + matching collider -------
        const Vec2 stumpSize{TileSize * 0.4f, TileSize * 0.3f};
        const Vec2 stumpHalf{stumpSize.X * 0.5f, stumpSize.Y * 0.5f};
        // Stump sits at the bottom of the canopy in world space.
        const Vec2 stumpPos{canopyPos.X, canopyBottom - stumpHalf.Y};

        const Entity stump                    = world.CreateEntity();
        world.GetComponent<Tag>(stump)->Value = "TreeStump";
        auto* st                              = world.GetTransform(stump);
        st->Position                          = stumpPos;
        st->Scale                             = stumpSize;
        world.AddComponent<SpriteRenderer>(stump, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.45f, 0.3f, 0.15f, 1.0f}, // brown
                                               .Layer   = static_cast<i32>(canopyBottom) + 1,
                                           });
        const Collider2D stumpCol{
            .Shape       = ColliderShape::Box,
            .HalfExtents = stumpHalf,
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Prop,
        };
        world.AddComponent<Collider2D>(stump, stumpCol);
        (void)physics.RegisterCollider(stump, stumpCol, stumpPos);
    }

    // Bush: full-size collider with Horizontal() arcs. Genuinely directional —
    // approaches from E/W block (you can't push through the brambles), but
    // approaches from N/S pass (you hop over from above or below). Geometry
    // can't express this since both axes share the same shape.
    void CreateBush(World& world, PhysicsWorld& physics)
    {
        const Vec2   pos{0.0f, TileSize * 3.5f};
        const Vec2   size{TileSize * 1.2f, TileSize * 1.0f};
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = "Bush";
        auto* t                           = world.GetTransform(e);
        t->Position                       = pos;
        t->Scale                          = size;
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.4f, 0.7f, 0.4f, 1.0f}, // light green
                                               .Layer   = static_cast<i32>(pos.Y + size.Y * 0.5f),
                                           });
        const Collider2D col{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {size.X * 0.5f, size.Y * 0.5f},
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Prop,
            .BlockedFrom = DirectionArc::Horizontal(),
        };
        world.AddComponent<Collider2D>(e, col);
        (void)physics.RegisterCollider(e, col, pos);
    }

    // North-facing cliff edge: a long thin tan box. The player walks UP to
    // the edge from below (south side) and should be stopped from walking
    // off. `BlockedFrom` describes which face of the collider blocks
    // contacts — the player's contact happens at the *south* face of the
    // edge (because they're south, walking north into it), so SouthOnly()
    // is the correct preset. Walking back south leaves the contact normal
    // pointing north → outside the SouthOnly arc → passes.
    void CreateLedge(World& world, PhysicsWorld& physics)
    {
        const Vec2   pos{TileSize * 4.0f, 0.0f};
        const Vec2   size{TileSize * 3.0f, TileSize * 0.5f};
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = "Ledge";
        auto* t                           = world.GetTransform(e);
        t->Position                       = pos;
        t->Scale                          = size;
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = {0.7f, 0.6f, 0.3f, 1.0f},
                                               .Layer   = static_cast<i32>(pos.Y + size.Y * 0.5f),
                                           });
        const Collider2D col{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {size.X * 0.5f, size.Y * 0.5f},
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Prop,
            .BlockedFrom = DirectionArc::SouthOnly(),
        };
        world.AddComponent<Collider2D>(e, col);
        (void)physics.RegisterCollider(e, col, pos);
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

// Entry point — main.cpp calls this when ARCBIT_DEMO_ARC is selected.
void RunArcDemo()
{
    ArcDemo demo;
    demo.Run();
}
