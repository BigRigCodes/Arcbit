#include "Common.h"

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/input/InputManager.h>
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
        };
        return defs;
    }
} // namespace

void RegisterInput(InputManager& input)
{
    for (const ActionDef& def : ActionDefs()) {
        const ActionID id = MakeAction(def.Name);
        input.RegisterAction(id, def.Name, def.DisplayName, def.Category);
        for (const Key key : def.Keys) input.BindKey(id, key);
    }

    input.BindGamepadButton(MakeAction("Interact"), GamepadButton::South);
    input.BindGamepadButton(MakeAction("Sprint"), GamepadButton::RightShoulder);
    // One half of each stick axis per direction — a whole axis on both
    // actions would press both whenever the stick moved, and they'd cancel.
    input.BindGamepadAxis(MakeAction("Move_Left"), GamepadAxis::LeftX, AxisHalf::Negative);
    input.BindGamepadAxis(MakeAction("Move_Right"), GamepadAxis::LeftX, AxisHalf::Positive);
    input.BindGamepadAxis(MakeAction("Move_Up"), GamepadAxis::LeftY, AxisHalf::Negative);
    input.BindGamepadAxis(MakeAction("Move_Down"), GamepadAxis::LeftY, AxisHalf::Positive);
}
} // namespace Demo
