#pragma once

#include <arcbit/assets/TextureManager.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// ScenePreloader - what Scene::OnPrepare loads ahead with, on the loader
// thread.
//
// A scene names the files its OnLoad will use; they're read and decoded here,
// while the previous scene is still running (or the splash is up), instead of
// during the swap. SceneManager hands the decoded images to the
// TextureManager just before OnLoad, so OnLoad's LoadTexture / LoadSpriteSheet
// / TileMap::LoadMap calls only upload them.
//
//   void OnPrepare(ScenePreloader& preload) override
//   {
//       preload.AddTileMap("assets/maps/village.arcmap");
//       preload.AddSpriteSheet("assets/spritesheets/player.json");
//   }
//
// Every method runs on the loader thread and touches only this object and the
// files it reads. A texture named twice, or already on the GPU when the load
// was queued (`loaded`), is decoded once or not at all. Missing files are
// skipped here; OnLoad reports them when it loads for real.
// ---------------------------------------------------------------------------
class ScenePreloader
{
public:
    explicit ScenePreloader(std::unordered_set<std::string> loaded = {}) : _loaded(std::move(loaded)) {}

    // An image file ("builtin:" names are skipped).
    void AddTexture(std::string_view path);

    // A sprite sheet's image (the metadata is small; LoadSpriteSheet reads it).
    void AddSpriteSheet(std::string_view metaPath);

    // Every atlas image an .arcmap uses.
    void AddTileMap(std::string_view mapPath);

    // A .tileatlas.json's image (for atlases registered one by one).
    void AddTileAtlas(std::string_view atlasPath);

    // Normalized paths of every texture named so far, in order.
    [[nodiscard]] const std::vector<std::string>& GetTexturePaths() const { return _paths; }

    // How many images were decoded (named, not already loaded, and readable).
    [[nodiscard]] size_t GetDecodedCount() const { return _images.size(); }

    // The decoded images, keyed by normalized path, for TextureManager::Stage.
    [[nodiscard]] std::unordered_map<std::string, DecodedImage> TakeImages() { return std::move(_images); }

private:
    std::unordered_set<std::string>               _loaded; // on the GPU already: skip
    std::vector<std::string>                      _paths;
    std::unordered_map<std::string, DecodedImage> _images;
};
} // namespace Arcbit
