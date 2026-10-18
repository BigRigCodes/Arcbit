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
#include <unordered_set>
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

// A loaded font file (TTF or OTF), shared by every atlas that draws from it:
// a fallback face for Japanese is usually shared by all of a game's fonts.
class FontFace
{
public:
    // Null (and a warning logged) if the file can't be read or isn't a font.
    [[nodiscard]] static std::shared_ptr<FontFace> Load(std::string_view path);

    ~FontFace();

    [[nodiscard]] bool               HasCodepoint(u32 codepoint) const;
    [[nodiscard]] const std::string& GetPath() const { return _path; }

private:
    friend class FontAtlas;
    struct Impl; // stb_truetype stays out of this header

    FontFace();
    std::unique_ptr<Impl> _impl;
    std::string           _path;
};

// Bakes a font into GPU texture atlas pages.
//
// A set of codepoint ranges is baked at load (by default most European
// languages); any other codepoint is baked the first time it's drawn, from
// the font or else its fallback faces, then the shared ones, in order. A codepoint none of them has
// draws as a box, so missing glyphs are visible rather than silent.
//
// Baking is CPU-only (LoadFace) and separate from the GPU textures (Upload),
// so text layout can be measured without a device. Glyphs baked while
// drawing reach the GPU through UploadAll, once per frame.
class FontAtlas
{
public:
    static constexpr u32 PageSize = 1024; // pixels per side of an atlas page

    FontAtlas();
    ~FontAtlas();
    FontAtlas(FontAtlas&& other) noexcept;
    FontAtlas& operator=(FontAtlas&& other) noexcept;

    // Basic Latin, Latin-1, Latin Extended-A and common punctuation (dashes,
    // curly quotes, ellipsis, the euro sign): English, French, Spanish,
    // German, Portuguese, Italian, Polish, Czech, Turkish and others.
    [[nodiscard]] static std::span<const GlyphRange> DefaultGlyphs();

    // Bake a font file at pixelSize in the chosen mode, then create its textures.
    // Returns false on load failure.
    bool Load(std::string_view path, f32 pixelSize, FontMode mode, RenderDevice& device,
              std::span<const GlyphRange> glyphs = DefaultGlyphs());

    // CPU half of Load: read the font and bake glyphs into pages.
    bool LoadFace(std::string_view path, f32 pixelSize, FontMode mode,
                  std::span<const GlyphRange> glyphs = DefaultGlyphs());

    // GPU half of Load: create textures for new pages and upload changed ones.
    void Upload(RenderDevice& device);

    // Upload every live atlas's new glyphs. beforeUpload runs first, once,
    // and only if there's something to upload: it must stop the GPU using
    // the atlases (Application: the render thread's WaitIdle).
    static void UploadAll(RenderDevice& device, const std::function<void()>& beforeUpload);

    // Destroy the textures and sampler. The GPU must be done with them.
    void Release(RenderDevice& device);

    // Faces asked, in order, for a codepoint this font lacks. Glyphs are
    // drawn at this atlas's size and mode, on its pages.
    void AddFallback(std::shared_ptr<const FontFace> face);
    void ClearFallbacks();
    [[nodiscard]] const std::vector<std::shared_ptr<const FontFace>>& GetFallbacks() const { return _fallbacks; }

    // Faces every atlas, loaded now or later, falls back to after its own
    // fallbacks: the active language's fonts (a Japanese face while Japanese is on).
    static void SetSharedFallbacks(std::vector<std::shared_ptr<const FontFace>> faces);
    [[nodiscard]] static const std::vector<std::shared_ptr<const FontFace>>& GetSharedFallbacks();

    // Bake every codepoint in text now (a language's strings, as it loads),
    // so drawing it later doesn't stop to rasterize.
    void Prepare(std::string_view text) const;

    // The glyph for a codepoint, baked now if needed: the missing-glyph box
    // if no face has it. Null only before the font is loaded.
    [[nodiscard]] const GlyphInfo* GetGlyph(u32 codepoint) const;
    [[nodiscard]] bool             HasGlyph(u32 codepoint) const { return _glyphs.contains(codepoint); } // baked
    [[nodiscard]] usize            GetGlyphCount() const { return _glyphs.size(); }

    [[nodiscard]] TextureHandle GetTexture(u32 page = 0) const;
    [[nodiscard]] u32           GetPageCount()  const { return static_cast<u32>(_pages.size()); }
    [[nodiscard]] SamplerHandle GetSampler()    const { return _sampler; }
    [[nodiscard]] FontMode      GetMode()       const { return _mode; }
    [[nodiscard]] f32           GetLineHeight() const { return _lineHeight; }
    [[nodiscard]] f32           GetAscent()     const { return _ascent; }
    [[nodiscard]] bool          IsValid()       const { return !_pages.empty() && _pages[0].Texture.IsValid(); }

private:
    struct Page
    {
        std::vector<u8> Pixels; // RGBA8, PageSize x PageSize
        TextureHandle   Texture;
        u32             CursorX = 0, CursorY = 0, RowHeight = 0;
        bool            Dirty   = true; // changed since the last upload
    };

    // Baking fills the glyph cache from const lookups (GetGlyph), so the
    // cache and the pages it draws on are mutable.
    bool Place(const u8* bitmap, u32 w, u32 h, GlyphInfo& glyph) const;
    void Write(Page& page, const u8* bitmap, u32 w, u32 h, u32 x, u32 y) const;
    bool BakeGlyph(u32 codepoint) const; // false if no face has it
    bool BakeFrom(const FontFace& face, u32 codepoint) const;
    void BakeMissingBox();

    // A glyph drawn here rather than by a font: a w x h shape lift pixels
    // above the baseline, given as a signed distance (pixels, negative
    // inside) at points of its box.
    GlyphInfo BakeDrawn(f32 w, f32 h, f32 lift, const std::function<f32(f32 x, f32 y)>& distance) const;
    bool      BakeArrow(u32 codepoint) const; // the UI triangles, if no face has them

    void Track();   // join the live atlases UploadAll visits
    void Untrack();

    std::shared_ptr<const FontFace>              _face;
    std::vector<std::shared_ptr<const FontFace>> _fallbacks;
    mutable std::unordered_map<u32, GlyphInfo>   _glyphs;
    mutable std::unordered_set<u32>              _absent; // asked for, no face has it: the box
    mutable std::vector<Page>                    _pages;
    GlyphInfo                                    _missing{};
    SamplerHandle                                _sampler;
    FontMode                                     _mode       = FontMode::Bitmap;
    f32                                          _pixelSize  = 0.0f;
    f32                                          _emPixels   = 0.0f; // the font's em at _pixelSize: every face is drawn to it
    f32                                          _lineHeight = 0.0f;
    f32                                          _ascent     = 0.0f;
    bool                                         _tracked    = false;
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
