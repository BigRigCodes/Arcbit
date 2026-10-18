#include <arcbit/render/Font.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/RenderDevice.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Utf8.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

namespace Arcbit {

namespace
{
constexpr u32 SdfPadding   = 4;
constexpr u32 GlyphPadding = 1;     // pixels between glyphs in a page
constexpr u8  SdfOnEdge    = 128;
constexpr f32 SdfDistScale = 64.0f; // stb: value change per pixel of distance

constexpr GlyphRange Default[] = {
    { 0x0020, 0x007E }, // Basic Latin
    { 0x00A0, 0x00FF }, // Latin-1 Supplement
    { 0x0100, 0x017F }, // Latin Extended-A
    { 0x2009, 0x200B }, // thin, hair and zero-width spaces
    { 0x2010, 0x2027 }, // dashes, curly quotes, bullet, ellipsis
    { 0x202F, 0x203A }, // narrow no-break space (French), per mille, single guillemets
    { 0x20AC, 0x20AC }, // euro sign
    { 0x2122, 0x2122 }, // trademark
    { 0x25B2, 0x25B2 }, // up, right, down and left triangles (UI arrows;
    { 0x25B6, 0x25B6 }, //   drawn by the engine if the font has none)
    { 0x25BC, 0x25BC },
    { 0x25C0, 0x25C0 },
};

std::optional<std::vector<u8>> ReadFile(const std::string_view path)
{
    std::ifstream f(std::string(path), std::ios::binary | std::ios::ate);
    if (!f) return std::nullopt;
    std::vector<u8> bytes(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

// Space characters many fonts lack, as a width in ems; a missing one gets
// an empty glyph of that width rather than the missing-glyph box.
std::optional<f32> SpaceWidth(const u32 cp)
{
    switch (cp) {
        case 0x00A0: return 0.25f; // no-break space (about a normal space)
        case 0x2009: return 0.2f;  // thin space
        case 0x200A: return 0.1f;  // hair space
        case 0x200B: return 0.0f;  // zero-width space (a break opportunity)
        case 0x202F: return 0.2f;  // narrow no-break space (French "!" "?" ";" ":")
        case 0x3000: return 1.0f;  // ideographic space
        default:     return std::nullopt;
    }
}

// Signed distance (pixels, negative inside, exact inside and near enough
// outside for a distance field) from p to a convex polygon.
f32 ConvexDistance(const f32 px, const f32 py, const std::span<const Vec2> points)
{
    f32 cx = 0.0f, cy = 0.0f;
    for (const Vec2 p : points) cx += p.X, cy += p.Y;
    cx /= static_cast<f32>(points.size()), cy /= static_cast<f32>(points.size());

    f32 dist = -1e9f;
    for (usize i = 0; i < points.size(); ++i) {
        const Vec2 a = points[i], b = points[(i + 1) % points.size()];
        f32        nx = b.Y - a.Y, ny = a.X - b.X;
        const f32  len = std::sqrt(nx * nx + ny * ny);
        nx /= len, ny /= len;
        if ((cx - a.X) * nx + (cy - a.Y) * ny > 0.0f) nx = -nx, ny = -ny; // point the normal outward
        dist = std::max(dist, (px - a.X) * nx + (py - a.Y) * ny);
    }
    return dist;
}

// Signed distance (pixels, negative inside) from p to a box at center c with half-size h.
f32 BoxDistance(const f32 px, const f32 py, const f32 cx, const f32 cy, const f32 hx, const f32 hy)
{
    const f32 qx = std::abs(px - cx) - hx, qy = std::abs(py - cy) - hy;
    const f32 ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f);
}
} // namespace

struct FontAtlas::Face
{
    std::vector<u8> Ttf; // stb reads from this for as long as the font lives
    stbtt_fontinfo  Info{};
};

FontAtlas::FontAtlas()                                = default;
FontAtlas::~FontAtlas()                               = default;
FontAtlas::FontAtlas(FontAtlas&&) noexcept            = default;
FontAtlas& FontAtlas::operator=(FontAtlas&&) noexcept = default;

std::span<const GlyphRange> FontAtlas::DefaultGlyphs() { return Default; }

bool FontAtlas::Load(const std::string_view ttfPath, const f32 pixelSize, const FontMode mode,
                     RenderDevice& device, const std::span<const GlyphRange> glyphs)
{
    if (!LoadFace(ttfPath, pixelSize, mode, glyphs)) return false;
    Upload(device);
    return true;
}

bool FontAtlas::LoadFace(const std::string_view ttfPath, const f32 pixelSize, const FontMode mode,
                         const std::span<const GlyphRange> glyphs)
{
    auto ttf = ReadFile(ttfPath);
    if (!ttf) {
        LOG_WARN(Engine, "FontAtlas: cannot open '{}'", ttfPath);
        return false;
    }
    auto face = std::make_unique<Face>();
    face->Ttf = std::move(*ttf);
    if (!stbtt_InitFont(&face->Info, face->Ttf.data(), stbtt_GetFontOffsetForIndex(face->Ttf.data(), 0))) {
        LOG_WARN(Engine, "FontAtlas: stbtt_InitFont failed for '{}'", ttfPath);
        return false;
    }

    _face      = std::move(face);
    _mode      = mode;
    _pixelSize = pixelSize;
    _scale     = stbtt_ScaleForPixelHeight(&_face->Info, pixelSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&_face->Info, &ascent, &descent, &lineGap);
    _ascent     = static_cast<f32>(ascent) * _scale;
    _lineHeight = static_cast<f32>(ascent - descent + lineGap) * _scale;
    _glyphs.clear();
    _pages.clear();

    BakeMissingBox();
    for (const auto [first, last] : glyphs)
        for (u32 cp = first; cp <= last; ++cp) BakeGlyph(cp);

    LOG_INFO(Engine, "FontAtlas: '{}' @ {}px ({}) - {} glyphs on {} page(s)", ttfPath, pixelSize,
             mode == FontMode::SDF ? "SDF" : "Bitmap", _glyphs.size(), _pages.size());
    return true;
}

// Rasterize one codepoint the font has; codepoints it lacks are left to the box.
void FontAtlas::BakeGlyph(const u32 codepoint)
{
    if (_glyphs.contains(codepoint)) return;
    const int index = stbtt_FindGlyphIndex(&_face->Info, static_cast<int>(codepoint));
    if (index == 0) {
        if (const auto em = SpaceWidth(codepoint)) _glyphs[codepoint] = { .Advance = *em * _pixelSize };
        else BakeArrow(codepoint);
        return;
    }

    int w = 0, h = 0, xoff = 0, yoff = 0;
    u8* bitmap = _mode == FontMode::SDF
        ? stbtt_GetGlyphSDF(&_face->Info, _scale, index, SdfPadding, SdfOnEdge, SdfDistScale, &w, &h, &xoff, &yoff)
        : stbtt_GetGlyphBitmap(&_face->Info, _scale, _scale, index, &w, &h, &xoff, &yoff);
    int advance, lsb;
    stbtt_GetGlyphHMetrics(&_face->Info, index, &advance, &lsb);

    GlyphInfo g{};
    g.Bearing = { static_cast<f32>(xoff), static_cast<f32>(yoff) };
    g.Advance = static_cast<f32>(advance) * _scale;
    if (bitmap && w > 0 && h > 0 && Place(bitmap, static_cast<u32>(w), static_cast<u32>(h), g))
        g.BitmapSize = { static_cast<f32>(w), static_cast<f32>(h) };
    if (bitmap) {
        if (_mode == FontMode::SDF) stbtt_FreeSDF(bitmap, nullptr);
        else                        stbtt_FreeBitmap(bitmap, nullptr);
    }
    _glyphs[codepoint] = g;
}

GlyphInfo FontAtlas::BakeDrawn(const f32 w, const f32 h, const f32 lift, const std::function<f32(f32 x, f32 y)>& distance)
{
    const u32 pad = _mode == FontMode::SDF ? SdfPadding : 0;
    const u32 bw = static_cast<u32>(std::ceil(w)) + pad * 2, bh = static_cast<u32>(std::ceil(h)) + pad * 2;

    std::vector<u8> bitmap(static_cast<usize>(bw) * bh);
    for (u32 y = 0; y < bh; ++y)
        for (u32 x = 0; x < bw; ++x) {
            const f32 dist  = distance(static_cast<f32>(x) + 0.5f - static_cast<f32>(pad),
                                       static_cast<f32>(y) + 0.5f - static_cast<f32>(pad));
            const f32 value = _mode == FontMode::SDF ? SdfOnEdge - dist * SdfDistScale : (0.5f - dist) * 255.0f;
            bitmap[y * bw + x] = static_cast<u8>(std::clamp(value, 0.0f, 255.0f));
        }

    GlyphInfo g{};
    g.Bearing = { 1.0f - static_cast<f32>(pad), -(lift + h) - static_cast<f32>(pad) };
    g.Advance = w + 2.0f;
    if (Place(bitmap.data(), bw, bh, g)) g.BitmapSize = { static_cast<f32>(bw), static_cast<f32>(bh) };
    return g;
}

// The missing-glyph box: a hollow rectangle about the height of a capital,
// drawn here rather than taken from the font (many fonts' own one is blank).
void FontAtlas::BakeMissingBox()
{
    const f32 stroke = std::max(1.0f, std::round(_pixelSize / 16.0f));
    const f32 w = std::round(_pixelSize * 0.45f), h = std::round(_ascent * 0.8f);
    _missing = BakeDrawn(w, h, 0.0f, [=](const f32 x, const f32 y) {
        const f32 outer = BoxDistance(x, y, w * 0.5f, h * 0.5f, w * 0.5f, h * 0.5f);
        const f32 inner = BoxDistance(x, y, w * 0.5f, h * 0.5f, w * 0.5f - stroke, h * 0.5f - stroke);
        return std::max(outer, -inner); // the ring between the two boxes
    });
}

// Solid triangles centered on the lowercase height, so a dropdown or a
// scroll arrow shows even in a font without them.
bool FontAtlas::BakeArrow(const u32 codepoint)
{
    const f32 s = std::round(_pixelSize * 0.4f), lift = std::round(_ascent * 0.35f - s * 0.5f);
    std::array<Vec2, 3> points;
    switch (codepoint) {
        case 0x25B2: points = { Vec2{ 0, s }, Vec2{ s, s }, Vec2{ s * 0.5f, 0 } }; break; // up
        case 0x25BC: points = { Vec2{ 0, 0 }, Vec2{ s, 0 }, Vec2{ s * 0.5f, s } }; break; // down
        case 0x25B6: points = { Vec2{ 0, 0 }, Vec2{ 0, s }, Vec2{ s, s * 0.5f } }; break; // right
        case 0x25C0: points = { Vec2{ s, 0 }, Vec2{ s, s }, Vec2{ 0, s * 0.5f } }; break; // left
        default: return false;
    }
    _glyphs[codepoint] = BakeDrawn(s, s, lift, [points](const f32 x, const f32 y) { return ConvexDistance(x, y, points); });
    return true;
}

bool FontAtlas::Place(const u8* bitmap, const u32 w, const u32 h, GlyphInfo& glyph)
{
    if (w + GlyphPadding * 2 > PageSize || h + GlyphPadding * 2 > PageSize) {
        LOG_WARN(Engine, "FontAtlas: a {}x{} glyph is larger than a page", w, h);
        return false;
    }
    const auto addPage = [this] {
        _pages.push_back({ .Pixels = std::vector<u8>(static_cast<usize>(PageSize) * PageSize * 4, 0),
                           .CursorX = GlyphPadding, .CursorY = GlyphPadding });
    };
    if (_pages.empty()) addPage();

    Page* page = &_pages.back();
    if (page->CursorX + w + GlyphPadding > PageSize) { // next row
        page->CursorX = GlyphPadding;
        page->CursorY += page->RowHeight + GlyphPadding;
        page->RowHeight = 0;
    }
    if (page->CursorY + h + GlyphPadding > PageSize) { // next page
        addPage();
        page = &_pages.back();
    }

    Write(*page, bitmap, w, h, page->CursorX, page->CursorY);
    constexpr f32 size = PageSize;
    glyph.Page = static_cast<u32>(_pages.size() - 1);
    glyph.UV   = { static_cast<f32>(page->CursorX) / size, static_cast<f32>(page->CursorY) / size,
                   static_cast<f32>(page->CursorX + w) / size, static_cast<f32>(page->CursorY + h) / size };
    page->CursorX  += w + GlyphPadding;
    page->RowHeight = std::max(page->RowHeight, h);
    page->Dirty     = true;
    return true;
}

// SDF: the distance in every channel (sdf.frag reads R). Bitmap: white texels
// whose alpha is the coverage (alpha-tested by the sprite blend).
void FontAtlas::Write(Page& page, const u8* bitmap, const u32 w, const u32 h, const u32 x, const u32 y) const
{
    for (u32 row = 0; row < h; ++row)
        for (u32 col = 0; col < w; ++col) {
            const u8 val = bitmap[row * w + col];
            u8*      px  = &page.Pixels[(static_cast<usize>(y + row) * PageSize + x + col) * 4];
            const bool sdf = _mode == FontMode::SDF;
            px[0] = px[1] = px[2] = sdf ? val : 255;
            px[3] = sdf ? 255 : val;
        }
}

void FontAtlas::Upload(RenderDevice& device)
{
    for (Page& page : _pages) {
        if (!page.Dirty) continue;
        if (!page.Texture.IsValid()) {
            TextureDesc desc{};
            desc.Width     = PageSize;
            desc.Height    = PageSize;
            desc.Format    = Format::RGBA8_UNorm;
            desc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
            desc.DebugName = _mode == FontMode::SDF ? "font_sdf_page" : "font_bitmap_page";
            page.Texture   = device.CreateTexture(desc);
        }
        device.UploadTexture(page.Texture, page.Pixels.data(), page.Pixels.size());
        page.Dirty = false;
    }
    if (_sampler.IsValid()) return;
    SamplerDesc desc{};
    desc.MinFilter = Filter::Linear;
    desc.MagFilter = Filter::Linear;
    desc.AddressU  = AddressMode::ClampToEdge;
    desc.AddressV  = AddressMode::ClampToEdge;
    desc.DebugName = _mode == FontMode::SDF ? "font_sdf_sampler" : "font_bitmap_sampler";
    _sampler       = device.CreateSampler(desc);
}

void FontAtlas::Release(RenderDevice& device)
{
    for (Page& page : _pages)
        if (page.Texture.IsValid()) device.DestroyTexture(std::exchange(page.Texture, {}));
    if (_sampler.IsValid()) device.DestroySampler(std::exchange(_sampler, {}));
}

const GlyphInfo* FontAtlas::GetGlyph(const u32 codepoint) const
{
    if (!_face) return nullptr;
    const auto it = _glyphs.find(codepoint);
    return it != _glyphs.end() ? &it->second : &_missing;
}

TextureHandle FontAtlas::GetTexture(const u32 page) const
{
    return page < _pages.size() ? _pages[page].Texture : TextureHandle{};
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

namespace
{
// Walk one line (no newlines) codepoint by codepoint from x, calling
// emit(glyph, penX) for each glyph with a bitmap. Returns the line's width.
template <typename Emit>
f32 WalkLine(const FontAtlas& font, const std::string_view line, const f32 scale, const f32 x, Emit&& emit)
{
    f32 pen = x;
    for (const u32 cp : Utf8::Codepoints(line)) {
        if (cp == '\t') {
            if (const GlyphInfo* sp = font.GetGlyph(' ')) pen += sp->Advance * scale * 4.0f;
            continue;
        }
        if (cp < 32) continue; // other control characters draw nothing
        const GlyphInfo* g = font.GetGlyph(cp);
        if (!g) continue;
        if (g->BitmapSize.X > 0.0f) emit(*g, pen);
        pen += g->Advance * scale;
    }
    return pen - x;
}

f32 LineStartX(const f32 anchorX, const f32 lineWidth, const TextAlign align)
{
    switch (align) {
        case TextAlign::Center: return anchorX - lineWidth * 0.5f;
        case TextAlign::Right:  return anchorX - lineWidth;
        default:                return anchorX;
    }
}

// Lay out text line by line and hand push one sprite per visible glyph.
template <typename Push>
void LayoutText(const FontAtlas& font, const std::string_view text, const Vec2 position, const f32 scale,
                const Color color, const i32 layer, const TextAlign align, Push&& push)
{
    std::string_view remaining = text;
    f32              baseline  = position.Y + font.GetAscent() * scale;
    while (true) {
        const auto nl    = remaining.find('\n');
        const auto line  = remaining.substr(0, nl);
        const f32  lineW = align != TextAlign::Left ? WalkLine(font, line, scale, 0.0f, [](auto&&...) {}) : 0.0f;

        WalkLine(font, line, scale, LineStartX(position.X, lineW, align), [&](const GlyphInfo& g, const f32 pen) {
            const Vec2 size    = { g.BitmapSize.X * scale, g.BitmapSize.Y * scale };
            const Vec2 topLeft = { pen + g.Bearing.X * scale, baseline + g.Bearing.Y * scale };
            Sprite     s{};
            s.Texture  = font.GetTexture(g.Page);
            s.Sampler  = font.GetSampler();
            s.Position = { topLeft.X + size.X * 0.5f, topLeft.Y + size.Y * 0.5f };
            s.Size     = size;
            s.UV       = g.UV;
            s.Tint     = color;
            s.Layer    = layer;
            push(s);
        });

        if (nl == std::string_view::npos) break;
        baseline += font.GetLineHeight() * scale;
        remaining = remaining.substr(nl + 1);
    }
}
} // namespace

void DrawText(FramePacket& packet, const FontAtlas& font, const std::string_view text,
              const Vec2 position, const f32 scale, const Color color,
              const i32 layer, const TextAlign align)
{
    if (!font.IsValid() || text.empty()) return;
    auto& out = font.GetMode() == FontMode::SDF ? packet.SDFSprites : packet.Sprites;
    LayoutText(font, text, position, scale, color, layer, align, [&](const Sprite& s) { out.push_back(s); });
}

void DrawTextUI(FramePacket& packet, const FontAtlas& font, const std::string_view text,
                const Vec2 position, const f32 scale, const Color color,
                const i32 layer, const TextAlign align)
{
    if (!font.IsValid() || text.empty()) return;
    LayoutText(font, text, position, scale, color, layer, align, [&](Sprite s) {
        s.SDFMode = true;
        packet.UISprites.push_back(s);
    });
}

f32 MeasureTextWidth(const FontAtlas& font, const std::string_view line, const f32 scale)
{
    return WalkLine(font, line, scale, 0.0f, [](auto&&...) {});
}

// ---------------------------------------------------------------------------
// Wrapping
// ---------------------------------------------------------------------------

namespace
{
// Scripts written without spaces between words: CJK punctuation, kana and
// ideographs, and full-width forms. (Korean uses spaces, so it isn't here.)
bool IsCjk(const u32 cp)
{
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Closing punctuation and small kana stay on the line they end (kinsoku).
bool NoBreakBefore(const u32 cp)
{
    static constexpr u32 Closers[] = {
        0x3001, 0x3002, 0x30FB, 0x30FC, 0x300D, 0x300F, 0x3011, 0x3015, 0x3009, 0x300B, // punctuation, brackets
        0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, // small hiragana
        0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, // small katakana
        0xFF01, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F,                         // full-width ! ) , . : ; ?
    };
    return std::ranges::find(Closers, cp) != std::end(Closers);
}

// Split a word (no spaces) into the pieces a line may break between.
std::vector<std::string_view> BreakUnits(const std::string_view word)
{
    std::vector<std::string_view> units;
    usize start = 0, pos = 0;
    u32   prev  = 0;
    while (pos < word.size()) {
        const usize at = pos;
        const u32   cp = Utf8::Decode(word, pos);
        if (at > start && (IsCjk(cp) || IsCjk(prev)) && !NoBreakBefore(cp)) {
            units.push_back(word.substr(start, at - start));
            start = at;
        }
        prev = cp;
    }
    if (start < word.size()) units.push_back(word.substr(start));
    return units;
}

// Fills lines greedily, one break unit at a time.
struct LineFiller
{
    const FontAtlas& Font;
    f32              MaxWidth, Scale, SpaceWidth;
    std::string      Out;
    f32              Width     = 0.0f;
    bool             LineStart = true;

    void NewLine()
    {
        Out += '\n';
        Width     = 0.0f;
        LineStart = true;
    }

    // A unit wider than a whole line: break it between characters.
    void AddLong(const std::string_view unit)
    {
        for (usize pos = 0; pos < unit.size();) {
            const usize next = Utf8::Next(unit, pos);
            const auto  ch   = unit.substr(pos, next - pos);
            const f32   w    = MeasureTextWidth(Font, ch, Scale);
            if (!LineStart && Width + w > MaxWidth) NewLine();
            Out += ch;
            Width += w;
            LineStart = false;
            pos       = next;
        }
    }

    void Add(const std::string_view unit, const bool spaceBefore)
    {
        const f32 w   = MeasureTextWidth(Font, unit, Scale);
        const f32 gap = spaceBefore && !LineStart ? SpaceWidth : 0.0f;
        if (!LineStart && Width + gap + w > MaxWidth) NewLine();
        else if (gap > 0.0f) {
            Out += ' ';
            Width += gap;
        }
        if (LineStart && w > MaxWidth) return AddLong(unit);
        Out += unit;
        Width += w;
        LineStart = false;
    }
};
} // namespace

std::string WrapText(const FontAtlas& font, const std::string_view text, const f32 maxWidth, const f32 scale)
{
    const GlyphInfo* space = font.GetGlyph(' ');
    LineFiller       fill{ font, maxWidth, scale, space ? space->Advance * scale : 0.0f };

    std::string_view remaining      = text;
    bool             firstParagraph = true;
    while (true) {
        const auto       nl   = remaining.find('\n');
        std::string_view para = remaining.substr(0, nl);
        if (!firstParagraph) fill.NewLine();
        firstParagraph = false;
        while (!para.empty()) {
            const auto sp = para.find(' ');
            bool       first = true;
            for (const auto unit : BreakUnits(para.substr(0, sp))) {
                fill.Add(unit, first);
                first = false;
            }
            if (sp == std::string_view::npos) break;
            para = para.substr(sp + 1);
        }
        if (nl == std::string_view::npos) break;
        remaining = remaining.substr(nl + 1);
    }
    return fill.Out;
}

} // namespace Arcbit
