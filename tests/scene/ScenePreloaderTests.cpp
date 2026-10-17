// ScenePreloader against the shipped demo assets: which images a scene's
// OnPrepare names, and that they decode on the CPU with no render device.

#include "TestSupport.h"

#include <arcbit/assets/TextureManager.h>
#include <arcbit/scene/FileScene.h>
#include <arcbit/scene/ScenePreloader.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace Arcbit;

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
    CHECK(preload.GetDecodedCount() == 1); // only the water
}

TEST_CASE("Missing files are skipped by the preloader", "[scene][preload]")
{
    ScenePreloader preload;
    preload.AddTexture("no/such/image.png");
    preload.AddSpriteSheet("no/such/sheet.json");
    preload.AddTileMap("no/such/map.arcmap");
    preload.AddTileAtlas("no/such/atlas.tileatlas.json");
    CHECK(preload.GetDecodedCount() == 0);
}
