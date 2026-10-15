// ---------------------------------------------------------------------------
// Phase 22E rotated-collider (OBB) demo.
//
// Sister demo to ArcDemo (free movement). Four rotated props:
//
//   - **Diagonal wall** — long thin box at 30°. Walking into it slides along
//     the diagonal; the contact normal is the wall's face normal.
//
//   - **Chest** — box at 20°. Blocks on its real outline, so the corners of
//     its bounding square are walkable.
//
//   - **Plank** — 45°, a few pixels thick. Walk through the empty triangles
//     beside it; pre-22E its whole bounding square blocked.
//
//   - **Spinning gate** — slowly rotating box with NorthOnly() arcs. The
//     orange strip marks the blocking face; the arc turns with the gate.
//     There is no depenetration pass, so the gate stalls rather than rotate
//     into the player.
//
// Keys: WASD move, R pauses/resumes the gate, C toggles the player collider
// between Box and Circle, F2 toggles collider outlines, G toggles the grid.
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
#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/OBB.h>
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
constexpr ActionID ActionToggleGate   = MakeAction("Demo_ToggleGate");
constexpr ActionID ActionToggleShape  = MakeAction("Demo_TogglePlayerShape");

constexpr f32 TileSize       = 64.0f;
constexpr u32 GrassTileId    = 1;
constexpr i32 GrassExtent    = 9;
constexpr f32 PlayerSpeed    = 240.0f;
constexpr f32 PlayerHalf     = TileSize * 0.3f;
constexpr f32 GateSpinRate   = 0.5f;            // radians per second
constexpr f32 GateStripThick = TileSize * 0.08f;

// Everything a static prop needs; rotation applies to both sprite and collider.
struct PropDesc
{
    const char*               Name;
    Vec2                      Position;
    Vec2                      Size;
    f32                       RotationDegrees;
    Color                     Tint;
    std::vector<DirectionArc> BlockedFrom = {};
};
} // namespace

class ObbDemo : public Application
{
public:
    ObbDemo() : Application({
        .Title = "Arcbit — OBB Demo (R gate, C player shape, F2 colliders)",
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
                      "ObbDemo: failed to load tile atlases");
        for (i32 ty = -GrassExtent; ty <= GrassExtent; ++ty)
            for (i32 tx = -GrassExtent; tx <= GrassExtent; ++tx)
                tm.SetTile(tx, ty, 0, GrassTileId);

        SetupInput();

        PhysicsWorld& physics = GetScene().GetPhysics();
        physics.SetDebugDraw(true);

        auto& world = GetScene().GetWorld();
        CreatePlayer(world, physics);
        CreateProps(world, physics);
        CreateGate(world, physics);

        LOG_INFO(Game, "ObbDemo ready — WASD move, R gate, C player shape, F2 colliders, G grid");
    }

    void SetupInput()
    {
        auto& input = GetInput();
        input.RegisterAction(ActionMoveLeft, "Move_Left", "Move Left", "Movement");
        input.RegisterAction(ActionMoveRight, "Move_Right", "Move Right", "Movement");
        input.RegisterAction(ActionMoveUp, "Move_Up", "Move Up", "Movement");
        input.RegisterAction(ActionMoveDown, "Move_Down", "Move Down", "Movement");
        input.RegisterAction(ActionDebugPhysics, "Debug_Physics", "Toggle Physics Debug", "Debug");
        input.RegisterAction(ActionToggleGrid, "Debug_ToggleGrid", "Toggle Grid", "Debug");
        input.RegisterAction(ActionToggleGate, "Demo_ToggleGate", "Pause Gate", "Debug");
        input.RegisterAction(ActionToggleShape, "Demo_TogglePlayerShape", "Toggle Player Shape", "Debug");

        input.BindKey(ActionMoveLeft, Key::A);
        input.BindKey(ActionMoveRight, Key::D);
        input.BindKey(ActionMoveUp, Key::W);
        input.BindKey(ActionMoveDown, Key::S);
        input.BindKey(ActionDebugPhysics, Key::F2);
        input.BindKey(ActionToggleGrid, Key::G);
        input.BindKey(ActionToggleGate, Key::R);
        input.BindKey(ActionToggleShape, Key::C);
    }

    // Player: blue square, FreeMovement-driven. Starts as a Box collider; C
    // swaps to a Circle of the same half-size.
    void CreatePlayer(World& world, PhysicsWorld& physics)
    {
        _player                                 = world.CreateEntity();
        world.GetComponent<Tag>(_player)->Value = "Player";
        auto* t                                 = world.GetTransform(_player);
        t->Position                             = { -TileSize * 4.0f, 0.0f };
        t->Scale                                = { PlayerHalf * 2.0f, PlayerHalf * 2.0f };
        world.AddComponent<SpriteRenderer>(_player, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = { 0.3f, 0.5f, 1.0f, 1.0f },
                                           });
        world.AddComponent<CameraTarget>(_player, CameraTarget{ .Lag = 0.05f });
        world.AddComponent<FreeMovement>(_player, FreeMovement{ .Friction = 0.0f });
        world.AddComponent<PendingMove>(_player, PendingMove{});

        const Collider2D col{
            .Shape       = ColliderShape::Box,
            .HalfExtents = { PlayerHalf, PlayerHalf },
            .Radius      = PlayerHalf,
            .Kind        = BodyKind::Kinematic,
            .Layer       = CollisionLayers::Player,
        };
        world.AddComponent<Collider2D>(_player, col);
        (void)physics.RegisterCollider(_player, col, t->Position);
    }

    void CreateProps(World& world, PhysicsWorld& physics)
    {
        const PropDesc props[] = {
            { "DiagonalWall", { 0.0f, -TileSize * 3.0f }, { TileSize * 6.0f, TileSize * 0.4f }, 30.0f,
              { 0.55f, 0.35f, 0.3f, 1.0f } },
            { "Chest", { TileSize * 3.0f, TileSize * 1.0f }, { TileSize * 1.2f, TileSize * 0.8f }, 20.0f,
              { 0.6f, 0.4f, 0.15f, 1.0f } },
            { "Plank", { -TileSize * 1.0f, TileSize * 3.0f }, { TileSize * 3.0f, TileSize * 0.1f }, 45.0f,
              { 0.8f, 0.7f, 0.45f, 1.0f } },
        };
        for (const PropDesc& desc : props) (void)CreateProp(world, physics, desc);
    }

    // Static rotated box: sprite + matching collider, both rotated.
    Entity CreateProp(World& world, PhysicsWorld& physics, const PropDesc& desc)
    {
        const f32    rotation             = desc.RotationDegrees * DegToRad;
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = desc.Name;
        auto* t                           = world.GetTransform(e);
        t->Position                       = desc.Position;
        t->Scale                          = desc.Size;
        t->Rotation                       = rotation;
        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = desc.Tint,
                                               .Layer   = static_cast<i32>(desc.Position.Y),
                                           });
        const Collider2D col{
            .Shape       = ColliderShape::Box,
            .Rotation    = rotation,
            .HalfExtents = { desc.Size.X * 0.5f, desc.Size.Y * 0.5f },
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Prop,
            .BlockedFrom = desc.BlockedFrom,
        };
        world.AddComponent<Collider2D>(e, col);
        (void)physics.RegisterCollider(e, col, desc.Position);
        return e;
    }

    // Gate: a prop with NorthOnly() arcs plus a visual-only strip on its local
    // north (-Y) face — the face whose contacts block.
    void CreateGate(World& world, PhysicsWorld& physics)
    {
        const PropDesc gate{ "Gate", { TileSize * 5.0f, -TileSize * 2.0f }, { TileSize * 2.0f, TileSize * 0.5f },
                             0.0f, { 0.45f, 0.45f, 0.5f, 1.0f }, DirectionArc::NorthOnly() };
        _gate = CreateProp(world, physics, gate);

        _gateStrip                                 = world.CreateEntity();
        world.GetComponent<Tag>(_gateStrip)->Value = "GateStrip";
        world.GetTransform(_gateStrip)->Scale      = { gate.Size.X, GateStripThick };
        world.AddComponent<SpriteRenderer>(_gateStrip, SpriteRenderer{
                                               .Texture = _whiteTex,
                                               .Sampler = _sampler,
                                               .Tint    = { 1.0f, 0.55f, 0.1f, 1.0f },
                                               .Layer   = static_cast<i32>(gate.Position.Y) + 1,
                                           });
        PlaceGateStrip();
    }

    // Keep the strip glued to the gate's local north face.
    void PlaceGateStrip()
    {
        auto&             world   = GetScene().GetWorld();
        const Collider2D& gateCol = *world.GetComponent<Collider2D>(_gate);
        const Vec2        gatePos = world.GetTransform(_gate)->Position;
        const OBB         box     = OBB::FromCollider(gateCol, gatePos);
        auto*             strip   = world.GetTransform(_gateStrip);
        strip->Position           = box.Center + box.ToWorldDir({ 0.0f, -box.HalfExtents.Y + GateStripThick * 0.5f });
        strip->Rotation           = gateCol.Rotation;
    }

    // Advance the gate's rotation unless the next pose would overlap the
    // player — there's no depenetration, so rotating into them would trap them.
    void SpinGate(const f32 dt)
    {
        auto&             world     = GetScene().GetWorld();
        Collider2D&       gateCol   = *world.GetComponent<Collider2D>(_gate);
        Transform2D&      gateT     = *world.GetTransform(_gate);
        const Collider2D& playerCol = *world.GetComponent<Collider2D>(_player);
        const Vec2        playerPos = world.GetTransform(_player)->Position;

        Collider2D next = gateCol;
        next.Rotation   = std::remainder(gateCol.Rotation + GateSpinRate * dt, TwoPi);
        if (Narrowphase::Overlap(next, gateT.Position, playerCol, playerPos)) return;

        gateCol.Rotation = next.Rotation;
        gateT.Rotation   = next.Rotation;
        PhysicsWorld& physics = GetScene().GetPhysics();
        physics.UpdateCollider(physics.FindColliderForEntity(_gate), gateCol, gateT.Position);
        PlaceGateStrip();
    }

    void TogglePlayerShape()
    {
        auto&       world = GetScene().GetWorld();
        Collider2D& col   = *world.GetComponent<Collider2D>(_player);
        col.Shape         = col.Shape == ColliderShape::Box ? ColliderShape::Circle : ColliderShape::Box;

        PhysicsWorld& physics = GetScene().GetPhysics();
        physics.UpdateCollider(physics.FindColliderForEntity(_player), col, world.GetTransform(_player)->Position);
        LOG_INFO(Game, "Player collider: {}", col.Shape == ColliderShape::Box ? "Box" : "Circle");
    }

    void OnUpdate(const f32 dt) override
    {
        auto& input = GetInput();

        Vec2 dir{};
        if (input.IsPressed(ActionMoveLeft)) dir.X -= 1.0f;
        if (input.IsPressed(ActionMoveRight)) dir.X += 1.0f;
        if (input.IsPressed(ActionMoveUp)) dir.Y -= 1.0f;
        if (input.IsPressed(ActionMoveDown)) dir.Y += 1.0f;
        GetScene().GetWorld().GetComponent<FreeMovement>(_player)->Velocity = dir * PlayerSpeed;

        if (input.JustPressed(ActionToggleGate)) {
            _gateSpinning = !_gateSpinning;
            LOG_INFO(Game, "Gate {}", _gateSpinning ? "spinning" : "paused");
        }
        if (_gateSpinning) SpinGate(dt);

        if (input.JustPressed(ActionToggleShape)) TogglePlayerShape();
        if (input.JustPressed(ActionDebugPhysics)) {
            PhysicsWorld& physics = GetScene().GetPhysics();
            physics.SetDebugDraw(!physics.GetDebugDraw());
        }
        if (input.JustPressed(ActionToggleGrid)) _showGrid = !_showGrid;
    }

    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        GetScene().GetPhysics().CollectDebugDraw(packet, _whiteTex, _sampler);
        if (!_showGrid) return;

        const Camera2D& cam    = GetScene().GetCamera();
        const Vec2      camPos = cam.GetEffectivePosition();
        const Vec2      halfView{ (1920.0f / cam.Zoom) * 0.5f, (1080.0f / cam.Zoom) * 0.5f };
        const AABB      viewAABB  = AABB::FromCenterHalfExtents(camPos, halfView);
        const f32       thickness = std::max(1.0f, 1.0f / cam.Zoom);
        GetScene().GetTileMap().CollectGridDebugDraw(packet, viewAABB, _whiteTex, _sampler, thickness);
    }

    void OnShutdown() override
    {
        if (_whiteTex.IsValid()) GetDevice().DestroyTexture(_whiteTex);
        if (_sampler.IsValid()) GetDevice().DestroySampler(_sampler);
    }

private:
    SamplerHandle _sampler;
    TextureHandle _whiteTex;
    Entity        _player       = Entity::Invalid();
    Entity        _gate         = Entity::Invalid();
    Entity        _gateStrip    = Entity::Invalid();
    bool          _gateSpinning = true;
    bool          _showGrid     = false;
};

// Entry point — main.cpp calls this when ARCBIT_DEMO_OBB is selected.
void RunObbDemo()
{
    ObbDemo demo;
    demo.Run();
}
