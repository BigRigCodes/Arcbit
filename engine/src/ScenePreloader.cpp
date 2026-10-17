#include <arcbit/scene/ScenePreloader.h>

#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/tilemap/TileMap.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace Arcbit
{
void ScenePreloader::AddTexture(const std::string_view path)
{
    if (path.empty() || path.starts_with("builtin:")) return;
    std::string key = TextureManager::NormalizePath(path);
    if (std::ranges::find(_paths, key) == _paths.end()) _paths.push_back(std::move(key));
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

void ScenePreloader::SetShares(f32 prepare, f32 decode, f32 build)
{
    prepare         = std::max(prepare, 0.0f);
    decode          = std::max(decode, 0.0f);
    build           = std::max(build, 0.0f);
    const f32 total = prepare + decode + build;
    if (total <= 0.0f) return;
    _prepareShare = prepare / total;
    _decodeShare  = decode / total;
    _buildShare   = build / total;
}

void ScenePreloader::ReportPrepareProgress(const f32 fraction)
{
    _prepareProgress = std::clamp(fraction, 0.0f, 1.0f);
}

f32 ScenePreloader::GetProgress() const
{
    const f32 prepare = _decoding ? 1.0f : _prepareProgress.load();
    const u64 total   = _bytesTotal;
    f32       decode  = 0.0f;
    if (_decoded) decode = 1.0f;
    else if (total > 0) decode = static_cast<f32>(static_cast<f64>(_bytesDone) / static_cast<f64>(total));
    return _prepareShare * prepare + _decodeShare * decode;
}

void ScenePreloader::DecodeAll(const f32 debugDelay)
{
    // Weigh every image still to decode by its size on disk first, so the
    // progress has its total before the first decode.
    std::vector<std::pair<std::string, u64>> work;
    u64                                      total = 0;
    for (const std::string& path : _paths) {
        if (_loaded.contains(path)) continue;
        std::error_code ec;
        const u64       bytes = std::filesystem::file_size(path, ec);
        if (ec) continue; // missing: OnLoad reports it
        work.emplace_back(path, bytes);
        total += bytes;
    }
    _bytesTotal = total;
    _decoding   = true;

    for (const auto& [path, bytes] : work) {
        if (std::optional<DecodedImage> image = TextureManager::Decode(path)) _images.emplace(path, std::move(*image));
        if (debugDelay > 0.0f && total > 0)
            std::this_thread::sleep_for(std::chrono::duration<f64>(debugDelay * static_cast<f64>(bytes) / total));
        _bytesDone += bytes;
    }
    if (debugDelay > 0.0f && total == 0) {
        // Nothing to decode (all loaded already): stretch in even steps, so
        // the bar still moves while the delay runs.
        constexpr u64 Steps = 20;
        _bytesTotal         = Steps;
        for (u64 i = 0; i < Steps; ++i) {
            std::this_thread::sleep_for(std::chrono::duration<f32>(debugDelay / Steps));
            ++_bytesDone;
        }
    }
    _decoded = true;
}
} // namespace Arcbit
