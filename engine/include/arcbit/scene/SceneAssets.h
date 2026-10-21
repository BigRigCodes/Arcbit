#pragma once

#include <arcbit/input/InputTypes.h>
#include <arcbit/render/RenderHandle.h>

#include <memory>
#include <string>
#include <string_view>

namespace Arcbit
{
class ItemLibrary;
class SpriteSheet;
class TileMap;

// ---------------------------------------------------------------------------
// SceneAssets - how scene files turn asset references into handles and back.
//
// Scene files name assets by path ("assets/textures/chest.png", "builtin:white"),
// samplers by preset ("nearest", "linear"), and input actions by name. Loading
// resolves those names; saving turns handles back into names. FileScene
// implements this on top of the application (and the scene's own
// reference-counted texture / sprite sheet ownership); tests implement it
// without a GPU so the format can be round-trip tested headless.
//
// Reverse lookups return an empty string for a handle they don't know.
// ---------------------------------------------------------------------------
class SceneAssets
{
public:
    virtual ~SceneAssets() = default;

    [[nodiscard]] virtual TextureHandle LoadTexture(std::string_view path)           = 0;
    [[nodiscard]] virtual std::string   TexturePath(TextureHandle texture) const     = 0;
    [[nodiscard]] virtual SamplerHandle Sampler(std::string_view name)               = 0;
    [[nodiscard]] virtual std::string   SamplerName(SamplerHandle sampler) const     = 0;
    [[nodiscard]] virtual const SpriteSheet* LoadSpriteSheet(std::string_view path)  = 0;
    [[nodiscard]] virtual std::string   SpriteSheetPath(const SpriteSheet* sheet) const = 0;
    [[nodiscard]] virtual std::string   ActionName(ActionID action) const            = 0;

    // Load an .arcmap into `tilemap` (atlases, tile size, chunks).
    [[nodiscard]] virtual bool LoadTileMap(TileMap& tilemap, std::string_view path) = 0;

    // The game's items (a Container's bag reads them); null without a game.
    [[nodiscard]] virtual const ItemLibrary* Items() const { return nullptr; }
};

class Scene;

// SceneAssets on top of a scene hosted by an Application: the scene's own
// textures and sprite sheets, the app's samplers and white texture - what
// FileScene and saved scene state read through.
[[nodiscard]] std::unique_ptr<SceneAssets> MakeAppSceneAssets(Scene& scene);
} // namespace Arcbit
