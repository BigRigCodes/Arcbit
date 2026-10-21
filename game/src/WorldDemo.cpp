#include "Common.h"

#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>
#include <arcbit/ecs/World.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/ScenePreloader.h>
#include <arcbit/tilemap/TileMap.h>
#include <arcbit/ui/HudScreen.h>
#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/ui/Widgets.h>

#include <cmath>
#include <vector>

#include "arcbit/settings/Settings.h"

using namespace Arcbit;

// ---------------------------------------------------------------------------
// Tile ID constants — GRASS+.png (base=1, 25 cols), WATER+.png (base=401, 12 cols),
//                     floor.jpg (base=601, 4 cols). IDs encode (1 + row*cols + col).
// ---------------------------------------------------------------------------

// GRASS+.png ground tiles
constexpr u32 TileGrassPlain  = 1;   // (0,0)
constexpr u32 TileGrassDark   = 26;  // (0,1)
constexpr u32 TileGrassSav    = 60;  // (9,2) savanna
constexpr u32 TileGrassF1     = 251; // (0,10) plain + single flower
constexpr u32 TileGrassF4     = 253; // (2,10) plain + four flowers
constexpr u32 TileGrassDarkF1 = 276; // (0,11) dark + single flower
constexpr u32 TileGrassDarkF4 = 278; // (2,11) dark + four flowers
// GRASS+.png object tiles
constexpr u32 TileDeadLeaves = 263; // (12,10)
constexpr u32 TileLeaves     = 272; // (21,10)
constexpr u32 TileStick      = 308; // (7,12)
constexpr u32 TileStump      = 312; // (11,12)
constexpr u32 TileLog        = 313; // (12,12)
constexpr u32 TileMushroom   = 338; // (12,13)
constexpr u32 TileRock       = 328; // (2,13)
constexpr u32 TileRock2      = 327; // (1,13)
constexpr u32 TileSmallRock  = 333; // (7,13)
constexpr u32 TileStatue     = 335; // (9,13)
constexpr u32 TileTallGrass  = 293; // (17,11)
// WATER+.png ground tiles
// constexpr u32 TileWaterDeep  = 401; // (0,0)
// constexpr u32 TileWaterMid   = 402; // (1,0)
// constexpr u32 TileWaterLight = 403; // (2,0)
// constexpr u32 TileWaterAlt   = 404; // (3,0)
// constexpr u32 TileWaterFoam  = 405; // (4,0)
// constexpr u32 TileWaterFoam  = 406; // (5,0)
// constexpr u32 TileWaterFoam  = 407; // (6,0)
constexpr u32 TileWaterLight = 569; // light_water.png (single tile atlas)
// WATER+.png object tiles
constexpr u32 TileWaterRock  = 509; // (0,9)
constexpr u32 TileWaterStone = 512; // (3,9)
constexpr u32 TileIceberg    = 517; // (8,9)
// WATER+.png whirlpool frames — row 13, cols 0-1 (col 2 is a character sprite,
// col 3 is empty). The entity uses frame 0 and spins it; the two-frame
// flip-book version lives in water.tileatlas.json (local_id 156).
constexpr u32 TileWhirlpool0 = 401 + 13 * 12 + 0; // (0,13)
// floor.jpg tiles (cracked stone per instructions; rest = smooth)
constexpr u32 TileFloorCrk1 = 601; // (0,0)
constexpr u32 TileFloorCrk2 = 602; // (1,0)
constexpr u32 TileFloorCrk3 = 605; // (0,1)
constexpr u32 TileFloorCrk4 = 606; // (1,1)
constexpr u32 TileFloorCrk5 = 608; // (3,1)
constexpr u32 TileFloorCrk6 = 614; // (1,3)
constexpr u32 TileFloorSm1  = 603; // (2,0)
constexpr u32 TileFloorSm2  = 607; // (2,1)
constexpr u32 TileFloorSm3  = 609; // (0,2)
constexpr u32 TileFloorSm4  = 611; // (2,2)
constexpr u32 TileFloorSm5  = 612; // (3,2)
constexpr u32 TileFloorSm6  = 615; // (2,3)

enum class Facing { Down, Right, Up, Left };

struct ShipInfo
{
    Entity entity;
    Vec2   boundsMin;
    Vec2   boundsMax;
};

// ---------------------------------------------------------------------------
// WorldDemo
// ---------------------------------------------------------------------------

class WorldDemo : public Scene
{
protected:
    // Decode the map's atlases and the sprite images ahead, on the loader
    // thread; OnLoad's loads then only upload them.
    void OnPrepare(ScenePreloader& preload) override
    {
        preload.AddTileMap("assets/tilemaps/demo.arcmap");
        preload.AddSpriteSheet("assets/spritesheets/player.json");
        preload.AddTexture("assets/textures/MiniWorldSprites/Miscellaneous/PirateShip.png");
        preload.AddTexture("assets/textures/MiniWorldSprites/Miscellaneous/Bridge.png");
        preload.AddTexture("assets/textures/ui_panel.png");
    }

    void OnLoad() override
    {
        GetConfig().TileSize = TileSize;
        GetCamera().Zoom     = InitialZoom;

        CreateSamplers();
        SetupTilemap();
        CreatePlayer();
        CreateShips();
        CreateBridges();
        CreateWhirlpool();
        RegisterShipAI();
        RegisterWhirlpoolAI();
        CreateMouseLight();

        _bitmapFont.Load("assets/engine/fonts/Roboto-Regular.ttf", 32.0f, FontMode::Bitmap, GetApp().GetDevice());

        // Load in OnLoad, not OnUpdate: OnLoad runs while the render thread is idle,
        // so creating GPU resources here can't race a frame in flight.
        _uiPanelTex = GetApp().GetTextures().Load("assets/textures/ui_panel.png");

        // The world's own track: it plays while this scene is active, gives way
        // to the battle's when B pushes it, and picks up where it was on return.
        SetMusic("assets/music/demo_theme.wav");

        InitScreens();
    }

    void OnUpdate(const f32 dt) override
    {
        UpdateHud(dt);

        if (GetApp().GetUI().HasBlockingScreen()) return;

        if (GetApp().GetInput().JustPressed(ActionToggleGrid)) _showGrid = !_showGrid;
        if (GetApp().GetInput().JustPressed(ActionInteract)) GetCamera().AddTrauma(0.6f);
        if (GetApp().GetInput().JustPressed(ActionAttack) && !_isAttacking) _isAttacking = true;
        UpdatePlayerVisuals();
        UpdateMouseLight();
    }

    void OnRender(FramePacket& packet) override
    {
        packet.AmbientColor  = Color{0.018f, 0.022f, 0.03f, 1.0f};
        packet.ReferenceSize = {ViewportW, ViewportH};
        if (_showGrid) {
            // Engine-provided tile-grid debug overlay (slice 1, Phase 22B).
            const Camera2D& cam     = GetCamera();
            const Vec2      camPos  = cam.GetEffectivePosition();
            const Vec2      halfView{ (ViewportW / cam.Zoom) * 0.5f,
                                     (ViewportH / cam.Zoom) * 0.5f };
            const AABB      viewAABB  = AABB::FromCenterHalfExtents(camPos, halfView);
            const f32       thickness = std::max(1.0f, 1.0f / cam.Zoom);
            GetTileMap().CollectGridDebugDraw(
                packet, viewAABB, _gridTex, _gridSampler, thickness);
        }

        DrawText(packet, _bitmapFont, "Hello, World!\nThis is a test of a longer string", {100.0f, 100.0f}, 1.0f,
                 Color::Magenta(), 0, TextAlign::Center);
    }

    void OnUnload() override
    {
        // The HUD reads this scene's state, so it leaves with the scene.
        GetApp().GetUI().Remove(_hud);
        _hud = nullptr;

        GetApp().GetDevice().DestroyTexture(_gridTex);
        GetApp().GetDevice().DestroySampler(_gridSampler);
        GetApp().GetDevice().DestroySampler(_linearSampler);
        GetApp().GetDevice().DestroySampler(_sampler);

        _bitmapFont.Release(GetApp().GetDevice());
    }

private:
    // -----------------------------------------------------------------------
    // Setup
    // -----------------------------------------------------------------------


    void CreateSamplers()
    {
        SamplerDesc nd{};
        nd.MinFilter = Filter::Nearest;
        nd.MagFilter = Filter::Nearest;
        nd.AddressU  = AddressMode::Repeat;
        nd.AddressV  = AddressMode::Repeat;
        nd.DebugName = "NearestRepeat";
        _sampler     = GetApp().GetDevice().CreateSampler(nd);
        ARCBIT_ASSERT(_sampler.IsValid(), "Failed to create sampler");

        SamplerDesc ld{};
        ld.MinFilter   = Filter::Linear;
        ld.MagFilter   = Filter::Linear;
        ld.AddressU    = AddressMode::Repeat;
        ld.AddressV    = AddressMode::Repeat;
        ld.DebugName   = "LinearRepeat";
        _linearSampler = GetApp().GetDevice().CreateSampler(ld);
        ARCBIT_ASSERT(_linearSampler.IsValid(), "Failed to create linear sampler");

        TextureDesc gd{};
        gd.Width              = 1;
        gd.Height             = 1;
        gd.Format             = Format::RGBA8_UNorm;
        gd.Usage              = TextureUsage::Sampled | TextureUsage::Transfer;
        gd.DebugName          = "Debug_SolidWhite";
        _gridTex              = GetApp().GetDevice().CreateTexture(gd);
        constexpr u8 white[4] = {255, 255, 255, 255};
        GetApp().GetDevice().UploadTexture(_gridTex, white, sizeof(white));

        SamplerDesc sd{};
        sd.MinFilter = Filter::Nearest;
        sd.MagFilter = Filter::Nearest;
        sd.AddressU  = AddressMode::ClampToEdge;
        sd.AddressV  = AddressMode::ClampToEdge;
        sd.DebugName = "Debug_GridSampler";
        _gridSampler = GetApp().GetDevice().CreateSampler(sd);
    }

    // -----------------------------------------------------------------------
    // Tilemap
    // -----------------------------------------------------------------------

    void SetupTilemap()
    {
        TileMap& tm = GetTileMap();
        tm.SetTileSize(TileSize);

        if (!tm.LoadMap("assets/tilemaps/demo.arcmap", GetApp().GetTextures(), _sampler, _linearSampler)) {
            ARCBIT_ASSERT(
                tm.LoadAtlasJson(1, "assets/tilemaps/grass.tileatlas.json", _sampler, "nearest", GetApp().GetTextures()),
                "Failed to load grass atlas");
            ARCBIT_ASSERT(
                tm.LoadAtlasJson(401, "assets/tilemaps/water.tileatlas.json", _sampler, "nearest", GetApp().GetTextures()),
                "Failed to load water atlas");
            ARCBIT_ASSERT(
                tm.LoadAtlasJson(569, "assets/tilemaps/light_water.tileatlas.json", _sampler, "nearest", GetApp().GetTextures()),
                "Failed to load light_water atlas");
            ARCBIT_ASSERT(
                tm.LoadAtlasJson(601, "assets/tilemaps/floor.tileatlas.json", _linearSampler, "linear", GetApp().GetTextures()),
                "Failed to load floor atlas");

            GenerateGround(tm);
            GenerateWater(tm);
            GenerateStoneFloor(tm);
            GenerateObjects(tm);

            if (!tm.SaveMap("assets/tilemaps/demo.arcmap")) { LOG_WARN(Game, "Failed to save tilemap to disk"); }
        }

        // Always resolve _waterTex from Water+.png (whirlpool frames live there).
        if (const auto* entry = tm.FindAtlas(401))
            _waterTex = entry->Atlas.GetTexture();
        ARCBIT_ASSERT(_waterTex.IsValid(), "Failed to resolve water atlas texture");

        tm.LogStats();
    }

    static u32 HashTile(const i32 x, const i32 y)
    {
        u32 h = static_cast<u32>(x) * 2246822519u ^ static_cast<u32>(y) * 3266489917u;
        h     ^= (h >> 16);
        h     *= 0x45d9f3bu;
        h     ^= (h >> 16);
        return h;
    }

    bool IsWaterZone(const i32 tx, const i32 ty) const
    {
        if (tx >= -12 && tx <= -2 && ty >= 1 && ty <= 9) return true;
        if (tx >= 8 && tx <= 28 && ty >= 5 && ty <= 22) return true;
        return false;
    }

    bool IsFloorZone(const i32 tx, const i32 ty) const { return tx >= 22 && tx <= 35 && ty >= -22 && ty <= -8; }

    bool IsSavannaZone(const i32 tx, const i32 ty) const { return tx >= -35 && tx <= -18 && ty >= -25 && ty <= -10; }

    void GenerateGround(TileMap& tm)
    {
        for (i32 ty = -30; ty <= 30; ++ty) {
            for (i32 tx = -40; tx <= 40; ++tx) {
                if (IsWaterZone(tx, ty) || IsFloorZone(tx, ty)) continue;
                const u32 h   = HashTile(tx, ty);
                const u32 pct = h % 100;
                u32       id  = TileGrassPlain;
                if (IsSavannaZone(tx, ty))
                    id = (pct < 80) ? TileGrassSav : TileGrassDark;
                else if (pct < 45) id = TileGrassPlain;
                else if (pct < 68) id = TileGrassDark;
                else if (pct < 75) id = TileGrassF1;
                else if (pct < 80) id = TileGrassF4;
                else if (pct < 87) id = TileGrassDarkF1;
                else if (pct < 91) id = TileGrassDarkF4;
                tm.SetTile(tx, ty, 0, id);
            }
        }
    }

    void GenerateWater(TileMap& tm)
    {
        for (i32 ty = 1; ty <= 9; ++ty)
            for (i32 tx = -12; tx <= -2; ++tx)
                tm.SetTile(tx, ty, 0, TileWaterLight);

        for (i32 ty = 5; ty <= 22; ++ty)
            for (i32 tx = 8; tx <= 28; ++tx)
                tm.SetTile(tx, ty, 0, TileWaterLight);
    }

    void GenerateStoneFloor(TileMap& tm)
    {
        constexpr u32 cracked[] = {
            TileFloorCrk1, TileFloorCrk2, TileFloorCrk3,
            TileFloorCrk4, TileFloorCrk5, TileFloorCrk6,
        };
        constexpr u32 smooth[] = {
            TileFloorSm1, TileFloorSm2, TileFloorSm3,
            TileFloorSm4, TileFloorSm5, TileFloorSm6,
        };
        for (i32 ty = -22; ty <= -8; ++ty)
            for (i32 tx = 22; tx <= 35; ++tx) {
                const u32 h = HashTile(tx, ty);
                tm.SetTile(tx, ty, 0,
                           (h % 100 < 25) ? cracked[(h >> 8) % 6] : smooth[(h >> 8) % 6]);
            }
    }

    void GenerateObjects(TileMap& tm)
    {
        // Grass objects scattered at ~6% of non-water, non-floor tiles.
        constexpr u32 grassObjs[] = {
            TileStick, TileSmallRock, TileRock2, TileRock, TileMushroom,
            TileLog, TileStump, TileDeadLeaves, TileLeaves, TileTallGrass,
            TileStatue, TileTallGrass, TileSmallRock, TileStick, TileLeaves,
        };
        constexpr u32 ObjCount = sizeof(grassObjs) / sizeof(grassObjs[0]);
        for (i32 ty = -30; ty <= 30; ++ty)
            for (i32 tx = -40; tx <= 40; ++tx) {
                if (IsWaterZone(tx, ty) || IsFloorZone(tx, ty)) continue;
                const u32 h = HashTile(tx + 7, ty + 13);
                if ((h % 100) < 6)
                    tm.SetTile(tx, ty, 1, grassObjs[(h >> 8) % ObjCount]);
            }

        // Water body 1 objects.
        constexpr i32 w1x[] = {-10, -8, -5, -3, -11, -7};
        const i32     w1y[] = {3, 7, 2, 6, 5, 4};
        constexpr u32 w1t[] = {
            TileWaterRock, TileWaterRock, TileWaterStone,
            TileWaterRock, TileIceberg, TileWaterRock,
        };
        for (u32 i = 0; i < 6; ++i) tm.SetTile(w1x[i], w1y[i], 1, w1t[i]);

        // Water body 2 objects.
        const i32     w2x[] = {10, 15, 20, 25, 12, 22, 17};
        const i32     w2y[] = {7, 12, 8, 18, 20, 15, 10};
        constexpr u32 w2t[] = {
            TileWaterRock, TileIceberg, TileWaterRock, TileWaterStone,
            TileWaterRock, TileIceberg, TileWaterRock,
        };
        for (u32 i = 0; i < 7; ++i) tm.SetTile(w2x[i], w2y[i], 1, w2t[i]);
    }

    // -----------------------------------------------------------------------
    // Entities
    // -----------------------------------------------------------------------

    void CreatePlayer()
    {
        _playerSheet = SpriteSheet::Load("assets/spritesheets/player.json", GetApp().GetTextures());
        ARCBIT_ASSERT(_playerSheet.IsValid(), "Failed to load player sprite");

        auto& world                                   = GetWorld();
        _playerEntity                                 = world.CreateEntity();
        world.GetComponent<Tag>(_playerEntity)->Value = "Player";

        auto* t     = world.GetTransform(_playerEntity);
        t->Position = {};
        t->Scale    = _playerSheet.TileWorldSize(TileSize);

        world.AddComponent<SpriteRenderer>(_playerEntity, SpriteRenderer{
                                               .Texture = _playerSheet.GetTexture(), .Sampler = _sampler,
                                           });
        world.AddComponent<Animator>(_playerEntity, Animator{
                                         .Clip    = _playerSheet.GetAnimation("idle_down"),
                                         .Sheet   = &_playerSheet,
                                         .Playing = true,
                                         .OnEvent = [](const std::string_view ev) {
                                             if (ev == "AttackHit")
                                                 AudioManager::PlayOneShot(
                                                     "assets/sfx/sword_slashing.mp3", 0.5f); // a hot file
                                         },
                                     });
        world.AddComponent<CameraTarget>(_playerEntity, CameraTarget{.Lag = 0.12f});

        world.AddComponent<SmoothTileMovement>(_playerEntity, SmoothTileMovement{
                                                   .Speed = WalkTileSpeed,
                                               });
        world.AddComponent<InputMovement>(_playerEntity, InputMovement{.MoveLeft = ActionMoveLeft, .MoveRight = ActionMoveRight,
                                                                       .MoveUp   = ActionMoveUp, .MoveDown    = ActionMoveDown});
        world.AddComponent<MoveIntent>(_playerEntity); // read by UpdatePlayerVisuals
        // Saved and kept across scene changes: where the player stands.
        world.AddComponent<Persistent>(_playerEntity, Persistent{ .Id = "player", .Components = { "Transform2D" } });

        world.AddComponent<AudioSource>(_playerEntity, AudioSource{
                                            .Path = "assets/sfx/footsteps.mp3", .Volume = 0.6f,
                                            .Loop = true, .Playing                      = false,
                                            .Category = "sfx.footsteps", // the demo's own (main.cpp)
                                        });

        world.AddComponent<LightEmitter>(_playerEntity, LightEmitter{
                                             .Radius       = 150.0f,
                                             .Intensity    = 1.5f,
                                             .CastsShadows = true
                                         });
    }

    Entity MakeShip(const Vec2 pos, const Vec2 vel, const Vec2 bMin, const Vec2 bMax)
    {
        auto&        world                = GetWorld();
        const Entity e                    = world.CreateEntity();
        world.GetComponent<Tag>(e)->Value = "PirateShip";

        auto* t     = world.GetTransform(e);
        t->Position = pos;
        t->Scale    = {ShipSize, ShipSize};

        world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                               .Texture = _shipTex, .Sampler = _sampler,
                                               .UV      = ShipUVRight,
                                           });
        world.AddComponent<FreeMovement>(e, FreeMovement{
                                             .Velocity = vel, .Friction = 0.0f, .MaxSpeed = ShipMaxSpeed,
                                         });
        // Required pair — FreeMovement writes desired delta into this; the
        // resolver consumes it and updates Transform.
        world.AddComponent<PendingMove>(e, PendingMove{});
        _ships.push_back({e, bMin, bMax});
        return e;
    }

    void CreateShips()
    {
        _shipTex = GetApp().GetTextures().Load(
            "assets/textures/MiniWorldSprites/Miscellaneous/PirateShip.png");
        ARCBIT_ASSERT(_shipTex.IsValid(), "Failed to load PirateShip texture");

        constexpr Vec2 w1Min = {-12.0f * TileSize, 1.0f * TileSize};
        constexpr Vec2 w1Max = {-2.0f * TileSize, 9.0f * TileSize};
        MakeShip({-7.0f * TileSize, 4.0f * TileSize}, {ShipSpeed, 0.0f}, w1Min, w1Max);
        MakeShip({-10.0f * TileSize, 6.0f * TileSize}, {-ShipSpeed, ShipSpeed * 0.5f}, w1Min, w1Max);

        constexpr Vec2 w2Min = {8.0f * TileSize, 5.0f * TileSize};
        constexpr Vec2 w2Max = {28.0f * TileSize, 22.0f * TileSize};
        MakeShip({14.0f * TileSize, 10.0f * TileSize}, {ShipSpeed, ShipSpeed * 0.4f}, w2Min, w2Max);
        MakeShip({22.0f * TileSize, 17.0f * TileSize}, {-ShipSpeed * 0.8f, -ShipSpeed * 0.6f}, w2Min, w2Max);
    }

    void CreateBridges()
    {
        _bridgeTex = GetApp().GetTextures().Load(
            "assets/textures/MiniWorldSprites/Miscellaneous/Bridge.png");
        ARCBIT_ASSERT(_bridgeTex.IsValid(), "Failed to load Bridge texture");

        auto& world      = GetWorld();
        auto  MakeBridge = [&](Vec2 pos, Vec2 scale, UVRect uv) {
            const Entity e                    = world.CreateEntity();
            world.GetComponent<Tag>(e)->Value = "Bridge";
            auto* t                           = world.GetComponent<Transform2D>(e);
            t->Position                       = pos;
            t->Scale                          = scale;
            world.AddComponent<SpriteRenderer>(e, SpriteRenderer{
                                                   .Texture = _bridgeTex, .Sampler = _sampler, .UV = uv,
                                               });
        };

        // Horizontal bridge over east edge of water body 1.
        MakeBridge({-3.5f * TileSize, 5.0f * TileSize}, {BridgeW, BridgeH}, BridgeUVHoriz);
        // Vertical bridge at south edge of water body 2.
        MakeBridge({18.0f * TileSize, 23.5f * TileSize}, {BridgeH, BridgeW}, BridgeUVVert);
    }

    // Whirlpool: 2×2 tile entity inside water body 1; animated via AI system.
    void CreateWhirlpool()
    {
        auto& world                                      = GetWorld();
        _whirlpoolEntity                                 = world.CreateEntity();
        world.GetComponent<Tag>(_whirlpoolEntity)->Value = "Whirlpool";

        auto* t     = world.GetComponent<Transform2D>(_whirlpoolEntity);
        t->Position = {-7.0f * TileSize, 5.5f * TileSize};
        t->Scale    = {TileSize * 2.0f, TileSize * 2.0f};

        world.AddComponent<SpriteRenderer>(_whirlpoolEntity, SpriteRenderer{
                                               .Texture = _waterTex, .Sampler = _sampler,
                                               .UV      = GetTileMap().GetTileUV(TileWhirlpool0),
                                           });
    }

    void CreateMouseLight()
    {
        auto& world                                     = GetWorld();
        _mouseLightEntity                               = world.CreateEntity();
        world.GetTransform(_mouseLightEntity)->Position = {};
        world.AddComponent<LightEmitter>(_mouseLightEntity, LightEmitter{
                                             .Radius     = 75.0f,
                                             .Intensity  = 2.0f,
                                             .LightColor = Color::Cyan()
                                         });
    }

    // -----------------------------------------------------------------------
    // AI systems registered in OnLoad
    // -----------------------------------------------------------------------

    void RegisterShipAI()
    {
        auto ships = _ships;
        GetWorld().RegisterSystem("ShipAI",
                                             [ships](Scene& scene, f32 /*dt*/) mutable {
                                                 auto& world = scene.GetWorld();
                                                 for (auto& info : ships) {
                                                     if (!world.IsValid(info.entity)) continue;
                                                     auto* t  = world.GetComponent<Transform2D>(info.entity);
                                                     auto* fm = world.GetComponent<FreeMovement>(info.entity);
                                                     auto* sr = world.GetComponent<SpriteRenderer>(info.entity);
                                                     if (!t || !fm || !sr) continue;

                                                     if (t->Position.X < info.boundsMin.X + ShipSize * 0.5f) {
                                                         t->Position.X  = info.boundsMin.X + ShipSize * 0.5f;
                                                         fm->Velocity.X = std::abs(fm->Velocity.X);
                                                     }
                                                     if (t->Position.X > info.boundsMax.X - ShipSize * 0.5f) {
                                                         t->Position.X  = info.boundsMax.X - ShipSize * 0.5f;
                                                         fm->Velocity.X = -std::abs(fm->Velocity.X);
                                                     }
                                                     if (t->Position.Y < info.boundsMin.Y + ShipSize * 0.5f) {
                                                         t->Position.Y  = info.boundsMin.Y + ShipSize * 0.5f;
                                                         fm->Velocity.Y = std::abs(fm->Velocity.Y);
                                                     }
                                                     if (t->Position.Y > info.boundsMax.Y - ShipSize * 0.5f) {
                                                         t->Position.Y  = info.boundsMax.Y - ShipSize * 0.5f;
                                                         fm->Velocity.Y = -std::abs(fm->Velocity.Y);
                                                     }

                                                     if (std::abs(fm->Velocity.X) >= std::abs(fm->Velocity.Y))
                                                         sr->UV = (fm->Velocity.X >= 0.0f) ? ShipUVRight : ShipUVLeft;
                                                     else
                                                         sr->UV = (fm->Velocity.Y >= 0.0f) ? ShipUVDown : ShipUVUp;
                                                 }
                                             });
    }

    void RegisterWhirlpoolAI()
    {
        GetWorld().RegisterSystem("WhirlpoolAI",
                                             [wp = _whirlpoolEntity](Scene& scene, const f32 dt) {
                                                 auto& world = scene.GetWorld();
                                                 if (!world.IsValid(wp)) return;
                                                 auto* t = world.GetComponent<Transform2D>(wp);
                                                 if (!t) return;
                                                 constexpr f32 AngularSpeed = 1.5f; // radians per second
                                                 t->Rotation                += AngularSpeed * dt;
                                             });
    }

    // -----------------------------------------------------------------------
    // Player
    // -----------------------------------------------------------------------

    const char* SelectPlayerClip(const bool moving, bool& flipX) const
    {
        flipX = _playerFacing == Facing::Left;
        if (_isAttacking) {
            switch (_playerFacing) {
            case Facing::Down: return "attack_down";
            case Facing::Right:
            case Facing::Left: return "attack_right";
            case Facing::Up: return "attack_up";
            }
        }
        if (moving) {
            switch (_playerFacing) {
            case Facing::Down: return "walk_down";
            case Facing::Right:
            case Facing::Left: return "walk_right";
            case Facing::Up: return "walk_up";
            }
        }
        switch (_playerFacing) {
        case Facing::Down: return "idle_down";
        case Facing::Right:
        case Facing::Left: return "idle_right";
        case Facing::Up: return "idle_up";
        }
        return "idle_down";
    }

    // Facing and walk/idle come from the player's MoveIntent, which
    // InputMovementSystem fills from WASD / arrows. OnUpdate runs before the
    // scene's systems, so this reads the previous tick's intent.
    void UpdatePlayerVisuals()
    {
        auto& world = GetWorld();
        if (!world.IsValid(_playerEntity)) return;
        const auto* stm    = world.GetComponent<SmoothTileMovement>(_playerEntity);
        const auto* intent = world.GetComponent<MoveIntent>(_playerEntity);
        if (!stm || !intent) return;

        const Vec2 f = intent->Facing;
        if (std::abs(f.X) >= std::abs(f.Y)) _playerFacing = f.X < 0.0f ? Facing::Left : Facing::Right;
        else _playerFacing = f.Y < 0.0f ? Facing::Up : Facing::Down;

        UpdatePlayerAnimation(stm->Progress < 1.0f || intent->Direction != Vec2{});
    }

    void UpdatePlayerAnimation(const bool moving)
    {
        auto& world = GetWorld();
        auto* sr    = world.GetComponent<SpriteRenderer>(_playerEntity);
        auto* anim  = world.GetComponent<Animator>(_playerEntity);
        if (!sr || !anim) return;

        if (_isAttacking && anim->Finished) _isAttacking = false;

        bool        flipX    = false;
        const char* clipName = SelectPlayerClip(moving, flipX);
        sr->FlipX            = flipX;

        const AnimationClip* newClip = _playerSheet.GetAnimation(clipName);
        if (newClip != anim->Clip) {
            anim->Clip       = newClip;
            anim->FrameIndex = 0;
            anim->Elapsed    = 0.0f;
            anim->Finished   = false;
        }
        if (auto* audio = world.GetComponent<AudioSource>(_playerEntity))
            audio->Playing = moving && !_isAttacking;
    }

    void UpdateMouseLight()
    {
        if (!GetWorld().IsValid(_mouseLightEntity)) return;
        i32 mouseX = 0, mouseY = 0;
        GetApp().GetInput().GetMousePosition(mouseX, mouseY);
        const Vec2 mouseRef = {
            static_cast<f32>(mouseX) * ViewportW / static_cast<f32>(GetApp().GetWindowWidth()),
            static_cast<f32>(mouseY) * ViewportH / static_cast<f32>(GetApp().GetWindowHeight()),
        };
        const Vec2 world = GetCamera().ScreenToWorld(mouseRef, {ViewportW, ViewportH});
        GetWorld().GetComponent<Transform2D>(_mouseLightEntity)->Position = world;
    }
    
        // -----------------------------------------------------------------------
    // Debug grid
    // -----------------------------------------------------------------------

    // SubmitDebugGrid relocated to TileMap::CollectGridDebugDraw (engine).
    // Caller now passes a precomputed view AABB + thickness; see OnRender.

    // -----------------------------------------------------------------------
    // UI screens
    // -----------------------------------------------------------------------

    void InitScreens()
    {
        GetApp().GetUI().SetSkin(UISkin::LoadFromFile("assets/engine/skins/default.skin.json"));

        auto hud = std::make_unique<HudScreen>();
        _hud = hud.get();
        GetApp().GetUI().Push(std::move(hud));

        // Enable FPS label after Push so the widget pointer is valid.
        _hud->GetFpsLabel()->Visible = Settings::Graphics.ShowFps;
    }

    void UpdateHud(const f32 dt)
    {
        if (!_hud) return;
        _simTime += dt;

        // HP: slow 8-second oscillation between ~10% and 100%.
        _hud->GetHealthBar()->Value = 0.55f + 0.45f * std::sin(_simTime * 0.785f);
        // MP: slightly faster 12-second cycle, offset phase.
        _hud->GetManaBar()->Value   = 0.425f + 0.375f * std::sin(_simTime * 0.524f + 1.0f);

        _hud->GetFpsLabel()->Visible = Settings::Graphics.ShowFps;
        _hud->GetFpsLabel()->Text    = Loc::Format("demo.world.fps", { { "fps", static_cast<i32>(std::round(GetApp().GetFPS())) } });
    }

private:
    static constexpr ActionID ActionToggleGrid = MakeAction("Debug_ToggleGrid");
    static constexpr ActionID ActionMoveLeft   = MakeAction("Move_Left");
    static constexpr ActionID ActionMoveRight  = MakeAction("Move_Right");
    static constexpr ActionID ActionMoveUp     = MakeAction("Move_Up");
    static constexpr ActionID ActionMoveDown   = MakeAction("Move_Down");
    static constexpr ActionID ActionInteract   = MakeAction("Interact");
    static constexpr ActionID ActionSprint     = MakeAction("Sprint");
    static constexpr ActionID ActionAttack     = MakeAction("Player_Attack");

    static constexpr f32 ViewportW     = 1920.0f;
    static constexpr f32 ViewportH     = 1080.0f;
    static constexpr f32 TileSize      = 64.0f;
    static constexpr f32 InitialZoom   = 2.0f;
    static constexpr f32 WalkTileSpeed = 4.0f;   // tiles/sec for smooth tile movement
    static constexpr f32 ShipSize      = 128.0f; // 2×2 tiles
    static constexpr f32 ShipSpeed     = 24.0f;  // ~0.375 tiles/sec
    static constexpr f32 ShipMaxSpeed  = 36.0f;
    static constexpr f32 BridgeW       = 160.0f; // 40px @ 16 PPU = 2.5 tiles
    static constexpr f32 BridgeH       = 192.0f; // 48px @ 16 PPU = 3 tiles

    // PirateShip.png: 128×32, 4 × 32×32 sprites (right, left, down, up).
    static constexpr UVRect ShipUVRight = {0.00f, 0.0f, 0.25f, 1.0f};
    static constexpr UVRect ShipUVLeft  = {0.25f, 0.0f, 0.50f, 1.0f};
    static constexpr UVRect ShipUVDown  = {0.50f, 0.0f, 0.75f, 1.0f};
    static constexpr UVRect ShipUVUp    = {0.75f, 0.0f, 1.00f, 1.0f};

    // Bridge.png: 80×48, two 40×48 sprites (vertical left, horizontal right).
    static constexpr UVRect BridgeUVVert  = {0.0f, 0.0f, 0.5f, 1.0f};
    static constexpr UVRect BridgeUVHoriz = {0.5f, 0.0f, 1.0f, 1.0f};

    bool   _showGrid     = false;
    bool   _isAttacking  = false;
    Facing _playerFacing = Facing::Down;

    Entity _playerEntity     = Entity::Invalid();
    Entity _whirlpoolEntity  = Entity::Invalid();
    Entity _mouseLightEntity = Entity::Invalid();

    std::vector<ShipInfo> _ships;

    TextureHandle _gridTex;
    SamplerHandle _gridSampler;
    SamplerHandle _sampler;
    SamplerHandle _linearSampler;
    TextureHandle _shipTex;
    TextureHandle _bridgeTex;
    TextureHandle _waterTex;
    TextureHandle _uiPanelTex;

    SpriteSheet _playerSheet;

    FontAtlas       _bitmapFont;
    HudScreen*      _hud       = nullptr; // raw; owned by UIManager

    // Simulated HP/MP for demo — oscillate via sine so the bars visibly animate.
    f32 _simTime = 0.0f;
};

std::unique_ptr<Scene> Demo::CreateWorldDemo() { return std::make_unique<WorldDemo>(); }

