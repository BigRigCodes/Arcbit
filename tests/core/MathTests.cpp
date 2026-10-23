#include "TestSupport.h"

#include <arcbit/core/Math.h>
#include <arcbit/render/RenderHandle.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

TEST_CASE("Vec2 arithmetic and comparison", "[core][math]")
{
    constexpr Vec2 a{ 3.0f, -2.0f };
    constexpr Vec2 b{ 1.0f, 4.0f };

    STATIC_CHECK(a + b == Vec2{ 4.0f, 2.0f }); // constexpr-usable
    CHECK(a - b == Vec2{ 2.0f, -6.0f });
    CHECK(a * 2.0f == Vec2{ 6.0f, -4.0f });
    CHECK(a / 2.0f == Vec2{ 1.5f, -1.0f });
    CHECK(a != b);
    CHECK(Vec2{} == Vec2{ 0.0f, 0.0f });

    Vec2 c = a;
    c += b;
    CHECK(c == Vec2{ 4.0f, 2.0f });
    c -= b;
    CHECK(c == a);
    c *= 3.0f;
    CHECK(c == Vec2{ 9.0f, -6.0f });
    c /= 3.0f;
    CHECK(c == a);
}

TEST_CASE("Vec2 indexing aliases the named components", "[core][math]")
{
    Vec2 v{ 5.0f, 7.0f };
    CHECK(v[0] == 5.0f);
    CHECK(v[1] == 7.0f);
    v[1] = 9.0f;
    CHECK(v.Y == 9.0f);
    CHECK(v.Data()[0] == 5.0f);
}

TEST_CASE("Dot, Length, LengthSquared", "[core][math]")
{
    STATIC_CHECK(Dot({ 1.0f, 2.0f }, { 3.0f, 4.0f }) == 11.0f);
    CHECK(Dot({ 1.0f, 0.0f }, { 0.0f, 1.0f }) == 0.0f); // perpendicular
    CHECK(Dot({ 2.0f, 0.0f }, { -3.0f, 0.0f }) == -6.0f);

    STATIC_CHECK(LengthSquared({ 3.0f, 4.0f }) == 25.0f);
    CHECK(Length({ 3.0f, 4.0f }) == 5.0f);
    CHECK(Length({}) == 0.0f);
    CHECK_THAT(Length({ 1.0f, 1.0f }), WithinAbs(1.41421356f, 1e-6f));
}

TEST_CASE("Vec3 and Vec4 arithmetic", "[core][math]")
{
    constexpr Vec3 a{ 1.0f, 2.0f, 3.0f };
    CHECK(a + Vec3{ 1.0f, 1.0f, 1.0f } == Vec3{ 2.0f, 3.0f, 4.0f });
    CHECK(a - a == Vec3{ 0.0f, 0.0f, 0.0f });
    CHECK(a * 2.0f == Vec3{ 2.0f, 4.0f, 6.0f });
    CHECK(a[2] == 3.0f);

    constexpr Vec4 b{ 1.0f, 2.0f, 3.0f, 4.0f };
    CHECK(b / 2.0f == Vec4{ 0.5f, 1.0f, 1.5f, 2.0f });
    CHECK(b[3] == 4.0f);
    CHECK(Vec4{}.W == 1.0f); // homogeneous default
}

TEST_CASE("Color defaults, presets, and indexing", "[core][math]")
{
    CHECK(Color{}.A == 1.0f); // opaque black by default
    CHECK(Color{ 0.1f, 0.2f, 0.3f }.A == 1.0f);
    CHECK(Color::White() == Color{ 1.0f, 1.0f, 1.0f, 1.0f });
    CHECK(Color::Transparent().A == 0.0f);
    CHECK(Color::WarmOrange() == Color::NaturalLight());

    Color c = Color::Red();
    CHECK(c[0] == 1.0f);
    c[3] = 0.5f;
    CHECK(c.A == 0.5f);
}

TEST_CASE("Color decodes sRGB to linear, alpha untouched", "[core][math]")
{
    CHECK(Color::White().ToLinear() == Color::White()); // 0 and 1 are fixed points
    CHECK(Color::Black().ToLinear() == Color::Black());

    const Color gray = Color{ 0.5f, 0.5f, 0.5f, 0.5f }.ToLinear();
    CHECK_THAT(gray.R, WithinAbs(0.21404f, 1e-4f)); // mid gray is about 21% of the light
    CHECK(gray.A == 0.5f);

    CHECK_THAT(Color::SrgbToLinear(0.04f), WithinAbs(0.04f / 12.92f, 1e-7f)); // the linear toe
    CHECK_THAT(Color::SrgbToLinear(22.0f / 255.0f), WithinAbs(0.00802f, 1e-5f));
    CHECK(Color::SrgbToLinear(2.0f) == 2.0f); // an over-bright light color passes through
}

TEST_CASE("Angle constants agree", "[core][math]")
{
    CHECK_THAT(180.0f * DegToRad, WithinAbs(Pi, 1e-6f));
    CHECK_THAT(Pi * RadToDeg, WithinAbs(180.0f, 1e-4f));
    CHECK(TwoPi == 2.0f * Pi);
    CHECK(HalfPi == Pi / 2.0f);
}

TEST_CASE("Render handles pack index and generation", "[core][handles]")
{
    const TextureHandle h = TextureHandle::Make(42, 7);
    CHECK(h.Index() == 42);
    CHECK(h.Generation() == 7);
    CHECK(h.IsValid());

    CHECK_FALSE(TextureHandle::Invalid().IsValid());
    CHECK_FALSE(TextureHandle{}.IsValid());
    CHECK_FALSE(TextureHandle::Make(0, 3).IsValid()); // index 0 is reserved as "null"

    // Generation wraps within its 12 bits; index is masked to 20 bits.
    CHECK(TextureHandle::Make(1, TextureHandle::GenMask + 2).Generation() == 1);
    CHECK(TextureHandle::Make(TextureHandle::IndexMask + 2, 0).Index() == 1);

    CHECK(TextureHandle::Make(5, 1) != TextureHandle::Make(5, 2)); // stale generation differs
}
