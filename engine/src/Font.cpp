#include <arcbit/render/Font.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/RenderDevice.h>
#include <arcbit/core/Log.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace Arcbit {

static constexpr u32 AtlasSize       = 512;
static constexpr u32 SdfPadding      = 4;
static constexpr u32 GlyphPadding    = 1;   // pixels between glyphs in the atlas
static constexpr u32 FirstCodepoint  = 32;
static constexpr u32 LastCodepoint   = 126;

bool FontAtlas::Load(const std::string_view ttfPath, const f32 pixelSize,
                     const FontMode mode, RenderDevice& device)
{
    std::ifstream f(std::string(ttfPath), std::ios::binary | std::ios::ate);
    if (!f) {
        LOG_WARN(Engine, "FontAtlas: cannot open '{}'", ttfPath);
        return false;
    }
    const auto fileSize = static_cast<std::streamsize>(f.tellg());
    f.seekg(0);
    std::vector<u8> ttfData(static_cast<size_t>(fileSize));
    f.read(reinterpret_cast<char*>(ttfData.data()), fileSize);

    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, ttfData.data(), 0)) {
        LOG_WARN(Engine, "FontAtlas: stbtt_InitFont failed for '{}'", ttfPath);
        return false;
    }

    const f32 scale = stbtt_ScaleForPixelHeight(&font, pixelSize);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    _ascent     = static_cast<f32>(ascent) * scale;
    _lineHeight = static_cast<f32>(ascent - descent + lineGap) * scale;
    _mode       = mode;

    // RGBA8 atlas buffer, zeroed — transparent (Bitmap) / zero-distance (SDF).
    std::vector<u8> atlas(AtlasSize * AtlasSize * 4, 0);

    u32 cursorX   = GlyphPadding;
    u32 cursorY   = GlyphPadding;
    u32 rowHeight = 0;

    for (u32 cp = FirstCodepoint; cp <= LastCodepoint; ++cp)
    {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        u8* bitmap = nullptr;

        if (mode == FontMode::SDF)
            bitmap = stbtt_GetCodepointSDF(&font, scale, static_cast<int>(cp),
                                           SdfPadding, 128, 64.0f, &w, &h, &xoff, &yoff);
        else
            bitmap = stbtt_GetCodepointBitmap(&font, scale, scale,
                                              static_cast<int>(cp), &w, &h, &xoff, &yoff);

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(cp), &advance, &lsb);

        GlyphInfo g{};
        g.BitmapSize = { static_cast<f32>(w), static_cast<f32>(h) };
        g.Bearing    = { static_cast<f32>(xoff), static_cast<f32>(yoff) };
        g.Advance    = static_cast<f32>(advance) * scale;

        if (bitmap && w > 0 && h > 0)
        {
            if (cursorX + static_cast<u32>(w) + GlyphPadding > AtlasSize) {
                cursorX = GlyphPadding;
                cursorY += rowHeight + GlyphPadding;
                rowHeight = 0;
            }

            if (cursorY + static_cast<u32>(h) + GlyphPadding > AtlasSize) {
                LOG_WARN(Engine, "FontAtlas: atlas 512x512 full at codepoint {} - increase AtlasSize", cp);
                if (mode == FontMode::SDF) stbtt_FreeSDF(bitmap, nullptr);
                else                       stbtt_FreeBitmap(bitmap, nullptr);
                break;
            }

            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w; ++col) {
                    const u8  val = bitmap[row * w + col];
                    const u32 px  = (cursorY + static_cast<u32>(row)) * AtlasSize
                                  + (cursorX + static_cast<u32>(col));
                    if (mode == FontMode::SDF) {
                        atlas[px * 4 + 0] = val; // R — SDF distance (read by sdf.frag)
                        atlas[px * 4 + 1] = val;
                        atlas[px * 4 + 2] = val;
                        atlas[px * 4 + 3] = 255;
                    } else {
                        atlas[px * 4 + 0] = 255; // white texel
                        atlas[px * 4 + 1] = 255;
                        atlas[px * 4 + 2] = 255;
                        atlas[px * 4 + 3] = val; // A — coverage (alpha-tested by sprite blend)
                    }
                }
            }

            g.UV = {
                static_cast<f32>(cursorX)     / static_cast<f32>(AtlasSize),
                static_cast<f32>(cursorY)     / static_cast<f32>(AtlasSize),
                static_cast<f32>(cursorX + w) / static_cast<f32>(AtlasSize),
                static_cast<f32>(cursorY + h) / static_cast<f32>(AtlasSize),
            };

            cursorX  += static_cast<u32>(w) + GlyphPadding;
            rowHeight = std::max(rowHeight, static_cast<u32>(h));
        }

        if (mode == FontMode::SDF) { if (bitmap) stbtt_FreeSDF(bitmap, nullptr); }
        else                       { if (bitmap) stbtt_FreeBitmap(bitmap, nullptr); }

        _glyphs[cp] = g;
    }

    TextureDesc texDesc{};
    texDesc.Width     = AtlasSize;
    texDesc.Height    = AtlasSize;
    texDesc.Format    = Format::RGBA8_UNorm;
    texDesc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    texDesc.DebugName = (mode == FontMode::SDF) ? "font_sdf_atlas" : "font_bitmap_atlas";
    _texture = device.CreateTexture(texDesc);
    device.UploadTexture(_texture, atlas.data(), atlas.size());

    SamplerDesc sampDesc{};
    sampDesc.MinFilter = Filter::Linear;
    sampDesc.MagFilter = Filter::Linear;
    sampDesc.AddressU  = AddressMode::ClampToEdge;
    sampDesc.AddressV  = AddressMode::ClampToEdge;
    sampDesc.DebugName = (mode == FontMode::SDF) ? "font_sdf_sampler" : "font_bitmap_sampler";
    _sampler = device.CreateSampler(sampDesc);

    LOG_INFO(Engine, "FontAtlas: '{}' @ {}px ({}) - {} glyphs baked into {}x{} atlas",
             ttfPath, pixelSize, (mode == FontMode::SDF) ? "SDF" : "Bitmap",
             _glyphs.size(), AtlasSize, AtlasSize);
    return true;
}

const GlyphInfo* FontAtlas::GetGlyph(const u32 codepoint) const
{
    const auto it = _glyphs.find(codepoint);
    return it != _glyphs.end() ? &it->second : nullptr;
}

// Returns the total advance width of a single line (no newlines).
static f32 MeasureLineWidth(const FontAtlas& font, std::string_view line, f32 scale)
{
    f32 width = 0.0f;
    for (const char c : line) {
        if (c == '\t') {
            if (const GlyphInfo* sp = font.GetGlyph(' ')) width += sp->Advance * scale * 4.0f;
            continue;
        }
        if (const GlyphInfo* g = font.GetGlyph(static_cast<u32>(c))) width += g->Advance * scale;
    }
    return width;
}

static f32 LineStartX(const f32 anchorX, const f32 lineWidth, const TextAlign align)
{
    switch (align) {
        case TextAlign::Center: return anchorX - lineWidth * 0.5f;
        case TextAlign::Right:  return anchorX - lineWidth;
        default:                return anchorX;
    }
}

void DrawText(FramePacket& packet, const FontAtlas& font, const std::string_view text,
              const Vec2 position, const f32 scale, const Color color,
              const i32 layer, const TextAlign align)
{
    if (!font.IsValid() || text.empty()) return;

    const bool       sdf      = (font.GetMode() == FontMode::SDF);
    const f32        baselineY = position.Y + font.GetAscent() * scale;
    std::string_view remaining = text;
    f32              cursorY   = baselineY;

    while (true)
    {
        const auto       nl     = remaining.find('\n');
        const auto       line   = remaining.substr(0, nl);
        const f32        lineW  = (align != TextAlign::Left) ? MeasureLineWidth(font, line, scale) : 0.0f;
        f32              cursorX = LineStartX(position.X, lineW, align);

        for (const char c : line) {
            if (c == '\t') {
                if (const GlyphInfo* sp = font.GetGlyph(' '))
                    cursorX += sp->Advance * scale * 4.0f;
                continue;
            }

            const GlyphInfo* g = font.GetGlyph(static_cast<u32>(c));
            if (!g) continue;
            if (g->BitmapSize.X == 0.0f) { cursorX += g->Advance * scale; continue; }

            const Vec2 size    = { g->BitmapSize.X * scale, g->BitmapSize.Y * scale };
            const Vec2 topLeft = { cursorX + g->Bearing.X * scale, cursorY + g->Bearing.Y * scale };

            Sprite s{};
            s.Texture  = font.GetTexture();
            s.Sampler  = font.GetSampler();
            s.Position = { topLeft.X + size.X * 0.5f, topLeft.Y + size.Y * 0.5f };
            s.Size     = size;
            s.UV       = g->UV;
            s.Tint     = color;
            s.Layer    = layer;

            if (sdf) packet.SDFSprites.push_back(s);
            else     packet.Sprites.push_back(s);

            cursorX += g->Advance * scale;
        }

        if (nl == std::string_view::npos) break;
        cursorY   += font.GetLineHeight() * scale;
        remaining  = remaining.substr(nl + 1);
    }
}

void DrawTextUI(FramePacket& packet, const FontAtlas& font, const std::string_view text,
                const Vec2 position, const f32 scale, const Color color,
                const i32 layer, const TextAlign align)
{
    if (!font.IsValid() || text.empty()) return;

    const f32        baselineY = position.Y + font.GetAscent() * scale;
    std::string_view remaining = text;
    f32              cursorY   = baselineY;

    while (true)
    {
        const auto       nl     = remaining.find('\n');
        const auto       line   = remaining.substr(0, nl);
        const f32        lineW  = (align != TextAlign::Left) ? MeasureLineWidth(font, line, scale) : 0.0f;
        f32              cursorX = LineStartX(position.X, lineW, align);

        for (const char c : line) {
            if (c == '\t') {
                if (const GlyphInfo* sp = font.GetGlyph(' '))
                    cursorX += sp->Advance * scale * 4.0f;
                continue;
            }

            const GlyphInfo* g = font.GetGlyph(static_cast<u32>(c));
            if (!g) continue;
            if (g->BitmapSize.X == 0.0f) { cursorX += g->Advance * scale; continue; }

            const Vec2 size    = { g->BitmapSize.X * scale, g->BitmapSize.Y * scale };
            const Vec2 topLeft = { cursorX + g->Bearing.X * scale, cursorY + g->Bearing.Y * scale };

            Sprite s{};
            s.Texture  = font.GetTexture();
            s.Sampler  = font.GetSampler();
            s.Position = { topLeft.X + size.X * 0.5f, topLeft.Y + size.Y * 0.5f };
            s.Size     = size;
            s.UV       = g->UV;
            s.Tint     = color;
            s.Layer    = layer;
            s.SDFMode  = true;
            packet.UISprites.push_back(s);

            cursorX += g->Advance * scale;
        }

        if (nl == std::string_view::npos) break;
        cursorY   += font.GetLineHeight() * scale;
        remaining  = remaining.substr(nl + 1);
    }
}

f32 MeasureTextWidth(const FontAtlas& font, const std::string_view line, const f32 scale)
{
    return MeasureLineWidth(font, line, scale);
}

} // namespace Arcbit
