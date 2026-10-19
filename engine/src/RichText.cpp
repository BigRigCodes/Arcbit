#include <arcbit/render/RichText.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/core/Utf8.h>

#include "TextBreaks.h"

#include <algorithm>
#include <charconv>
#include <optional>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Markup
// ---------------------------------------------------------------------------

namespace
{
std::optional<Color> ParseColor(const std::string_view value)
{
    static constexpr std::pair<std::string_view, Color> Names[] = {
        { "white", { 1, 1, 1, 1 } },          { "black", { 0, 0, 0, 1 } },        { "gray", { 0.6f, 0.6f, 0.6f, 1 } },
        { "grey", { 0.6f, 0.6f, 0.6f, 1 } },  { "red", { 0.9f, 0.25f, 0.2f, 1 } }, { "green", { 0.35f, 0.8f, 0.35f, 1 } },
        { "blue", { 0.35f, 0.55f, 1, 1 } },   { "yellow", { 1, 0.9f, 0.3f, 1 } }, { "gold", { 1, 0.8f, 0.25f, 1 } },
        { "orange", { 1, 0.6f, 0.2f, 1 } },   { "purple", { 0.7f, 0.45f, 1, 1 } }, { "cyan", { 0.35f, 0.9f, 0.95f, 1 } },
    };
    for (const auto& [name, color] : Names)
        if (name == value) return color;
    if (value.size() < 2 || value[0] != '#') return std::nullopt;

    std::string hex(value.substr(1));
    if (hex.size() == 3 || hex.size() == 4) { // #rgb(a): each digit doubled
        std::string wide;
        for (const char c : hex) wide += std::string(2, c);
        hex = wide;
    }
    if (hex.size() == 6) hex += "ff";
    u32 bits = 0;
    if (hex.size() != 8 || std::from_chars(hex.data(), hex.data() + 8, bits, 16).ptr != hex.data() + 8) return std::nullopt;
    const auto channel = [bits](const int shift) { return static_cast<f32>((bits >> shift) & 0xFF) / 255.0f; };
    return Color{ channel(24), channel(16), channel(8), channel(0) };
}

std::optional<f32> ParseNumber(const std::string_view value)
{
    f32 number = 0.0f;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    return error == std::errc{} && end == value.data() + value.size() ? std::optional(number) : std::nullopt;
}

// The style as tags open and close: counts for on / off tags (nesting
// <b><b></b> stays bold), stacks for valued ones.
class StyleStack
{
public:
    explicit StyleStack(const SpanStyle& base) : _base(base) {}

    [[nodiscard]] SpanStyle Current() const
    {
        SpanStyle s = _base;
        if (!_colors.empty()) s.Color = _colors.back(), s.HasColor = true;
        if (!_sizes.empty()) s.Size = _sizes.back();
        if (!_speeds.empty()) s.Speed = _speeds.back();
        if (!_outlines.empty()) s.Outline = true, s.OutlineColor = _outlines.back();
        s.Bold = s.Bold || _bold > 0, s.Italic = s.Italic || _italic > 0;
        s.Underline = _underline > 0, s.Strike = _strike > 0;
        s.Shadow  = s.Shadow || _shadow > 0;
        s.Effects = static_cast<u8>((_wave > 0 ? EffectWave : 0) | (_shake > 0 ? EffectShake : 0) | (_rainbow > 0 ? EffectRainbow : 0));
        return s;
    }

    // Apply a tag; false if it isn't one (its text is then kept as written).
    bool Apply(const std::string_view name, const std::string_view value, const bool closing)
    {
        if (i32* count = Counter(name)) {
            *count = closing ? std::max(0, *count - 1) : *count + 1;
            return true;
        }
        if (name == "color") return Valued(_colors, closing, ParseColor(value));
        if (name == "size") return Valued(_sizes, closing, ParseNumber(value));
        if (name == "speed") return Valued(_speeds, closing, ParseNumber(value));
        if (name == "outline") return Valued(_outlines, closing, value.empty() ? std::optional(Color{ 0, 0, 0, 1 }) : ParseColor(value));
        return false;
    }

private:
    i32* Counter(const std::string_view name)
    {
        if (name == "b") return &_bold;
        if (name == "i") return &_italic;
        if (name == "u") return &_underline;
        if (name == "s") return &_strike;
        if (name == "shadow") return &_shadow;
        if (name == "wave") return &_wave;
        if (name == "shake") return &_shake;
        if (name == "rainbow") return &_rainbow;
        return nullptr;
    }

    template <typename T>
    static bool Valued(std::vector<T>& stack, const bool closing, const std::optional<T>& value)
    {
        if (closing) {
            if (!stack.empty()) stack.pop_back();
            return true;
        }
        if (!value) return false; // <color=nonsense> stays text, so the mistake shows
        stack.push_back(*value);
        return true;
    }

    SpanStyle          _base;
    std::vector<Color> _colors, _outlines;
    std::vector<f32>   _sizes, _speeds;
    i32                _bold = 0, _italic = 0, _underline = 0, _strike = 0, _shadow = 0, _wave = 0, _shake = 0, _rainbow = 0;
};

// Builds spans: text joins the current span until the style changes.
struct SpanBuilder
{
    ParsedText Out;
    u32        Chars = 0; // characters so far: where a pause lands

    void Text(const std::string_view text, const SpanStyle& style)
    {
        if (text.empty()) return;
        if (Out.Spans.empty() || !(Out.Spans.back().Style == style)) Out.Spans.push_back({ {}, style });
        Out.Spans.back().Text += text;
        Chars += static_cast<u32>(Utf8::Length(text));
    }
};

// One "<...>" at text[open]: applied if it's a tag; returns where parsing goes on.
usize ReadTag(const std::string_view text, const usize open, StyleStack& styles, SpanBuilder& spans)
{
    const usize close = text.find('>', open + 1);
    if (close == std::string_view::npos) return std::string_view::npos;
    std::string_view tag     = text.substr(open + 1, close - open - 1);
    const bool       closing = tag.starts_with('/');
    if (closing) tag.remove_prefix(1);
    const usize            equals = tag.find('=');
    const std::string_view name   = tag.substr(0, equals);
    const std::string_view value  = equals == std::string_view::npos ? std::string_view{} : tag.substr(equals + 1);

    if (name == "pause" && !closing) {
        spans.Out.Pauses.push_back({ spans.Chars, value.empty() ? 0.5f : ParseNumber(value).value_or(0.5f) });
        return close + 1;
    }
    return styles.Apply(name, value, closing) ? close + 1 : std::string_view::npos;
}
} // namespace

ParsedText ParseMarkup(const std::string_view text, const SpanStyle& base)
{
    StyleStack  styles(base);
    SpanBuilder spans;
    usize       plain = 0; // start of text not yet added
    for (usize i = 0; i < text.size(); ++i) {
        if (text[i] != '<') continue;
        spans.Text(text.substr(plain, i - plain), styles.Current());
        if (i + 1 < text.size() && text[i + 1] == '<') { // "<<": a literal "<"
            spans.Text("<", styles.Current());
            plain = ++i + 1;
            continue;
        }
        const usize after = ReadTag(text, i, styles, spans);
        if (after == std::string_view::npos) { // not a tag: keep the "<" as text
            plain = i;
            continue;
        }
        plain = after;
        i     = after - 1;
    }
    spans.Text(text.substr(plain), styles.Current());
    return std::move(spans.Out);
}

std::string StripMarkup(const std::string_view text)
{
    std::string out;
    for (const TextSpan& span : ParseMarkup(text).Spans) out += span.Text;
    return out;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

namespace
{
// One character, before it's placed.
struct Atom
{
    u32 Codepoint = 0;
    u32 Span      = 0;
    f32 Advance   = 0.0f; // scaled
};

FontStyle StyleOf(const SpanStyle& style)
{
    if (style.Bold && style.Italic) return FontStyle::BoldItalic;
    if (style.Bold) return FontStyle::Bold;
    return style.Italic ? FontStyle::Italic : FontStyle::Regular;
}

std::vector<Atom> ToAtoms(const FontAtlas& font, const std::vector<TextSpan>& spans, const f32 scale)
{
    std::vector<Atom> atoms;
    for (u32 s = 0; s < spans.size(); ++s) {
        const f32 size = scale * spans[s].Style.Size;
        const FontStyle style = StyleOf(spans[s].Style);
        for (const u32 cp : Utf8::Codepoints(spans[s].Text)) {
            const GlyphInfo* glyph   = cp >= 32 ? font.GetStyledGlyph(cp, style).Glyph : nullptr;
            const f32        advance = cp == '\t' ? (font.GetGlyph(' ') ? font.GetGlyph(' ')->Advance * 4.0f : 0.0f)
                                     : glyph      ? glyph->Advance
                                                  : 0.0f;
            atoms.push_back({ cp, s, advance * size });
        }
    }
    return atoms;
}

bool IsSpace(const u32 cp) { return cp == ' ' || cp == '\t' || cp == 0x3000; }

// May a line break before atoms[i]? After spaces, and around CJK characters.
bool BreakBefore(const std::vector<Atom>& atoms, const usize i)
{
    if (i == 0) return false;
    const u32 cp = atoms[i].Codepoint, prev = atoms[i - 1].Codepoint;
    if (IsSpace(cp)) return false; // spaces stay with what's before them
    if (IsSpace(prev) || prev == '\n') return true;
    return (IsCjk(cp) || IsCjk(prev)) && !NoBreakBefore(cp);
}

// Lines as [first, end) atom ranges, filled greedily to wrapWidth.
std::vector<std::pair<usize, usize>> BreakLines(const std::vector<Atom>& atoms, const f32 wrapWidth)
{
    std::vector<std::pair<usize, usize>> lines;
    usize start = 0;
    f32   width = 0.0f; // of the line so far, its trailing spaces included
    f32   solid = 0.0f; // without them
    for (usize i = 0; i < atoms.size();) {
        if (atoms[i].Codepoint == '\n') {
            lines.emplace_back(start, i);
            start = ++i, width = solid = 0.0f;
            continue;
        }
        usize end = i + 1; // the unit: up to the next break opportunity
        while (end < atoms.size() && atoms[end].Codepoint != '\n' && !BreakBefore(atoms, end)) ++end;
        f32 unit = 0.0f, unitSolid = 0.0f;
        for (usize k = i; k < end; ++k) {
            unit += atoms[k].Advance;
            if (!IsSpace(atoms[k].Codepoint)) unitSolid = unit;
        }
        if (wrapWidth > 0.0f && i > start && solid > 0.0f && width + unitSolid > wrapWidth) {
            lines.emplace_back(start, i);
            start = i, width = solid = 0.0f;
        }
        if (wrapWidth > 0.0f && unitSolid > wrapWidth && i == start) { // wider than a line: break inside it
            for (usize k = i; k < end; ++k) {
                if (k > start && width + atoms[k].Advance > wrapWidth && !IsSpace(atoms[k].Codepoint)) {
                    lines.emplace_back(start, k);
                    start = k, width = 0.0f;
                }
                width += atoms[k].Advance;
            }
            solid = width;
            i     = end;
            continue;
        }
        if (unitSolid > 0.0f) solid = width + unitSolid;
        width += unit;
        i = end;
    }
    lines.emplace_back(start, atoms.size());
    return lines;
}

// Place atoms [first, end) as the next line, from top down.
void PlaceLine(const FontAtlas& font, TextLayout& layout, const std::vector<Atom>& atoms, const usize first, const usize end,
               const f32 scale, f32& top)
{
    f32 ascent = 0.0f, height = 0.0f;
    for (usize k = first; k < end; ++k) {
        const f32 size = scale * layout.Spans[atoms[k].Span].Style.Size;
        ascent         = std::max(ascent, font.GetAscent() * size);
        height         = std::max(height, font.GetLineHeight() * size);
    }
    if (first == end) ascent = font.GetAscent() * scale, height = font.GetLineHeight() * scale; // an empty line

    TextLine line{ .Top = top, .Height = height, .Baseline = top + ascent };
    f32      pen = 0.0f;
    for (usize k = first; k < end; ++k) {
        const Atom&      atom   = atoms[k];
        const SpanStyle& style  = layout.Spans[atom.Span].Style;
        const f32        size   = scale * style.Size;
        const auto       styled = atom.Codepoint >= 32 ? font.GetStyledGlyph(atom.Codepoint, StyleOf(style)) : FontAtlas::StyledGlyph{};
        const GlyphInfo* glyph  = styled.Glyph;
        if (glyph && glyph->BitmapSize.X > 0.0f)
            layout.Glyphs.push_back({ .Glyph      = *glyph,
                                      .Position   = { pen + glyph->Bearing.X * size, line.Baseline + glyph->Bearing.Y * size },
                                      .Size       = glyph->BitmapSize * size,
                                      .Baseline   = line.Baseline,
                                      .Advance    = atom.Advance,
                                      .Pen        = pen,
                                      .Scale      = size,
                                      .Index      = static_cast<u32>(k),
                                      .Span       = atom.Span,
                                      .Line       = static_cast<u32>(layout.Lines.size()),
                                      .FauxBold   = styled.FauxBold,
                                      .FauxItalic = styled.FauxItalic });
        pen += atom.Advance;
        if (!IsSpace(atom.Codepoint)) line.Width = pen;
    }
    layout.Lines.push_back(line);
    top += height;
}
} // namespace

TextLayout LayoutRichText(const FontAtlas& font, const std::string_view text, const TextLayoutOptions& options)
{
    TextLayout layout;
    SpanStyle base;
    base.Bold = options.Style.Bold, base.Italic = options.Style.Italic, base.Shadow = options.Style.Shadow;
    base.Outline = options.Style.Outline, base.OutlineColor = options.Style.OutlineColor;
    ParsedText parsed = options.Markup ? ParseMarkup(text, base) : ParsedText{ { { std::string(text), base } }, {} };
    layout.Spans      = std::move(parsed.Spans);
    layout.Pauses     = std::move(parsed.Pauses);
    if (!font.GetGlyph(' ')) return layout; // no font loaded

    const std::vector<Atom> atoms = ToAtoms(font, layout.Spans, options.Scale);
    layout.CharCount              = static_cast<u32>(atoms.size());
    f32 top = 0.0f;
    for (const auto& [first, end] : BreakLines(atoms, options.WrapWidth)) PlaceLine(font, layout, atoms, first, end, options.Scale, top);

    // Align each line within the box, and measure the whole.
    for (const TextLine& line : layout.Lines) layout.Size.X = std::max(layout.Size.X, line.Width);
    layout.Size.Y   = top;
    const f32 box   = options.BoxWidth > 0.0f ? options.BoxWidth : layout.Size.X;
    const f32 share = options.Align == TextAlign::Center ? 0.5f : options.Align == TextAlign::Right ? 1.0f : 0.0f;
    for (TextLine& line : layout.Lines) line.Left = (box - line.Width) * share;
    for (LaidGlyph& glyph : layout.Glyphs) glyph.Position.X += layout.Lines[glyph.Line].Left, glyph.Pen += layout.Lines[glyph.Line].Left;
    return layout;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

namespace
{
constexpr f32 FauxBoldPixels = 0.7f; // how far a faux bold edge moves out, in baked pixels
constexpr f32 FauxItalicSlant = 0.2f;

Color TintOf(const LaidGlyph& glyph, const TextLayout& layout, const TextDrawOptions& options)
{
    const SpanStyle& style = layout.Spans[glyph.Span].Style;
    Color            tint  = style.HasColor ? style.Color : options.Color;
    tint.A *= options.Color.A;
    return tint;
}

// One glyph's quad, its look in distance-field units of its own size.
Sprite GlyphSprite(const FontAtlas& font, const LaidGlyph& glyph, const TextLayout& layout, const TextDrawOptions& options)
{
    const SpanStyle& style = layout.Spans[glyph.Span].Style;
    const f32        units = FontAtlas::SdfUnitsPerPixel();
    Sprite           s{};
    s.Texture      = font.GetTexture(glyph.Glyph.Page);
    s.Sampler      = font.GetSampler();
    s.Position     = options.Position + glyph.Position + glyph.Size * 0.5f;
    s.Size         = glyph.Size;
    s.UV           = glyph.Glyph.UV;
    s.Tint         = TintOf(glyph, layout, options);
    s.Layer        = options.Layer;
    s.SDFMode      = true;
    s.ClipIndex    = options.Clip;
    s.Weight       = glyph.FauxBold ? FauxBoldPixels * units : 0.0f;
    s.Shear        = glyph.FauxItalic ? FauxItalicSlant : 0.0f;
    s.Outline      = style.Outline ? options.Style.OutlineWidth / glyph.Scale * units : 0.0f;
    s.OutlineColor = style.OutlineColor;
    s.OutlineColor.A *= options.Color.A;
    return s;
}

// A shadow: the glyph again, offset, in the shadow's color, softened.
Sprite ShadowSprite(Sprite s, const LaidGlyph& glyph, const TextDrawOptions& options)
{
    const f32 units = FontAtlas::SdfUnitsPerPixel();
    s.Position += options.Style.ShadowOffset;
    s.Layer    -= 1; // under every glyph, whatever atlas page it's on (the UI sorts by texture within a layer)
    s.Tint     = options.Style.ShadowColor;
    s.Tint.A  *= options.Color.A;
    s.Weight  += s.Outline; // as wide as the outlined glyph
    s.Outline  = 0.0f;
    s.Softness = options.Style.ShadowSoftness / glyph.Scale * units;
    return s;
}

// Underlines and strikethroughs: a bar under (or through) each run of
// consecutive marked glyphs on a line, pen to pen, so spaces between marked
// words are marked too.
void DrawDecorations(FramePacket& packet, const FontAtlas& font, const TextLayout& layout, const TextDrawOptions& options)
{
    if (!options.White.IsValid()) return;
    for (const bool under : { true, false }) {
        const auto marked = [&](const LaidGlyph& g) {
            const SpanStyle& st = layout.Spans[g.Span].Style;
            return g.Index < options.Visible && (under ? st.Underline : st.Strike);
        };
        for (usize i = 0; i < layout.Glyphs.size();) {
            if (!marked(layout.Glyphs[i])) { ++i; continue; }
            usize end = i + 1;
            while (end < layout.Glyphs.size() && marked(layout.Glyphs[end]) && layout.Glyphs[end].Line == layout.Glyphs[i].Line &&
                   TintOf(layout.Glyphs[end], layout, options) == TintOf(layout.Glyphs[i], layout, options))
                ++end;
            const LaidGlyph& first = layout.Glyphs[i];
            const LaidGlyph& last  = layout.Glyphs[end - 1];
            const f32 ascent = font.GetAscent() * first.Scale;
            const f32 thick  = std::max(1.0f, font.GetLineHeight() * first.Scale * 0.06f);
            const f32 y      = first.Baseline + (under ? ascent * 0.14f : -ascent * 0.32f);
            const f32 x0 = first.Pen, x1 = last.Pen + last.Advance;
            Sprite    bar{};
            bar.Texture   = options.White;
            bar.Sampler   = options.WhiteSampler;
            bar.Position  = options.Position + Vec2{ (x0 + x1) * 0.5f, y + thick * 0.5f };
            bar.Size      = { x1 - x0, thick };
            bar.Tint      = TintOf(first, layout, options);
            bar.Layer     = options.Layer;
            bar.ClipIndex = options.Clip;
            packet.UISprites.push_back(bar);
            i = end;
        }
    }
}
} // namespace

void DrawRichText(FramePacket& packet, const FontAtlas& font, const TextLayout& layout, const TextDrawOptions& options)
{
    // Glyphs are in reading order; a reveal draws a prefix. Shadows go first so
    // no glyph's shadow falls on another glyph.
    for (const bool shadows : { true, false })
        for (const LaidGlyph& glyph : layout.Glyphs) {
            if (glyph.Index >= options.Visible) break;
            Sprite s = GlyphSprite(font, glyph, layout, options);
            if (!s.Texture.IsValid()) continue; // on a page made this frame: drawn from the next
            if (shadows) {
                if (layout.Spans[glyph.Span].Style.Shadow) packet.UISprites.push_back(ShadowSprite(s, glyph, options));
            } else {
                packet.UISprites.push_back(s);
            }
        }
    DrawDecorations(packet, font, layout, options);
}

const TextLayout& CachedTextLayout::Get(const FontAtlas& font, const std::string_view text, const TextLayoutOptions& options)
{
    const u64 fallbacks = FontAtlas::GetFallbackGeneration();
    if (_font == &font && _fallbacks == fallbacks && _text == text && _options == options) return _layout;
    _layout    = LayoutRichText(font, text, options);
    _text      = std::string(text);
    _font      = &font;
    _fallbacks = fallbacks;
    _options   = options;
    return _layout;
}
} // namespace Arcbit
