#include <arcbit/scene/ScenePreloader.h>

#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/tilemap/TileMap.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace Arcbit
{
void ScenePreloader::AddTexture(const std::string_view path)
{
    if (path.empty() || path.starts_with("builtin:")) return;
    std::string key = TextureManager::NormalizePath(path);
    if (std::ranges::find(_paths, key) != _paths.end()) return;
    _paths.push_back(key);

    if (_loaded.contains(key)) return;
    if (std::optional<DecodedImage> image = TextureManager::Decode(key))
        _images.emplace(std::move(key), std::move(*image));
}

void ScenePreloader::AddSpriteSheet(const std::string_view metaPath)
{
    std::ifstream f{ std::string(metaPath) };
    if (!f) return;
    const nlohmann::json meta = nlohmann::json::parse(f, nullptr, false);
    if (meta.is_object()) AddTexture(SpriteSheet::TexturePathOf(metaPath, meta));
}

void ScenePreloader::AddTileMap(const std::string_view mapPath)
{
    for (const std::string& path : TileMap::TexturePathsOf(mapPath)) AddTexture(path);
}

void ScenePreloader::AddTileAtlas(const std::string_view atlasPath)
{
    AddTexture(TileMap::AtlasTexturePathOf(atlasPath));
}
} // namespace Arcbit
