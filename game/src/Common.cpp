#include "Common.h"

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/tilemap/TileMap.h>

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
} // namespace Demo
