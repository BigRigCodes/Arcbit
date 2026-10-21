#pragma once

#include <arcbit/assets/AssetTypes.h>
#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>

#include <string_view>

namespace Arcbit {

class TextureManager;

// Wraps a single grid-based texture atlas.
// Loads the texture and converts (col, row) tile coordinates to normalized UV rects.
class TileAtlas
{
public:
    // Load the atlas texture. tilePixelW/H is the pixel size of one tile cell.
    // Returns false if the texture could not be loaded.
    bool Load(std::string_view texturePath, u32 tilePixelW, u32 tilePixelH,
              TextureManager& textures);

    [[nodiscard]] bool          IsValid()       const { return _texture.IsValid(); }
    [[nodiscard]] TextureHandle GetTexture()    const { return _texture; }
    [[nodiscard]] u32           Columns()       const { return _columns; }
    [[nodiscard]] u32           Rows()          const { return _rows; }
    [[nodiscard]] Vec2          TilePixelSize() const { return _tilePixelSize; }

    // UV rect for the tile at (col, row). No bounds check is performed.
    [[nodiscard]] UVRect GetUV(u32 col, u32 row) const;

private:
    TextureHandle _texture;
    u32           _columns      = 0;
    u32           _rows         = 0;
    f32           _invTexWidth  = 0.0f;
    f32           _invTexHeight = 0.0f;
    Vec2          _tilePixelSize = {};
};

} // namespace Arcbit
