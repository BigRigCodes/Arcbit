#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/render/Font.h>

#include <string>
#include <string_view>
#include <vector>

// Rich text: markup tags for style inside a string, and a layout of that
// text as a list of placed glyphs (which a typewriter reveals one by one and
// effects move each frame).
//
// Tags (docs/gui.md#rich-text):
//   <b> <i> <u> <s>                     bold, italic, underline, strikethrough
//   <color=#e33> <color=gold>           color (#rgb, #rrggbb, #rrggbbaa, or a name)
//   <size=1.5>                          scale, times the text's own
//   <outline> <outline=#000>            an outline (black by default)
//   <shadow>                            a drop shadow
//   <wave> <shake> <rainbow>            animated effects
//   <speed=0.5>                         reveal pace for a typewriter (1 = normal)
//   <pause=0.4>                         a typewriter waits this many seconds here
// Each closes with </name> (pause has no close). Only these names are tags:
// "<" in other text ("a < b", "<3") stays as it is, and "<<" writes a "<".
namespace Arcbit
{
enum TextEffect : u8
{
    EffectWave    = 1 << 0,
    EffectShake   = 1 << 1,
    EffectRainbow = 1 << 2,
};

// A text's look beyond its tags - a skin's text style (UISkin::Text): the
// base the tags build on, and how outlines and shadows are drawn.
struct TextStyle
{
    bool  Bold           = false;
    bool  Italic         = false;
    bool  Outline        = false;
    f32   OutlineWidth   = 2.0f;               // screen pixels (for <outline> too)
    Color OutlineColor   = { 0, 0, 0, 1 };
    bool  Shadow         = false;
    Vec2  ShadowOffset   = { 2.0f, 2.0f };     // screen pixels, down and right
    Color ShadowColor    = { 0, 0, 0, 0.6f };
    f32   ShadowSoftness = 1.0f;               // screen pixels of blur

    bool operator==(const TextStyle&) const = default;
};

// How a span of text looks, from the tags around it.
struct SpanStyle
{
    Color Color        = { 1, 1, 1, 1 };
    bool  HasColor     = false; // set by <color>; otherwise the text's own color
    f32   Size         = 1.0f;  // <size>: times the text's scale
    bool  Bold         = false;
    bool  Italic       = false;
    bool  Underline    = false;
    bool  Strike       = false;
    bool  Outline      = false;
    ::Arcbit::Color OutlineColor = { 0, 0, 0, 1 };
    bool  Shadow       = false;
    u8    Effects      = 0;     // TextEffect bits
    f32   Speed        = 1.0f;  // <speed>: reveal pace

    bool operator==(const SpanStyle&) const = default;
};

struct TextSpan
{
    std::string Text;
    SpanStyle   Style;
};

// A <pause>: a reveal waits Seconds once Index characters are shown.
struct TextPause
{
    u32 Index   = 0;
    f32 Seconds = 0.0f;
};

struct ParsedText
{
    std::vector<TextSpan>  Spans;
    std::vector<TextPause> Pauses;
};

// Split marked-up text into styled spans (and the pauses between them),
// starting from base (bold, italic, outline and shadow from a TextStyle).
[[nodiscard]] ParsedText ParseMarkup(std::string_view text, const SpanStyle& base = {});

// The text without its tags ("<b>Hi</b> <<3" -> "Hi <3").
[[nodiscard]] std::string StripMarkup(std::string_view text);

// One glyph, placed: relative to the layout's top-left.
struct LaidGlyph
{
    GlyphInfo Glyph;      // its bitmap's page and UVs, at the font's size
    Vec2      Position;   // the bitmap's top-left
    Vec2      Size;       // the bitmap, scaled
    f32       Baseline;   // its line's baseline (y)
    f32       Advance;    // scaled: where the next glyph starts
    f32       Pen;        // x where its advance starts (underlines run pen to pen)
    f32       Scale;      // the text's scale times its span's size
    u32       Index = 0;  // its character's place in the text: a reveal shows the first N
    u32       Span  = 0;  // its style: TextLayout::Spans
    u32       Line  = 0;
    bool      FauxBold   = false; // the font has no bold face for it: thickened instead
    bool      FauxItalic = false; // ... no italic face: slanted
};

struct TextLine
{
    f32 Left     = 0.0f; // after alignment
    f32 Width    = 0.0f; // without trailing spaces
    f32 Top      = 0.0f;
    f32 Height   = 0.0f;
    f32 Baseline = 0.0f;
};

struct TextLayout
{
    std::vector<TextSpan>  Spans;
    std::vector<LaidGlyph> Glyphs;    // only glyphs with a bitmap (not spaces)
    std::vector<TextLine>  Lines;
    std::vector<TextPause> Pauses;
    u32                    CharCount = 0; // every character, spaces and newlines included
    Vec2                   Size;          // the widest line, all lines' height
};

struct TextLayoutOptions
{
    f32       Scale     = 1.0f;
    f32       WrapWidth = 0.0f;  // > 0: wrap lines to this width
    f32       BoxWidth  = 0.0f;  // the width Align aligns lines within (0: the widest line)
    TextAlign Align     = TextAlign::Left;
    bool      Markup    = true;  // false: tags are plain text
    TextStyle Style;             // the base look (bold, italic, outline, shadow)

    bool operator==(const TextLayoutOptions&) const = default;
};

// Lay out text (marked up or plain) into placed glyphs, wrapping like
// WrapText: at spaces, between CJK characters, never before closing
// punctuation, and inside a word wider than the line.
[[nodiscard]] TextLayout LayoutRichText(const FontAtlas& font, std::string_view text, const TextLayoutOptions& options);

struct TextDrawOptions
{
    Vec2  Position;                // the layout's top-left, in screen pixels
    Color Color   = { 1, 1, 1, 1 }; // for spans without their own; its alpha fades everything
    i32   Layer   = 0;
    u32   Visible = ~0u;           // characters shown (a typewriter's progress)
    u16   Clip    = 0;             // ScrollPanel clip index
    TextStyle     Style;           // outline width, shadow offset / color / softness
    TextureHandle White;           // a white texture and its sampler: underlines and
    SamplerHandle WhiteSampler;    //   strikethroughs (none drawn without one)
};

// Draw a layout as UI text (FramePacket::UISprites, SDF): shadows first,
// then the glyphs, then underlines and strikethroughs.
void DrawRichText(FramePacket& packet, const FontAtlas& font, const TextLayout& layout, const TextDrawOptions& options);

// A layout kept between frames, laid out again only when its text, options
// or the font's fallbacks change - what a label or button holds.
class CachedTextLayout
{
public:
    [[nodiscard]] const TextLayout& Get(const FontAtlas& font, std::string_view text, const TextLayoutOptions& options);

private:
    std::string       _text;
    const FontAtlas*  _font      = nullptr;
    u64               _fallbacks = 0;
    TextLayoutOptions _options;
    TextLayout        _layout;
};
} // namespace Arcbit
