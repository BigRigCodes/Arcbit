#pragma once

#include <arcbit/core/Types.h>

#include <vector>

namespace Arcbit
{
// A signed distance field made from a coverage bitmap, for glyph outlines
// stb_truetype's own SDF can't read (the cubic curves of CFF / OpenType
// fonts): the glyph is rasterized `upscale` times larger, and this measures
// distances there, then samples them down.
struct SdfBitmap
{
    std::vector<u8> Pixels; // one byte a pixel, stb's convention: onEdge at the outline, more inside
    u32             Width  = 0;
    u32             Height = 0;
};

// coverage: w x h, 0-255, `upscale` times the output size. The output grows
// by padding pixels on every side; distance changes the value by
// distScale per output pixel.
[[nodiscard]] SdfBitmap SdfFromCoverage(const u8* coverage, u32 w, u32 h, u32 upscale, u32 padding, u8 onEdge,
                                        f32 distScale);
} // namespace Arcbit
