// Font baking and text layout, CPU side only (no GPU device): glyph coverage,
// pages, the missing-glyph box, UTF-8 measuring and word wrap.

#include "TestSupport.h"

#include <arcbit/core/Utf8.h>
#include <arcbit/render/Font.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Arcbit;

namespace
{
// Escaped so the source stays ASCII.
constexpr const char* Cafe    = "caf\xC3\xA9";     // "cafe" with an acute e
constexpr u32         JaA     = 0x3042;            // Japanese "a" - not in Roboto
constexpr const char* JaThree = "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"; // three of it
constexpr const char* JaStop  = "\xE3\x80\x82";    // ideographic full stop

FontAtlas Roboto(const f32 pixelSize = 24.0f)
{
    FontAtlas font;
    REQUIRE(font.LoadFace(ArcbitTests::SourcePath("engine/assets/fonts/Roboto-Regular.ttf").string(), pixelSize, FontMode::SDF));
    return font;
}

f32 Advance(const FontAtlas& font, const u32 cp) { return font.GetGlyph(cp)->Advance; }
} // namespace

TEST_CASE("The default glyph set covers European languages on one page", "[render][font]")
{
    const FontAtlas font = Roboto();
    for (const u32 cp : { u32('A'), 0xE9u, 0xE7u, 0x153u, 0xABu, 0x2019u, 0x2026u, 0x20ACu, 0x202Fu })
        CHECK(font.HasGlyph(cp));
    CHECK(font.GetPageCount() == 1);
}

TEST_CASE("A codepoint the font lacks draws as the missing-glyph box", "[render][font]")
{
    const FontAtlas font = Roboto();
    CHECK_FALSE(font.HasGlyph(JaA));
    const GlyphInfo* box = font.GetGlyph(JaA);
    REQUIRE(box);
    CHECK(box == font.GetGlyph(0x4E00)); // every missing codepoint shares it
    CHECK(box->BitmapSize.X > 0.0f);
    CHECK(box->Advance > 0.0f);
    CHECK_FALSE(FontAtlas{}.GetGlyph('A')); // nothing before the font loads
}

TEST_CASE("UI arrows are drawn when the font has none", "[render][font]")
{
    const FontAtlas font = Roboto();
    for (const u32 cp : { 0x25B2u, 0x25B6u, 0x25BCu, 0x25C0u }) {
        REQUIRE(font.HasGlyph(cp));
        CHECK(font.GetGlyph(cp)->BitmapSize.X > 0.0f);
    }
}

TEST_CASE("Glyphs that don't fit a page spill onto another", "[render][font]")
{
    const FontAtlas font = Roboto(96.0f);
    REQUIRE(font.GetPageCount() > 1);
    const GlyphInfo* last = font.GetGlyph(0x2122); // the last one baked
    CHECK(last->Page == font.GetPageCount() - 1);
    CHECK(last->UV.U1 <= 1.0f);
    CHECK(last->UV.V1 <= 1.0f);
}

TEST_CASE("Text is measured by codepoint, not byte", "[render][font]")
{
    const FontAtlas font = Roboto();
    const f32 expected = Advance(font, 'c') + Advance(font, 'a') + Advance(font, 'f') + Advance(font, 0xE9);
    CHECK(MeasureTextWidth(font, Cafe, 1.0f) == expected);
    CHECK(MeasureTextWidth(font, Cafe, 2.0f) == expected * 2.0f);
}

TEST_CASE("WrapText breaks at spaces and keeps explicit newlines", "[render][font]")
{
    const FontAtlas font = Roboto();
    const f32       word = MeasureTextWidth(font, "aaa", 1.0f);
    CHECK(WrapText(font, "aaa bbb", word * 1.5f, 1.0f) == "aaa\nbbb");
    CHECK(WrapText(font, "aaa bbb", word * 3.0f, 1.0f) == "aaa bbb");
    CHECK(WrapText(font, "\naaa\n\nbbb", word * 3.0f, 1.0f) == "\naaa\n\nbbb");
}

TEST_CASE("WrapText breaks between CJK characters, never before closing punctuation", "[render][font]")
{
    const FontAtlas font = Roboto();
    const f32       ch   = Advance(font, JaA);
    const std::string a  = "\xE3\x81\x82";

    CHECK(WrapText(font, JaThree, ch * 2.5f, 1.0f) == a + a + "\n" + a);
    // The full stop rides with the character before it onto the next line.
    CHECK(WrapText(font, a + a + JaStop, ch * 2.5f, 1.0f) == a + "\n" + a + JaStop);
}

TEST_CASE("WrapText breaks a word wider than the line between its characters", "[render][font]")
{
    const FontAtlas font = Roboto();
    const f32       two  = MeasureTextWidth(font, "mm", 1.0f);
    CHECK(WrapText(font, "mmmmm", two + 1.0f, 1.0f) == "mm\nmm\nm");
    CHECK(WrapText(font, "a mmmmm", two + 1.0f, 1.0f) == "a\nmm\nmm\nm");
}

namespace
{
std::shared_ptr<FontFace> NotoJP()
{
    auto face = FontFace::Load(ArcbitTests::SourcePath("game/assets/fonts/NotoSansJP-Regular.otf").string());
    REQUIRE(face);
    return face;
}
} // namespace

TEST_CASE("A codepoint outside the baked set is baked the first time it's asked for", "[render][font]")
{
    const FontAtlas font = Roboto();
    constexpr u32   Zhe  = 0x0416; // Cyrillic: Roboto has it, the default set doesn't
    CHECK_FALSE(font.HasGlyph(Zhe));
    const GlyphInfo* g = font.GetGlyph(Zhe);
    CHECK(font.HasGlyph(Zhe));
    CHECK(g != font.GetGlyph(0x4E00)); // not the box
    CHECK(g->BitmapSize.X > 0.0f);
}

TEST_CASE("Fallback faces supply what the font lacks, at the atlas's size", "[render][font]")
{
    FontAtlas        font = Roboto();
    const GlyphInfo* box  = font.GetGlyph(JaA);
    const f32        a    = Advance(font, 'A');

    font.AddFallback(NotoJP());
    const GlyphInfo* ja = font.GetGlyph(JaA); // the box is forgotten once a fallback arrives
    CHECK(ja != box);
    CHECK(ja->BitmapSize.X > 0.0f);
    CHECK(ja->Advance > 19.0f); // a full-width character is about an em: ~20.5px for Roboto at 24px
    CHECK(ja->Advance < 22.0f);
    CHECK(Advance(font, 'A') == a); // the font's own glyphs still come first

    font.ClearFallbacks();
    CHECK(font.GetGlyph(JaA) == ja);    // baked glyphs stay
    CHECK(font.GetGlyph(0x65E5) == box); // nothing new comes from a removed face
}

TEST_CASE("Prepare bakes a text's glyphs ahead, onto new pages as needed", "[render][font]")
{
    FontAtlas font = Roboto(96.0f);
    font.AddFallback(NotoJP());
    const u32 pages = font.GetPageCount();

    std::string text;
    for (u32 cp = 0x4E00; cp < 0x4E00 + 200; ++cp) Utf8::Append(text, cp); // 200 kanji
    font.Prepare(text);
    CHECK(font.HasGlyph(0x4E00 + 199));
    CHECK(font.GetPageCount() > pages);
}

TEST_CASE("FontFace refuses a file that isn't a font", "[render][font]")
{
    ArcbitTests::TempDir dir;
    CHECK_FALSE(FontFace::Load(dir.Write("fake.ttf", "not a font")));
    CHECK_FALSE(FontFace::Load(dir.Write("tiny.ttf", "ab")));
    CHECK_FALSE(FontFace::Load(dir.File("missing.ttf").string()));
}

TEST_CASE("Shared fallbacks reach every atlas, loaded before or after", "[render][font]")
{
    struct Reset { ~Reset() { FontAtlas::SetSharedFallbacks({}); } } reset;
    const FontAtlas before = Roboto();
    CHECK(before.GetGlyph(JaA) == before.GetGlyph(0x4E00)); // the box, remembered

    FontAtlas::SetSharedFallbacks({ NotoJP() });
    const FontAtlas after = Roboto(32.0f);
    CHECK(before.GetGlyph(JaA)->BitmapSize.X > 0.0f);
    CHECK(before.GetGlyph(JaA) != before.GetGlyph(0x10FFFD)); // no longer the box
    CHECK(after.GetGlyph(JaA) != after.GetGlyph(0x10FFFD));
}
