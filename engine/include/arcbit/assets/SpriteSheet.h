#pragma once

#include <arcbit/assets/AssetTypes.h>
#include <arcbit/core/Math.h>
#include <arcbit/render/RenderHandle.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace Arcbit
{

class TextureManager;

// ---------------------------------------------------------------------------
// SpriteFrame
//
// A single named frame from the sprite sheet: UV coordinates into the atlas
// and a normalised pivot point used to position the sprite in world space.
//
// Common pivot values:
//   { 0.5, 0.5 } — centre (default)
//   { 0.5, 1.0 } — bottom-centre (standing characters)
//   { 0.0, 0.0 } — top-left
// ---------------------------------------------------------------------------
struct SpriteFrame
{
    UVRect UV;
    Vec2   Pivot = { 0.5f, 0.5f };
};

// ---------------------------------------------------------------------------
// AnimationFrameRef / AnimationClip
//
// An AnimationClip is a named sequence of frames with per-frame durations,
// loaded from the "animations" section of the sprite format JSON.
// The Animator component (Phase 17) will drive SpriteRenderer.UV from these.
// ---------------------------------------------------------------------------
struct AnimationFrameRef
{
    std::string              FrameName;
    u32                      DurationMs = 100;
    std::vector<std::string> Events;    // named events fired when this frame becomes active
};

struct AnimationClip
{
    std::string                    Name;
    bool                           Loop = false;
    std::vector<AnimationFrameRef> Frames;
};

// ---------------------------------------------------------------------------
// SpriteSheet
//
// Loads a sprite atlas from the Arcbit sprite format JSON (docs/sprite-format.md).
// One file may define any combination of:
//
//   frames     — named rectangular regions with optional pivot points.
//   tile_grid  — uniform grid of equal-sized tiles indexed left-to-right,
//                top-to-bottom starting at 0.
//   animations — named sequences of frames with per-frame durations.
//
// The "version" field is required; files without it are rejected so that
// non-Arcbit files (e.g. Carrot format) are caught at load time rather than
// silently misread. Use the explicit Carrot import path for those files.
//
// Pixel coordinates are converted to normalised (0-1) UVs using the loaded
// texture's dimensions — callers never need raw pixel math.
// ---------------------------------------------------------------------------
class SpriteSheet
{
public:
    // Load a spritesheet from an Arcbit sprite format JSON file.
    // The texture path inside the JSON is resolved relative to metaPath's
    // directory. Returns an invalid SpriteSheet on any error — check IsValid().
    [[nodiscard]] static SpriteSheet Load(std::string_view metaPath, TextureManager& textures);

    // Build a spritesheet from already-parsed sprite format JSON and an
    // already-loaded texture of texW × texH pixels. Load() is file I/O +
    // texture upload + this; it is public so the format can be parsed and
    // validated without a render device (tests, editor tooling). sourceName
    // is only used in log messages. Returns an invalid SpriteSheet when the
    // version is missing or unsupported, or the texture is invalid / empty.
    [[nodiscard]] static SpriteSheet FromJson(const nlohmann::json& json, TextureHandle texture,
                                              u32 texW, u32 texH, std::string_view sourceName = "<json>");

    // The format's major version this loader understands. "1", "1.0", "1.3"
    // are accepted; "2.0" or a non-string version is rejected.
    static constexpr std::string_view SupportedMajorVersion = "1";

    [[nodiscard]] bool          IsValid()          const { return _texture.IsValid(); }
    [[nodiscard]] TextureHandle GetTexture()       const { return _texture; }
    [[nodiscard]] Vec2          TexturePixelSize() const { return _texturePixelSize; }

    // Raw pixels_per_unit value from the JSON (0 means "use world tile size").
    [[nodiscard]] f32 PixelsPerUnit() const { return _pixelsPerUnit; }

    // Convert a pixel size to world-space size using this sheet's pixels_per_unit.
    // worldTileSize comes from WorldConfig.TileSize (or a hardcoded constant pre-ECS).
    // If pixels_per_unit is absent from the JSON (stored as 0), worldTileSize is used
    // as the denominator — a sprite whose pixel size equals the tile size renders as
    // exactly 1 tile, which is the natural default.
    //
    // Examples with worldTileSize = 32:
    //   32×32 sprite, pixels_per_unit = 0  → (32/32)*32 = 32  world px  (1 tile)
    //   32×32 sprite, pixels_per_unit = 32 → (32/32)*32 = 32  world px  (1 tile, explicit)
    //   64×64 sprite, pixels_per_unit = 64 → (64/64)*32 = 32  world px  (hi-res, still 1 tile)
    //   64×64 sprite, pixels_per_unit = 32 → (64/32)*32 = 64  world px  (intentional 2-tile sprite)
    [[nodiscard]] Vec2 ToWorldSize(f32 pixelW, f32 pixelH, f32 worldTileSize) const;

    // Look up a named frame (from the "frames" section).
    // Returns std::nullopt if the name is not found.
    [[nodiscard]] std::optional<SpriteFrame> GetFrame(std::string_view name) const;

    // Look up a named animation clip (from the "animations" section).
    // Returns nullptr if the name is not found.
    [[nodiscard]] const AnimationClip* GetAnimation(std::string_view name) const;

    // Look up a tile by grid index (0-based, left-to-right, top-to-bottom).
    // Returns std::nullopt if the index is out of range or no tile_grid is defined.
    [[nodiscard]] std::optional<UVRect> GetTile(u32 index) const;

    // Look up a tile by 2D grid coordinates (column, row), both 0-based.
    [[nodiscard]] std::optional<UVRect> GetTile(u32 x, u32 y) const;

    [[nodiscard]] u32  TileCount()      const { return static_cast<u32>(_tiles.size()); }
    [[nodiscard]] u32  TileColumns()    const { return _columns; }
    [[nodiscard]] u32  TileRows()       const { return _columns > 0 ? static_cast<u32>(_tiles.size()) / _columns : 0; }
    [[nodiscard]] Vec2 TilePixelSize()  const { return _tilePixelSize; }

    // Convenience: world-space size of one tile using this sheet's pixels_per_unit.
    [[nodiscard]] Vec2 TileWorldSize(f32 worldTileSize) const
    {
        return ToWorldSize(_tilePixelSize.X, _tilePixelSize.Y, worldTileSize);
    }

private:
    static void LoadFrames    (std::string_view path, const nlohmann::json& json, SpriteSheet& sheet, f32 invW, f32 invH);
    static void LoadTileGrid  (std::string_view path, const nlohmann::json& json, SpriteSheet& sheet, u32 texW, u32 texH, f32 invW, f32 invH);
    static void LoadAnimations(std::string_view path, const nlohmann::json& json, SpriteSheet& sheet);

private:
    TextureHandle _texture;
    f32           _pixelsPerUnit    = 1.0f;
    Vec2          _texturePixelSize = { 0.0f, 0.0f };
    Vec2          _tilePixelSize    = { 0.0f, 0.0f };

    std::unordered_map<std::string, SpriteFrame>   _frames;
    std::unordered_map<std::string, AnimationClip> _animations;
    std::vector<UVRect>                             _tiles;
    u32                                             _columns = 0;
};

} // namespace Arcbit
