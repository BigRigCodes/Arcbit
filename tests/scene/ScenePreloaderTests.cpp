// ScenePreloader against the shipped demo assets: which images a scene's
// OnPrepare names, that they decode on the CPU with no render device, and the
// load progress they add up to.

#include "TestSupport.h"

#include <arcbit/assets/TextureManager.h>
#include <arcbit/scene/FileScene.h>
#include <arcbit/scene/ScenePreloader.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
// Asset paths inside the game's files are relative to game/ (the runtime's
// working directory), so run from there and restore the old directory after.
class InGameDir
{
public:
    InGameDir() : _previous(std::filesystem::current_path())
    {
        std::filesystem::current_path(ArcbitTests::SourcePath("game"));
    }
    ~InGameDir() { std::filesystem::current_path(_previous); }

    InGameDir(const InGameDir&)            = delete;
    InGameDir& operator=(const InGameDir&) = delete;

private:
    std::filesystem::path _previous;
};

std::string CacheKey(const char* path) { return TextureManager::NormalizePath(path); }
} // namespace

TEST_CASE("A scene file's prepare decodes its tilemap atlases ahead", "[scene][preload][shipped]")
{
    InGameDir      dir;
    FileScene      scene("assets/scenes/physics.arcscene");
    ScenePreloader preload;
    scene.OnPrepare(preload);

    // The arcmap's two atlases; the entities only use builtin:white.
    CHECK(preload.GetTexturePaths() ==
          std::vector<std::string>{ CacheKey("assets/textures/GRASS+.png"), CacheKey("assets/textures/Water+.png") });
    CHECK(preload.GetDecodedCount() == 0); // named only; decoding comes after OnPrepare
    preload.DecodeAll();
    CHECK(preload.GetDecodedCount() == 2);

    for (const auto& [path, image] : preload.TakeImages()) {
        CHECK(image.Width > 0);
        CHECK(image.Height > 0);
        CHECK(image.Pixels.size() == static_cast<size_t>(image.Width) * image.Height * 4);
    }
    CHECK(preload.GetDecodedCount() == 0); // taken
}

TEST_CASE("A sprite sheet's image is resolved next to its metadata", "[scene][preload][shipped]")
{
    InGameDir      dir;
    ScenePreloader preload;
    preload.AddSpriteSheet("assets/spritesheets/player.json");
    CHECK(preload.GetTexturePaths() ==
          std::vector<std::string>{ CacheKey("assets/textures/Cute_Fantasy_Free/Player/Player.png") });
    preload.DecodeAll();
    CHECK(preload.GetDecodedCount() == 1);
}

TEST_CASE("Textures already loaded or named twice decode once or not at all", "[scene][preload][shipped]")
{
    InGameDir      dir;
    ScenePreloader preload({ CacheKey("assets/textures/GRASS+.png") }); // already on the GPU
    preload.AddTexture("assets/textures/GRASS+.png");
    preload.AddTileAtlas("assets/tilemaps/water.tileatlas.json");
    preload.AddTexture("assets/textures/../textures/Water+.png"); // the same file, spelled differently
    preload.AddTexture("builtin:white");

    CHECK(preload.GetTexturePaths() ==
          std::vector<std::string>{ CacheKey("assets/textures/GRASS+.png"), CacheKey("assets/textures/Water+.png") });
    preload.DecodeAll();
    CHECK(preload.GetDecodedCount() == 1); // only the water
}

TEST_CASE("Missing files are skipped by the preloader", "[scene][preload]")
{
    ScenePreloader preload;
    preload.AddTexture("no/such/image.png");
    preload.AddSpriteSheet("no/such/sheet.json");
    preload.AddTileMap("no/such/map.arcmap");
    preload.AddTileAtlas("no/such/atlas.tileatlas.json");
    preload.DecodeAll();
    CHECK(preload.GetDecodedCount() == 0);
    CHECK_THAT(preload.GetProgress(), WithinAbs(0.95f, 1e-5f)); // nothing to decode counts as done
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

TEST_CASE("Progress runs through the prepare and decode shares, leaving the build share", "[scene][preload]")
{
    ScenePreloader preload;
    CHECK(preload.GetProgress() == 0.0f);
    CHECK_THAT(preload.GetBuildShare(), WithinAbs(0.05f, 1e-5f)); // the default split

    preload.SetShares(1.0f, 2.0f, 1.0f); // normalized: 25% / 50% / 25%
    preload.ReportPrepareProgress(0.5f);
    CHECK_THAT(preload.GetProgress(), WithinAbs(0.125f, 1e-5f));
    CHECK_THAT(preload.GetBuildShare(), WithinAbs(0.25f, 1e-5f));

    preload.DecodeAll(); // no images: the decode share completes at once
    CHECK_THAT(preload.GetProgress(), WithinAbs(0.75f, 1e-5f));
}

TEST_CASE("Decoding progress is weighted by file size", "[scene][preload][shipped]")
{
    InGameDir      dir;
    ScenePreloader preload;
    preload.AddTexture("assets/textures/GRASS+.png");
    preload.AddTexture("assets/textures/Water+.png");
    const auto grass = static_cast<f32>(std::filesystem::file_size("assets/textures/GRASS+.png"));
    const auto water = static_cast<f32>(std::filesystem::file_size("assets/textures/Water+.png"));
    const f32  afterGrass = 0.95f * grass / (grass + water);

    // Stretched over a second so the step between the two images is visible.
    auto decoding = std::async(std::launch::async, [&] { preload.DecodeAll(1.0f); });
    f32  last     = 0.0f;
    bool sawStep  = false;
    while (decoding.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
        const f32 now = preload.GetProgress();
        CHECK(now >= last); // never backwards
        if (std::abs(now - afterGrass) < 0.01f) sawStep = true;
        last = now;
    }
    CHECK(sawStep); // grass done, water not: the bar sits at grass's share of the bytes
    CHECK_THAT(preload.GetProgress(), WithinAbs(0.95f, 1e-5f));
}

TEST_CASE("A scene file's load block sets the stage shares", "[scene][preload]")
{
    const ArcbitTests::TempDir tmp;
    const std::string          path = tmp.Write("hinted.arcscene", R"({
        "version": 1,
        "load": { "shares": { "prepare": 1, "decode": 6, "build": 3 }, "textures": [] },
        "entities": {}
    })");
    FileScene      scene(path);
    ScenePreloader preload;
    scene.OnPrepare(preload);
    CHECK_THAT(preload.GetBuildShare(), WithinAbs(0.3f, 1e-5f));
    preload.DecodeAll();
    CHECK_THAT(preload.GetProgress(), WithinAbs(0.7f, 1e-5f));
}
