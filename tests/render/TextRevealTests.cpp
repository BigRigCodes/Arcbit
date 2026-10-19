// The typewriter (TextReveal) and the animated effects, headless: a layout
// from Roboto in the source tree, stepped by hand.

#include "TestSupport.h"

#include <arcbit/render/RichText.h>
#include <arcbit/render/TextReveal.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
TextLayout Lay(const char* text)
{
    static FontAtlas font = [] {
        FontAtlas f;
        REQUIRE(f.LoadFace(ArcbitTests::SourcePath("engine/assets/fonts/Roboto-Regular.ttf").string(), 24.0f, FontMode::SDF));
        return f;
    }();
    return LayoutRichText(font, text, {});
}
} // namespace

TEST_CASE("A reveal shows characters at its pace, the next one partly", "[render][reveal]")
{
    const TextLayout layout = Lay("Hello");
    TextReveal       reveal;
    reveal.CharsPerSecond = 10.0f;
    CHECK_FALSE(reveal.IsStarted());
    reveal.Start();
    reveal.Update(0.25f, layout);
    CHECK_THAT(reveal.GetVisible(), WithinAbs(2.5, 0.001));
    CHECK_FALSE(reveal.IsDone());
    reveal.Update(1.0f, layout);
    CHECK(reveal.IsDone());
    CHECK(reveal.GetVisible() == 5.0f);
}

TEST_CASE("A reveal waits at a pause and paces a slow span", "[render][reveal]")
{
    const TextLayout layout = Lay("ab<pause=0.5>c<speed=0.5>de</speed>");
    TextReveal       reveal;
    reveal.CharsPerSecond = 10.0f;
    reveal.Start();
    reveal.Update(0.2f, layout); // "ab", then the pause begins
    CHECK(reveal.GetVisible() == 2.0f);
    reveal.Update(0.4f, layout); // still waiting
    CHECK(reveal.GetVisible() == 2.0f);
    reveal.Update(0.2f, layout); // 0.1 more of the pause, then 0.1 s of "c"
    CHECK_THAT(reveal.GetVisible(), WithinAbs(3.0, 0.001));
    reveal.Update(0.2f, layout); // half speed: one more character
    CHECK_THAT(reveal.GetVisible(), WithinAbs(4.0, 0.001));
}

TEST_CASE("Voice blips skip spaces and keep their gap; Skip finishes at once", "[render][reveal]")
{
    const TextLayout  layout = Lay("a b c");
    TextReveal        reveal;
    std::vector<u32>  blips;
    int               done = 0;
    reveal.CharsPerSecond = 100.0f;
    reveal.BlipGap        = 0.0f;
    reveal.OnBlip         = [&](const u32 cp) { blips.push_back(cp); };
    reveal.OnDone         = [&] { ++done; };
    reveal.Start();
    reveal.Update(1.0f, layout);
    CHECK(blips == std::vector<u32>{ 'a', 'b', 'c' });
    CHECK(done == 1);

    blips.clear();
    reveal.BlipGap = 1.0f; // at most one a second
    reveal.Start();
    reveal.Update(1.0f, layout);
    CHECK(blips.size() == 1);

    reveal.Start();
    reveal.Skip(layout);
    CHECK(reveal.IsDone());
    CHECK(reveal.GetVisible() == 5.0f);
    CHECK(done == 3);
}

TEST_CASE("Effects move and color glyphs over time", "[render][reveal]")
{
    CHECK(EffectOffset(0, 3, 1.0f, 30.0f) == Vec2{});
    const Vec2 wave1 = EffectOffset(EffectWave, 0, 0.0f, 30.0f), wave2 = EffectOffset(EffectWave, 0, 0.3f, 30.0f);
    CHECK(wave1.X == 0.0f);
    CHECK(wave1.Y != wave2.Y);
    CHECK(std::abs(wave2.Y) <= 3.0f); // a tenth of the line

    const Vec2 shake = EffectOffset(EffectShake, 7, 2.0f, 30.0f);
    CHECK(std::abs(shake.X) <= 1.5f);
    CHECK(shake == EffectOffset(EffectShake, 7, 2.01f, 30.0f)); // steady within a twentieth of a second
    CHECK(EffectTint(0, 0, 0.0f, Color{ 1, 0, 0, 0.5f }) == Color{ 1, 0, 0, 0.5f });
    const Color a = EffectTint(EffectRainbow, 0, 0.0f, Color{ 1, 1, 1, 0.5f });
    CHECK(a.A == 0.5f);
    CHECK_FALSE(a == EffectTint(EffectRainbow, 5, 0.0f, Color{ 1, 1, 1, 0.5f })); // hue moves along the text
}
