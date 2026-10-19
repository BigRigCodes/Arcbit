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
    [[nodiscard]] SpanStyle Current() const
    {
        SpanStyle s;
        if (!_colors.empty()) s.Color = _colors.back(), s.HasColor = true;
        if (!_sizes.empty()) s.Size = _sizes.back();
        if (!_speeds.empty()) s.Speed = _speeds.back();
        if (!_outlines.empty()) s.Outline = true, s.OutlineColor = _outlines.back();
        s.Bold = _bold > 0, s.Italic = _italic > 0, s.Underline = _underline > 0, s.Strike = _strike > 0;
        s.Shadow = _shadow > 0;
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
        if (Out.Spans.empty() || !SameStyle(Out.Spans.back().Style, style)) Out.Spans.push_back({ {}, style });
        Out.Spans.back().Text += text;
        Chars += static_cast<u32>(Utf8::Length(text));
    }

    static bool SameStyle(const SpanStyle& a, const SpanStyle& b)
    {
        const auto same = [](const Color& x, const Color& y) { return x.R == y.R && x.G == y.G && x.B == y.B && x.A == y.A; };
        return same(a.Color, b.Color) && a.HasColor == b.HasColor && a.Size == b.Size && a.Bold == b.Bold &&
               a.Italic == b.Italic && a.Underline == b.Underline && a.Strike == b.Strike && a.Outline == b.Outline &&
               same(a.OutlineColor, b.OutlineColor) && a.Shadow == b.Shadow && a.Effects == b.Effects && a.Speed == b.Speed;
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

ParsedText ParseMarkup(const std::string_view text)
{
    StyleStack  styles;
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

std::vector<Atom> ToAtoms(const FontAtlas& font, const std::vector<TextSpan>& spans, const f32 scale)
{
    std::vector<Atom> atoms;
    for (u32 s = 0; s < spans.size(); ++s) {
        const f32 size = scale * spans[s].Style.Size;
        for (const u32 cp : Utf8::Codepoints(spans[s].Text)) {
            const GlyphInfo* glyph   = cp >= 32 ? font.GetGlyph(cp) : nullptr;
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
        const Atom&      atom  = atoms[k];
        const f32        size  = scale * layout.Spans[atom.Span].Style.Size;
        const GlyphInfo* glyph = atom.Codepoint >= 32 ? font.GetGlyph(atom.Codepoint) : nullptr;
        if (glyph && glyph->BitmapSize.X > 0.0f)
            layout.Glyphs.push_back({ .Glyph    = *glyph,
                                      .Position = { pen + glyph->Bearing.X * size, line.Baseline + glyph->Bearing.Y * size },
                                      .Size     = glyph->BitmapSize * size,
                                      .Baseline = line.Baseline,
                                      .Advance  = atom.Advance,
                                      .Index    = static_cast<u32>(k),
                                      .Span     = atom.Span,
                                      .Line     = static_cast<u32>(layout.Lines.size()) });
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
    ParsedText parsed = options.Markup ? ParseMarkup(text) : ParsedText{ { { std::string(text), {} } }, {} };
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
    for (LaidGlyph& glyph : layout.Glyphs) glyph.Position.X += layout.Lines[glyph.Line].Left;
    return layout;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void DrawRichText(FramePacket& packet, const FontAtlas& font, const TextLayout& layout, const TextDrawOptions& options)
{
    for (const LaidGlyph& glyph : layout.Glyphs) {
        if (glyph.Index >= options.Visible) break; // glyphs are in reading order
        const TextSpan& span = layout.Spans[glyph.Span];
        Sprite          s{};
        s.Texture = font.GetTexture(glyph.Glyph.Page);
        if (!s.Texture.IsValid()) continue; // on a page made this frame: drawn from the next
        Color tint = span.Style.HasColor ? span.Style.Color : options.Color;
        tint.A *= options.Color.A;
        s.Sampler   = font.GetSampler();
        s.Position  = options.Position + glyph.Position + glyph.Size * 0.5f;
        s.Size      = glyph.Size;
        s.UV        = glyph.Glyph.UV;
        s.Tint      = tint;
        s.Layer     = options.Layer;
        s.SDFMode   = true;
        s.ClipIndex = options.Clip;
        packet.UISprites.push_back(s);
    }
}
} // namespace Arcbit
