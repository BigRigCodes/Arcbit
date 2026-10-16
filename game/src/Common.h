#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>

namespace Arcbit
{
class RenderDevice;
class TextureManager;
class TileMap;
} // namespace Arcbit

// Helpers shared across demo scenes living in game/src/. Intentionally tiny —
// each new demo is meant to be readable end-to-end, so we extract only the
// boilerplate every demo would otherwise duplicate.
namespace Demo
{
// Create a "Nearest, Repeat" sampler suitable for pixel-art tile atlases.
[[nodiscard]] Arcbit::SamplerHandle CreateNearestRepeatSampler(Arcbit::RenderDevice& device);

// Create a 1×1 white texture used by debug-draw outline emitters and any other
// solid-color sprite path. Caller owns it and must DestroyTexture in shutdown.
[[nodiscard]] Arcbit::TextureHandle CreateWhiteTexture(Arcbit::RenderDevice& device);

// Load the engine's stock grass and water tile atlases into the supplied
// TileMap with the given sampler. Returns false on any load failure.
//
// Layout matches the world demo:
//   base 1   → grass.tileatlas.json   (GRASS+.png)
//   base 401 → water.tileatlas.json   (WATER+.png — required for the
//                                      water-tile collision demo)
[[nodiscard]] bool LoadGrassAndWaterAtlases(Arcbit::TileMap& tilemap,
                                            Arcbit::TextureManager& textures,
                                            Arcbit::SamplerHandle nearestSampler);
} // namespace Demo
