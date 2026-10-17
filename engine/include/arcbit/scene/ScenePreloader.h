#pragma once

#include <arcbit/assets/TextureManager.h>

#include <atomic>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// ScenePreloader - what Scene::OnPrepare loads ahead with, on the loader
// thread, and how far along that load is.
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
// A load runs in three stages, each a share of the progress bar:
//
//   prepare  OnPrepare itself. Naming assets is quick; a scene doing heavy
//            work of its own here (generating a world) claims a share with
//            SetShares and reports through ReportPrepareProgress.
//   decode   The named images, decoded after OnPrepare returns - weighted by
//            file size, so one 8k texture counts for more than a small tile.
//   build    OnLoad on the main thread. Nothing redraws while it runs, so
//            the bar waits at the start of this share until the scene is in.
//
// Without SetShares, decode is 95% and build 5%. Scene files can carry
// measured shares (FileScene reads the optional "load" block).
//
// The Add* and Report* methods run on the loader thread and touch only this
// object and the files they read. A texture named twice, or already on the
// GPU when the load was queued (`loaded`), is decoded once or not at all.
// Missing files are skipped; OnLoad reports them when it loads for real.
// GetProgress may be called from any thread.
// ---------------------------------------------------------------------------
class ScenePreloader
{
public:
    explicit ScenePreloader(std::unordered_set<std::string> loaded = {}) : _loaded(std::move(loaded)) {}

    // --- Naming assets (OnPrepare) -------------------------------------------

    // An image file ("builtin:" names are skipped).
    void AddTexture(std::string_view path);

    // A sprite sheet's image (the metadata is small; LoadSpriteSheet reads it).
    void AddSpriteSheet(std::string_view metaPath);

    // Every atlas image an .arcmap uses.
    void AddTileMap(std::string_view mapPath);

    // A .tileatlas.json's image (for atlases registered one by one).
    void AddTileAtlas(std::string_view atlasPath);

    // --- Progress -------------------------------------------------------------

    // How the load splits between the three stages (any positive scale;
    // normalized here). Call from OnPrepare, before heavy work of its own.
    void SetShares(f32 prepare, f32 decode, f32 build);

    // How far OnPrepare's own work is, 0..1.
    void ReportPrepareProgress(f32 fraction);

    // The whole load so far, 0..1 - at most 1 - build share, since the build
    // stage runs after the preloader is done.
    [[nodiscard]] f32 GetProgress() const;

    // The build stage's share of the bar (what's left once decoding is done).
    [[nodiscard]] f32 GetBuildShare() const { return _buildShare.load(); }

    // --- Decoding (SceneManager, after OnPrepare) -----------------------------

    // Decode every named image not already loaded. `debugDelay` stretches it
    // by that many seconds in total, spread by file size
    // (SceneManager::PrepareDelay), to watch the bar move.
    void DecodeAll(f32 debugDelay = 0.0f);

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

    // Progress, read from the main thread while the loader writes it.
    std::atomic<f32>  _prepareShare{ 0.0f };
    std::atomic<f32>  _decodeShare{ 0.95f };
    std::atomic<f32>  _buildShare{ 0.05f };
    std::atomic<f32>  _prepareProgress{ 0.0f };
    std::atomic<bool> _decoding{ false }; // OnPrepare has returned
    std::atomic<bool> _decoded{ false };
    std::atomic<u64>  _bytesTotal{ 0 };
    std::atomic<u64>  _bytesDone{ 0 };
};
} // namespace Arcbit
