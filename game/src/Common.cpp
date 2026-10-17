#include "Common.h"

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/ecs/World.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/physics/PhysicsCallbacks.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/ScenePreloader.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/tilemap/TileMap.h>

#include <vector>

namespace Demo
{
using namespace Arcbit;

SamplerHandle CreateNearestRepeatSampler(RenderDevice& device)
{
    SamplerDesc desc{};
    desc.MinFilter = Filter::Nearest;
    desc.MagFilter = Filter::Nearest;
    desc.AddressU  = AddressMode::Repeat;
    desc.AddressV  = AddressMode::Repeat;
    desc.DebugName = "Demo_NearestRepeat";
    const SamplerHandle sampler = device.CreateSampler(desc);
    ARCBIT_ASSERT(sampler.IsValid(), "Demo::CreateNearestRepeatSampler failed");
    return sampler;
}

TextureHandle CreateWhiteTexture(RenderDevice& device)
{
    TextureDesc desc{};
    desc.Width     = 1;
    desc.Height    = 1;
    desc.Format    = Format::RGBA8_UNorm;
    desc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    desc.DebugName = "Demo_WhiteTexture";
    const TextureHandle tex     = device.CreateTexture(desc);
    constexpr u8        white[4] = { 255, 255, 255, 255 };
    device.UploadTexture(tex, white, sizeof(white));
    return tex;
}

void PreloadGrassAndWaterAtlases(ScenePreloader& preload)
{
    preload.AddTileAtlas("assets/tilemaps/grass.tileatlas.json");
    preload.AddTileAtlas("assets/tilemaps/water.tileatlas.json");
}

bool LoadGrassAndWaterAtlases(TileMap& tilemap, TextureManager& textures,
                              const SamplerHandle nearestSampler)
{
    if (!tilemap.LoadAtlasJson(1, "assets/tilemaps/grass.tileatlas.json",
                                nearestSampler, "nearest", textures)) {
        LOG_ERROR(Game, "Demo: failed to load grass atlas");
        return false;
    }
    if (!tilemap.LoadAtlasJson(401, "assets/tilemaps/water.tileatlas.json",
                                nearestSampler, "nearest", textures)) {
        LOG_ERROR(Game, "Demo: failed to load water atlas");
        return false;
    }
    return true;
}
namespace
{
    struct ActionDef
    {
        const char*      Name;
        const char*      DisplayName;
        const char*      Category;
        std::vector<Key> Keys;
    };

    // Union of what the demos used when each was its own app.
    const std::vector<ActionDef>& ActionDefs()
    {
        static const std::vector<ActionDef> defs = {
            { "Move_Left", "Move Left", "Movement", { Key::A, Key::Left } },
            { "Move_Right", "Move Right", "Movement", { Key::D, Key::Right } },
            { "Move_Up", "Move Up", "Movement", { Key::W, Key::Up } },
            { "Move_Down", "Move Down", "Movement", { Key::S, Key::Down } },
            { "Interact", "Interact", "Gameplay", { Key::E, Key::Enter } },
            { "Sprint", "Sprint", "Gameplay", { Key::LeftShift } },
            { "Player_Attack", "Attack", "Gameplay", { Key::Space } },
            { "Pause", "Pause", "System", { Key::Escape } },
            { "Debug_Physics", "Toggle Physics Debug", "Debug", { Key::F2 } },
            { "Debug_ToggleGrid", "Toggle Grid", "Debug", { Key::G } },
            { "Debug_WidgetTest", "Widget Test", "Debug", { Key::T } },
            { "Demo_ToggleMovementStyle", "Toggle Movement Style", "Demo", { Key::M } },
            { "Demo_ToggleGate", "Pause Gate", "Demo", { Key::R } },
            { "Demo_TogglePlayerShape", "Toggle Player Shape", "Demo", { Key::C } },
            { "Demo_Menu", "Demo Menu", "Demo", { Key::F1 } },
            { "Demo_Next", "Next Demo", "Demo", { Key::PageDown } },
            { "Demo_Prev", "Previous Demo", "Demo", { Key::PageUp } },
            { "Demo_Battle", "Push / Pop Battle", "Demo", { Key::B } },
            { "Demo_EventLog", "Event Log", "Debug", { Key::F3 } },
            { "Demo_QuickSave", "Quicksave", "Demo", { Key::F5 } },
            { "Demo_QuickLoad", "Quickload", "Demo", { Key::F9 } },
        };
        return defs;
    }
} // namespace

namespace
{
    // Kicks back whatever touches it: the scene-file version of PhysicsDemo's
    // bumper, whose behavior is a CollisionCallback set up in C++.
    struct DemoBumper
    {
        f32 Kick = 700.0f; // px/s impulse along the contact normal
    };

    // Give each DemoBumper its callback the first tick it exists. (Collected
    // first: adding a component mid-query isn't safe.)
    void WireBumpers(Scene& scene)
    {
        World&              world = scene.GetWorld();
        std::vector<Entity> fresh;
        world.Query<DemoBumper>().Without<CollisionCallback>().ForEach(
            [&](const Entity e, DemoBumper&) { fresh.push_back(e); });

        for (const Entity e : fresh)
            world.AddComponent<CollisionCallback>(e, CollisionCallback{
                .OnEnter = [&scene](const Entity self, const Entity other, const Vec2 normal) {
                    const DemoBumper* bumper = scene.GetWorld().GetComponent<DemoBumper>(self);
                    FreeMovement*     fm     = scene.GetWorld().GetComponent<FreeMovement>(other);
                    if (bumper && fm) fm->AddImpulse(normal * -bumper->Kick); // normal points at the bumper
                },
            });
    }

    // F2 toggles collider outlines - what the C++ demos do in OnUpdate.
    void ToggleDebugDraw(Scene& scene)
    {
        const InputManager* input = scene.GetInput();
        if (!input || !input->JustPressed(MakeAction("Debug_Physics"))) return;
        PhysicsWorld& physics = scene.GetPhysics();
        physics.SetDebugDraw(!physics.GetDebugDraw());
    }
} // namespace

void RegisterSceneTypes(SceneRegistry& registry)
{
    registry.RegisterComponent<DemoBumper>(
        "DemoBumper", [](DemoBumper& c, const nlohmann::json& j, SceneAssets&) { c.Kick = j.value("kick", c.Kick); },
        [](const DemoBumper& c, nlohmann::json& j, const SceneAssets&) { j["kick"] = c.Kick; });
    registry.RegisterSystem("DemoBumpers", [](Scene& scene) {
        scene.GetWorld().RegisterSystem("DemoBumpers", [](Scene& s, f32) { WireBumpers(s); });
    });
    registry.RegisterSystem("DemoDebugKeys", [](Scene& scene) {
        scene.GetWorld().RegisterSystem("DemoDebugKeys", [](Scene& s, f32) { ToggleDebugDraw(s); });
    });
}

void RegisterInput(InputManager& input)
{
    for (const ActionDef& def : ActionDefs()) {
        const ActionID id = MakeAction(def.Name);
        input.RegisterAction(id, def.Name, def.DisplayName, def.Category);
        for (const Key key : def.Keys) input.BindKey(id, key);
    }

    input.BindGamepadButton(MakeAction("Interact"), GamepadButton::South);
    input.BindGamepadButton(MakeAction("Pause"), GamepadButton::Start); // the pause / start menu
    input.BindGamepadButton(MakeAction("Sprint"), GamepadButton::RightShoulder);
    // One half of each stick axis per direction — a whole axis on both
    // actions would press both whenever the stick moved, and they'd cancel.
    input.BindGamepadAxis(MakeAction("Move_Left"), GamepadAxis::LeftX, AxisHalf::Negative);
    input.BindGamepadAxis(MakeAction("Move_Right"), GamepadAxis::LeftX, AxisHalf::Positive);
    input.BindGamepadAxis(MakeAction("Move_Up"), GamepadAxis::LeftY, AxisHalf::Negative);
    input.BindGamepadAxis(MakeAction("Move_Down"), GamepadAxis::LeftY, AxisHalf::Positive);
}
} // namespace Demo
