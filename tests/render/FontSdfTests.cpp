// The distance field built from a large coverage bitmap (for CFF fonts).

#include "FontSdf.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace Arcbit;

namespace
{
constexpr u8  OnEdge = 128;
constexpr f32 Scale  = 64.0f;

// A solid size x size square, drawn upscale times larger.
SdfBitmap Square(const u32 size, const u32 upscale, const u32 padding)
{
    const u32       n = size * upscale;
    std::vector<u8> coverage(static_cast<usize>(n) * n, 255);
    return SdfFromCoverage(coverage.data(), n, n, upscale, padding, OnEdge, Scale);
}
} // namespace

TEST_CASE("SdfFromCoverage pads the output and follows stb's convention", "[render][font]")
{
    const SdfBitmap sdf = Square(8, 4, 4);
    REQUIRE(sdf.Width == 16);
    REQUIRE(sdf.Height == 16);
    const auto at = [&](const u32 x, const u32 y) { return sdf.Pixels[y * sdf.Width + x]; };

    CHECK(at(0, 0) == 0);      // far outside: clamped
    CHECK(at(8, 8) == 255);    // deep inside: clamped
    CHECK(at(3, 8) < OnEdge);  // half a pixel outside the left edge
    CHECK(at(4, 8) > OnEdge);  // half a pixel inside it
    CHECK(at(3, 8) > 64);      // ...and both near the edge value
    CHECK(at(4, 8) < 192);
    CHECK(at(2, 8) < at(3, 8)); // rising toward the inside
    CHECK(at(4, 8) < at(5, 8));
}

TEST_CASE("SdfFromCoverage measures a hole as outside", "[render][font]")
{
    // A ring: 16 cells across with an 8-cell hole, upscale 2 -> 8 output pixels plus padding.
    constexpr u32   n = 16;
    std::vector<u8> coverage(n * n, 255);
    for (u32 y = 4; y < 12; ++y)
        for (u32 x = 4; x < 12; ++x) coverage[y * n + x] = 0;
    const SdfBitmap sdf = SdfFromCoverage(coverage.data(), n, n, 2, 2, OnEdge, Scale);
    CHECK(sdf.Pixels[6 * sdf.Width + 6] < OnEdge); // the hole's middle
    CHECK(sdf.Pixels[3 * sdf.Width + 6] > OnEdge); // the ring
}
