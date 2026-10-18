#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>
#include <arcbit/assets/AssetTypes.h>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Arcbit {

struct FramePacket;
class  RenderDevice;

enum class FontMode
{
    // RGBA8 atlas; white pixels with alpha = coverage.
    // DrawText pushes into packet.Sprites — world-space, camera-affected, lit.
    Bitmap,

    // R-channel signed distance field atlas (stb onedge=128, dist_scale=64).
    // DrawText pushes into packet.SDFSprites — screen-space pixels, no lighting.
    SDF,
};

// Per-glyph layout metrics produced by the atlas baker.
struct GlyphInfo
{
    UVRect UV;          // normalized sub-rect in the glyph's atlas page
    Vec2   BitmapSize;  // bitmap width x height in pixels (includes SDF padding for SDF mode)
    Vec2   Bearing;     // (xoff, yoff) from cursor/baseline to bitmap top-left (stb convention)
    f32    Advance;     // horizontal advance in pixels at the baked pixelSize
    u32    Page = 0;    // which atlas page (texture) holds the bitmap
};

// An inclusive range of codepoints to bake.
struct GlyphRange
{
    u32 First;
    u32 Last;
};

// Bakes a TTF font into GPU texture atlas pages.
//
// The glyphs baked at load are a set of codepoint ranges (by default most
// European languages); pages are added as the set needs them. A codepoint the
// font lacks draws as a box, so missing glyphs are visible rather than silent.
//
// Baking is CPU-only (LoadFace) and separate from the GPU textures (Upload),
// so text layout can be measured without a device.
class FontAtlas
{
public:
    static constexpr u32 PageSize = 1024; // pixels per side of an atlas page

    FontAtlas();
    ~FontAtlas();
    FontAtlas(FontAtlas&&) noexcept;
    FontAtlas& operator=(FontAtlas&&) noexcept;

    // Basic Latin, Latin-1, Latin Extended-A and common punctuation (dashes,
    // curly quotes, ellipsis, the euro sign): English, French, Spanish,
    // German, Portuguese, Italian, Polish, Czech, Turkish and others.
    [[nodiscard]] static std::span<const GlyphRange> DefaultGlyphs();

    // Bake a TTF file at pixelSize in the chosen mode, then create its textures.
    // Returns false on load failure.
    bool Load(std::string_view ttfPath, f32 pixelSize, FontMode mode, RenderDevice& device,
              std::span<const GlyphRange> glyphs = DefaultGlyphs());

    // CPU half of Load: read the font and bake glyphs into pages.
    bool LoadFace(std::string_view ttfPath, f32 pixelSize, FontMode mode,
                  std::span<const GlyphRange> glyphs = DefaultGlyphs());

    // GPU half of Load: create textures for new pages and upload changed ones.
    void Upload(RenderDevice& device);

    // Destroy the textures and sampler. The GPU must be done with them.
    void Release(RenderDevice& device);

    // The glyph for a codepoint: the missing-glyph box if the font has none.
    // Null only before the font is loaded.
    [[nodiscard]] const GlyphInfo* GetGlyph(u32 codepoint) const;
    [[nodiscard]] bool             HasGlyph(u32 codepoint) const { return _glyphs.contains(codepoint); }
    [[nodiscard]] usize            GetGlyphCount() const { return _glyphs.size(); }

    [[nodiscard]] TextureHandle GetTexture(u32 page = 0) const;
    [[nodiscard]] u32           GetPageCount()  const { return static_cast<u32>(_pages.size()); }
    [[nodiscard]] SamplerHandle GetSampler()    const { return _sampler; }
    [[nodiscard]] FontMode      GetMode()       const { return _mode; }
    [[nodiscard]] f32           GetLineHeight() const { return _lineHeight; }
    [[nodiscard]] f32           GetAscent()     const { return _ascent; }
    [[nodiscard]] bool          IsValid()       const { return !_pages.empty() && _pages[0].Texture.IsValid(); }

private:
    struct Face; // the parsed TTF (stb_truetype stays out of this header)

    struct Page
    {
        std::vector<u8> Pixels; // RGBA8, PageSize x PageSize
        TextureHandle   Texture;
        u32             CursorX = 0, CursorY = 0, RowHeight = 0;
        bool            Dirty   = true; // changed since the last upload
    };

    // Place a w x h bitmap in a page (a new one when the last is full) and
    // copy it in. Returns false if it can't fit any page.
    bool Place(const u8* bitmap, u32 w, u32 h, GlyphInfo& glyph);
    void Write(Page& page, const u8* bitmap, u32 w, u32 h, u32 x, u32 y) const;
    void BakeGlyph(u32 codepoint);
    void BakeMissingBox();

    // A glyph drawn here rather than by the font: a w x h shape lift pixels
    // above the baseline, given as a signed distance (pixels, negative
    // inside) at points of its box.
    GlyphInfo BakeDrawn(f32 w, f32 h, f32 lift, const std::function<f32(f32 x, f32 y)>& distance);
    bool      BakeArrow(u32 codepoint); // the UI triangles, if the font lacks them

    std::unique_ptr<Face>              _face;
    std::unordered_map<u32, GlyphInfo> _glyphs;
    GlyphInfo                          _missing{};
    std::vector<Page>                  _pages;
    SamplerHandle                      _sampler;
    FontMode                           _mode       = FontMode::Bitmap;
    f32                                _pixelSize  = 0.0f;
    f32                                _scale      = 0.0f; // stb scale for _pixelSize
    f32                                _lineHeight = 0.0f;
    f32                                _ascent     = 0.0f;
};

enum class TextAlign
{
    Left,    // position.X is the left edge of each line (default)
    Center,  // position.X is the horizontal center of each line
    Right,   // position.X is the right edge of each line
};

// Submit a string as textured quads into the frame packet.
//
// Bitmap FontAtlas: position is a world-space coordinate; quads land in
//   packet.Sprites and are affected by the camera and point lighting.
//
// SDF FontAtlas: position is a screen-space coordinate in pixels measured
//   from the window's top-left corner; quads land in packet.SDFSprites and
//   are rendered without camera transform or lighting — ideal for HUD and
//   debug overlays.
//
// scale multiplies the baked pixelSize: 1.0 = original size, 2.0 = doubled.
// align controls horizontal alignment relative to position.X — Left keeps
//   the default behavior; Center/Right shift each line so it anchors at
//   position.X rather than starting from it.
// layer controls draw order within the same packet list.
void DrawText(FramePacket& packet, const FontAtlas& font, std::string_view text,
              Vec2 position, f32 scale, Color color,
              i32 layer = 0, TextAlign align = TextAlign::Left);

// Same as DrawText but emits into packet.UISprites with SDFMode=true so the
// UI pipeline renders it at the correct layer relative to UI backgrounds.
// Used by Label and Button widgets — do not call from world-space game code.
void DrawTextUI(FramePacket& packet, const FontAtlas& font, std::string_view text,
                Vec2 position, f32 scale, Color color,
                i32 layer = 0, TextAlign align = TextAlign::Left);

// Measure the pixel width of a single line (no newlines) at the given scale.
f32 MeasureTextWidth(const FontAtlas& font, std::string_view line, f32 scale);

// Word-wrap text to fit maxWidth pixels, keeping explicit newlines. Lines
// break at spaces, and between Chinese / Japanese characters (which have no
// spaces), never before closing punctuation; a word wider than the whole
// line breaks wherever it must.
[[nodiscard]] std::string WrapText(const FontAtlas& font, std::string_view text, f32 maxWidth, f32 scale);

} // namespace Arcbit
