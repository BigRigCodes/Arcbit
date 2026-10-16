#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>

#include <memory>
#include <span>

namespace Arcbit
{
class InputManager;
class RenderDevice;
class Scene;
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

// Register every demo's input actions and default bindings, once, app-wide.
// The demos share one InputManager, so no scene registers its own (a reload
// would bind every key twice). No two demos bind a key to different actions.
void RegisterInput(Arcbit::InputManager& input);

// One demo scene in the switcher. Each demo TU defines its Create function.
struct Entry
{
    const char* Name;    // registered scene name, shown in the menu
    const char* Summary; // one line for the menu
    const char* Keys;    // demo-specific keys, shown in the window title
    std::unique_ptr<Arcbit::Scene> (*Create)();
};

// Every demo, in menu order.
[[nodiscard]] std::span<const Entry> Entries();

std::unique_ptr<Arcbit::Scene> CreateWorldDemo();
std::unique_ptr<Arcbit::Scene> CreatePhysicsDemo();
std::unique_ptr<Arcbit::Scene> CreateTileMoveDemo();
std::unique_ptr<Arcbit::Scene> CreateArcDemo();
std::unique_ptr<Arcbit::Scene> CreateTileMoveArcDemo();
std::unique_ptr<Arcbit::Scene> CreateObbDemo();
std::unique_ptr<Arcbit::Scene> CreateTriggerDemo();
} // namespace Demo
