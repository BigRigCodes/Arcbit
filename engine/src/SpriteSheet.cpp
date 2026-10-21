#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// File-local helpers
// ---------------------------------------------------------------------------
namespace {

// Open and parse a JSON file. Returns nullopt and logs on any failure.
std::optional<nlohmann::json> ParseJsonFile(std::string_view path)
{
    std::ifstream file{ std::string(path) };
    if (!file.is_open())
    {
        LOG_ERROR(Engine, "SpriteSheet: cannot open '{}'", path);
        return std::nullopt;
    }
    try
    {
        nlohmann::json j;
        file >> j;
        return j;
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_ERROR(Engine, "SpriteSheet: JSON parse error in '{}': {}", path, e.what());
        return std::nullopt;
    }
}

// The spec (docs/sprite-format.md) requires rejecting unrecognized versions:
// any "1" / "1.x" string is accepted, everything else — missing, non-string,
// or another major — is refused rather than silently misread.
bool HasSupportedVersion(const nlohmann::json& json, const std::string_view source)
{
    const auto it = json.find("version");
    if (it == json.end())
    {
        LOG_ERROR(Engine, "SpriteSheet: '{}' is missing the 'version' field - "
                          "use the explicit Carrot import path for non-Arcbit files", source);
        return false;
    }

    const std::string_view major = SpriteSheet::SupportedMajorVersion;
    if (it->is_string())
    {
        const std::string& v = it->get_ref<const std::string&>();
        if (v == major || (v.starts_with(major) && v.size() > major.size() && v[major.size()] == '.'))
            return true;
    }
    LOG_ERROR(Engine, "SpriteSheet: '{}' has unsupported version {} (expected {}.x)", source, it->dump(), major);
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

SpriteSheet SpriteSheet::Load(const std::string_view metaPath, TextureManager& textures)
{
    return Load(metaPath, textures, [&](const std::string_view path) { return textures.Load(path); });
}

SpriteSheet SpriteSheet::Load(const std::string_view metaPath, TextureManager& textures,
                              const std::function<TextureHandle(std::string_view)>& loadTexture)
{
    const auto jsonOpt = ParseJsonFile(metaPath);
    if (!jsonOpt)
        return {};
    const auto& json = *jsonOpt;

    // Validate before touching the texture so a bad file never uploads one.
    if (!HasSupportedVersion(json, metaPath))
        return {};

    const std::string texPath = TexturePathOf(metaPath, json);
    if (texPath.empty())
    {
        LOG_ERROR(Engine, "SpriteSheet: missing 'texture' field in '{}'", metaPath);
        return {};
    }

    const TextureHandle texture = loadTexture(texPath);
    if (!texture.IsValid())
        return {};

    const auto [texW, texH] = textures.GetInfo(texture);
    return FromJson(json, texture, texW, texH, metaPath);
}

std::string SpriteSheet::TexturePathOf(const std::string_view metaPath, const nlohmann::json& json)
{
    const auto it = json.find("texture");
    if (it == json.end() || !it->is_string()) return {};
    const std::filesystem::path metaDir = std::filesystem::path(metaPath).parent_path();
    return (metaDir / it->get<std::string>()).lexically_normal().string();
}

SpriteSheet SpriteSheet::FromJson(const nlohmann::json& json, const TextureHandle texture,
                                  const u32 texW, const u32 texH, const std::string_view sourceName)
{
    if (!HasSupportedVersion(json, sourceName) || !texture.IsValid())
        return {};
    if (texW == 0 || texH == 0)
    {
        LOG_ERROR(Engine, "SpriteSheet: zero-dimension texture for '{}'", sourceName);
        return {};
    }

    SpriteSheet sheet{};
    sheet._texture = texture;
    // 0 means "absent — defer to WorldConfig.TileSize at render time".
    sheet._pixelsPerUnit    = json.value("pixels_per_unit", 0.0f);
    sheet._texturePixelSize = { static_cast<f32>(texW), static_cast<f32>(texH) };

    const f32 invW = 1.0f / static_cast<f32>(texW);
    const f32 invH = 1.0f / static_cast<f32>(texH);

    LoadFrames    (sourceName, json, sheet, invW, invH);
    LoadTileGrid  (sourceName, json, sheet, texW, texH, invW, invH);
    LoadAnimations(sourceName, json, sheet);

    return sheet;
}

// ---------------------------------------------------------------------------
// Parse helpers
// ---------------------------------------------------------------------------

void SpriteSheet::LoadFrames(std::string_view path, const nlohmann::json& json,
                             SpriteSheet& sheet, const f32 invW, const f32 invH)
{
    if (!json.contains("frames"))
        return;

    for (const auto& f : json["frames"])
    {
        const std::string name  = f.at("name").get<std::string>();
        const auto&       rect  = f.at("rect");
        const f32 x = rect.at("x").get<f32>();
        const f32 y = rect.at("y").get<f32>();
        const f32 w = rect.at("w").get<f32>();
        const f32 h = rect.at("h").get<f32>();

        SpriteFrame frame{};
        frame.UV = { x * invW, y * invH, (x + w) * invW, (y + h) * invH };

        if (f.contains("pivot"))
        {
            frame.Pivot.X = f["pivot"].value("x", 0.5f);
            frame.Pivot.Y = f["pivot"].value("y", 0.5f);
        }

        sheet._frames[name] = frame;
    }

    LOG_DEBUG(Engine, "SpriteSheet: loaded {} frames from '{}'", sheet._frames.size(), path);
}

void SpriteSheet::LoadTileGrid(std::string_view path, const nlohmann::json& json,
                               SpriteSheet& sheet, const u32 texW, const u32 texH,
                               const f32 invW, const f32 invH)
{
    if (!json.contains("tile_grid"))
        return;

    const auto& grid  = json["tile_grid"];
    const u32   tileW = grid.at("tile_width").get<u32>();
    const u32   tileH = grid.at("tile_height").get<u32>();
    const u32   cols  = texW / tileW;
    const u32   rows  = texH / tileH;

    if (cols == 0 || rows == 0)
    {
        LOG_WARN(Engine, "SpriteSheet: tile size {}x{} exceeds texture {}x{} in '{}'",
                 tileW, tileH, texW, texH, path);
        return;
    }

    sheet._columns       = cols;
    sheet._tilePixelSize = { static_cast<f32>(tileW), static_cast<f32>(tileH) };
    sheet._tiles.reserve(cols * rows);

    for (u32 row = 0; row < rows; ++row)
        for (u32 col = 0; col < cols; ++col)
        {
            const f32 px = static_cast<f32>(col * tileW);
            const f32 py = static_cast<f32>(row * tileH);
            sheet._tiles.push_back({
                px * invW,
                py * invH,
                (px + static_cast<f32>(tileW)) * invW,
                (py + static_cast<f32>(tileH)) * invH,
            });
        }

    LOG_DEBUG(Engine, "SpriteSheet: built {}x{} tile grid ({} tiles) from '{}'",
              cols, rows, sheet._tiles.size(), path);
}

void SpriteSheet::LoadAnimations(std::string_view path, const nlohmann::json& json,
                                 SpriteSheet& sheet)
{
    if (!json.contains("animations"))
        return;

    for (const auto& a : json["animations"])
    {
        AnimationClip clip{};
        clip.Name = a.at("name").get<std::string>();
        clip.Loop = a.value("loop", false);

        for (const auto& f : a.at("frames"))
        {
            AnimationFrameRef ref{};
            ref.FrameName  = f.at("frame").get<std::string>();
            ref.DurationMs = f.at("duration_ms").get<u32>();
            if (f.contains("events"))
                for (const auto& ev : f["events"])
                    ref.Events.push_back(ev.get<std::string>());
            clip.Frames.push_back(std::move(ref));
        }

        const std::string clipName = clip.Name;
        sheet._animations[clipName] = std::move(clip);
    }

    LOG_DEBUG(Engine, "SpriteSheet: loaded {} animations from '{}'", sheet._animations.size(), path);
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

Vec2 SpriteSheet::ToWorldSize(const f32 pixelW, const f32 pixelH, const f32 worldTileSize) const
{
    const f32 ppu = _pixelsPerUnit > 0.0f ? _pixelsPerUnit : worldTileSize;
    return { (pixelW / ppu) * worldTileSize, (pixelH / ppu) * worldTileSize };
}

std::optional<SpriteFrame> SpriteSheet::GetFrame(const std::string_view name) const
{
    const auto it = _frames.find(std::string(name));
    if (it == _frames.end())
        return std::nullopt;
    return it->second;
}

const AnimationClip* SpriteSheet::GetAnimation(const std::string_view name) const
{
    const auto it = _animations.find(std::string(name));
    if (it == _animations.end())
        return nullptr;
    return &it->second;
}

std::optional<UVRect> SpriteSheet::GetTile(const u32 index) const
{
    if (index >= _tiles.size())
        return std::nullopt;
    return _tiles[index];
}

std::optional<UVRect> SpriteSheet::GetTile(const u32 x, const u32 y) const
{
    if (_columns == 0 || x >= _columns)
        return std::nullopt;
    return GetTile(y * _columns + x);
}

} // namespace Arcbit
