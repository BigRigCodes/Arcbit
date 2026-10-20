// Rich text: markup parsing, and laying styled text out into glyphs (CPU
// side, Roboto from the source tree; no device).

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/render/RichText.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
FontAtlas Roboto()
{
    FontAtlas font;
    REQUIRE(font.LoadFace(ArcbitTests::SourcePath("engine/assets/fonts/Roboto-Regular.ttf").string(), 24.0f, FontMode::SDF));
    return font;
}

std::vector<std::string> Texts(const ParsedText& parsed)
{
    std::vector<std::string> out;
    for (const TextSpan& span : parsed.Spans) out.push_back(span.Text);
    return out;
}

bool Same(const Color& a, const Color& b) { return a.R == b.R && a.G == b.G && a.B == b.B && a.A == b.A; }
} // namespace

TEST_CASE("Markup splits text into styled spans", "[render][richtext]")
{
    const ParsedText parsed = ParseMarkup("Take the <color=#ff0000>red</color> key");
    REQUIRE(Texts(parsed) == std::vector<std::string>{ "Take the ", "red", " key" });
    CHECK_FALSE(parsed.Spans[0].Style.HasColor);
    CHECK(parsed.Spans[1].Style.HasColor);
    CHECK(Same(parsed.Spans[1].Style.Color, { 1, 0, 0, 1 }));

    const ParsedText nested = ParseMarkup("<b>bold <i>both</i></b> <u>under</u><s>out</s>");
    REQUIRE(nested.Spans.size() == 5);
    CHECK((nested.Spans[0].Style.Bold && !nested.Spans[0].Style.Italic));
    CHECK((nested.Spans[1].Style.Bold && nested.Spans[1].Style.Italic));
    CHECK(nested.Spans[3].Style.Underline);
    CHECK(nested.Spans[4].Style.Strike);
}

TEST_CASE("Valued tags: colors by name and hex, size, outline, effects, speed", "[render][richtext]")
{
    const ParsedText parsed =
        ParseMarkup("<color=#f80>a</color><color=gold>b</color><size=1.5>c</size><outline=#00f>d</outline>"
                    "<shadow><wave><rainbow>e</rainbow></wave></shadow><speed=0.25>f</speed>");
    REQUIRE(Texts(parsed) == std::vector<std::string>{ "a", "b", "c", "d", "e", "f" });
    CHECK(Same(parsed.Spans[0].Style.Color, { 1, 0x88 / 255.0f, 0, 1 }));
    CHECK(parsed.Spans[1].Style.HasColor);
    CHECK(parsed.Spans[2].Style.Size == 1.5f);
    CHECK(parsed.Spans[3].Style.Outline);
    CHECK(Same(parsed.Spans[3].Style.OutlineColor, { 0, 0, 1, 1 }));
    CHECK(parsed.Spans[4].Style.Shadow);
    CHECK(parsed.Spans[4].Style.Effects == (EffectWave | EffectRainbow));
    CHECK(parsed.Spans[5].Style.Speed == 0.25f);
}

TEST_CASE("Only known tags are tags; << is a literal <", "[render][richtext]")
{
    CHECK(StripMarkup("a < b and c > d") == "a < b and c > d");
    CHECK(StripMarkup("<3 and <foo>bar</foo>") == "<3 and <foo>bar</foo>");
    CHECK(StripMarkup("<color=nonsense>x</color>") == "<color=nonsense>x"); // a bad value shows the mistake
    CHECK(StripMarkup("<<b>> is bold") == "<b>> is bold");
    CHECK(StripMarkup("</b>closing nothing") == "closing nothing");
    CHECK(StripMarkup("unclosed <b") == "unclosed <b");
    CHECK(StripMarkup("caf\xC3\xA9 <b>\xE6\x97\xA5</b>") == "caf\xC3\xA9 \xE6\x97\xA5"); // UTF-8 passes through
}

TEST_CASE("Pauses land between characters", "[render][richtext]")
{
    const ParsedText parsed = ParseMarkup("Hi<pause=0.5> th<pause>ere");
    REQUIRE(parsed.Pauses.size() == 2);
    CHECK(parsed.Pauses[0].Index == 2);
    CHECK(parsed.Pauses[0].Seconds == 0.5f);
    CHECK(parsed.Pauses[1].Index == 5);
    CHECK(parsed.Pauses[1].Seconds == 0.5f); // the default
}

TEST_CASE("Plain text lays out like the plain text drawing", "[render][richtext]")
{
    const FontAtlas  font   = Roboto();
    const TextLayout layout = LayoutRichText(font, "Hello there", { .Markup = false });
    CHECK(layout.Lines.size() == 1);
    CHECK(layout.CharCount == 11);
    CHECK(layout.Glyphs.size() == 10); // the space has no bitmap
    CHECK(layout.Size.X == MeasureTextWidth(font, "Hello there", 1.0f));
    CHECK(layout.Glyphs.back().Index == 10);
}

TEST_CASE("Styled runs wrap together; size scales a run", "[render][richtext]")
{
    const FontAtlas font = Roboto();
    const f32       word = MeasureTextWidth(font, "aaa", 1.0f);
    const TextLayout wrapped = LayoutRichText(font, "aaa <b>bbb</b> ccc", { .WrapWidth = word * 2.5f });
    REQUIRE(wrapped.Lines.size() == 2);
    CHECK(wrapped.Glyphs[6].Line == 1); // "c..." moved down; "aaa bbb" fits

    const TextLayout big = LayoutRichText(font, "<size=2>aaa</size>", {});
    CHECK(big.Size.X == word * 2.0f);
    CHECK(big.Lines[0].Height == font.GetLineHeight() * 2.0f);
}

TEST_CASE("Rich text keeps explicit newlines and aligns each line", "[render][richtext]")
{
    const FontAtlas  font   = Roboto();
    const TextLayout layout = LayoutRichText(font, "a\n\nbb", { .BoxWidth = 200.0f, .Align = TextAlign::Right });
    REQUIRE(layout.Lines.size() == 3);
    CHECK(layout.Lines[1].Width == 0.0f);
    CHECK(layout.Lines[2].Top == font.GetLineHeight() * 2.0f);
    CHECK(layout.Lines[0].Left + layout.Lines[0].Width == 200.0f);
    CHECK(layout.Lines[2].Left + layout.Lines[2].Width == 200.0f);
}

TEST_CASE("Rich text wraps between CJK characters and inside over-long words", "[render][richtext]")
{
    const FontAtlas font = Roboto();
    const f32       ch   = font.GetGlyph(0x3042)->Advance;
    // Three CJK characters then a closing full stop, two characters a line.
    const TextLayout cjk = LayoutRichText(font, "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x80\x82", { .WrapWidth = ch * 2.5f });
    REQUIRE(cjk.Lines.size() == 2);
    CHECK(cjk.Glyphs[2].Line == 1); // the third character moved down with the stop

    const f32        two  = MeasureTextWidth(font, "mm", 1.0f);
    const TextLayout long_ = LayoutRichText(font, "mmmmm", { .WrapWidth = two + 1.0f });
    CHECK(long_.Lines.size() == 3);
}

TEST_CASE("The pseudo-locale leaves tags working", "[render][richtext]")
{
    const std::string pseudo = Loc::Pseudo("Take the <color=gold>red</color> key");
    CHECK(pseudo.find("<color=gold>") != std::string::npos);
    CHECK(pseudo.find("</color>") != std::string::npos);
    CHECK(ParseMarkup(pseudo).Spans.size() == 3);
}

namespace
{
FontAtlas RobotoFamily()
{
    FontAtlas font = Roboto();
    font.SetStyleFace(FontStyle::Bold, FontFace::Load(ArcbitTests::SourcePath("engine/assets/fonts/Roboto-Bold.ttf").string()));
    font.SetStyleFace(FontStyle::Italic, FontFace::Load(ArcbitTests::SourcePath("engine/assets/fonts/Roboto-Italic.ttf").string()));
    return font;
}
} // namespace

TEST_CASE("Style faces supply bold and italic; anything else is faked", "[render][richtext]")
{
    const FontAtlas family = RobotoFamily();
    const auto      bold   = family.GetStyledGlyph('W', FontStyle::Bold);
    CHECK_FALSE(bold.FauxBold);
    CHECK(bold.Glyph != family.GetGlyph('W')); // its own glyph, from the bold face

    const auto both = family.GetStyledGlyph('W', FontStyle::BoldItalic); // no bold-italic face: bold, slanted
    CHECK(both.FauxItalic);
    CHECK_FALSE(both.FauxBold);

    const auto kana = family.GetStyledGlyph(0x3042, FontStyle::Bold); // not in Roboto Bold: the regular glyph, thickened
    CHECK(kana.FauxBold);

    const FontAtlas plain = Roboto();
    const auto      faux  = plain.GetStyledGlyph('W', FontStyle::Italic);
    CHECK(faux.FauxItalic);
    CHECK(faux.Glyph == plain.GetGlyph('W'));
}

TEST_CASE("Layout uses the style faces; a base style styles every span", "[render][richtext]")
{
    const FontAtlas  family = RobotoFamily();
    const TextLayout plain  = LayoutRichText(family, "Wow", {});
    const TextLayout bold   = LayoutRichText(family, "<b>Wow</b>", {});
    CHECK(bold.Glyphs[0].Glyph.UV.U0 != plain.Glyphs[0].Glyph.UV.U0); // the bold face's W, not the regular one
    CHECK_FALSE(bold.Glyphs[0].FauxBold);

    const TextLayout based = LayoutRichText(family, "a <i>b</i>", { .Style = { .Bold = true } });
    REQUIRE(based.Spans.size() == 2);
    CHECK(based.Spans[0].Style.Bold);
    CHECK((based.Spans[1].Style.Bold && based.Spans[1].Style.Italic));
    CHECK(based.Glyphs[1].FauxItalic); // bold italic from the bold face, slanted
}

TEST_CASE("A cached layout is laid out again only when something changes", "[render][richtext]")
{
    const FontAtlas   font = Roboto();
    CachedTextLayout  cache;
    const TextLayout* first = &cache.Get(font, "Hello", {});
    const u32         count = static_cast<u32>(first->Glyphs.size());
    CHECK(&cache.Get(font, "Hello", {}) == first);
    CHECK(cache.Get(font, "Hello!", {}).Glyphs.size() == count + 1);
    CHECK(cache.Get(font, "Hello!", { .Style = { .Bold = true } }).Spans[0].Style.Bold);
}
