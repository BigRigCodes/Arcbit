#include "FontSdf.h"

#include <algorithm>
#include <cmath>

namespace Arcbit
{
namespace
{
constexpr f32 Far = 1e20f;

// Felzenszwalb & Huttenlocher's exact squared distance transform in one
// dimension: d[q] = min over p of (q - p)^2 + f[p]. v, z are scratch.
void Transform1D(const f32* f, const u32 n, f32* d, u32* v, f32* z)
{
    u32 k = 0;
    v[0]  = 0;
    z[0]  = -Far;
    z[1]  = Far;
    // Where the parabolas rooted at q and p cross. Always above -Far, so the
    // loop below stops at k = 0.
    const auto cross = [f](const u32 q, const u32 p) {
        const auto fq = static_cast<f32>(q), fp = static_cast<f32>(p);
        return ((f[q] + fq * fq) - (f[p] + fp * fp)) / (2.0f * fq - 2.0f * fp);
    };
    for (u32 q = 1; q < n; ++q) {
        f32 s = cross(q, v[k]);
        while (s <= z[k]) s = cross(q, v[--k]);
        ++k;
        v[k]     = q;
        z[k]     = s;
        z[k + 1] = Far;
    }
    k = 0;
    for (u32 q = 0; q < n; ++q) {
        while (z[k + 1] < static_cast<f32>(q)) ++k;
        const f32 dq = static_cast<f32>(q) - static_cast<f32>(v[k]);
        d[q]         = dq * dq + f[v[k]];
    }
}

// Squared distance from every cell to the nearest cell where `feature` is
// true: columns, then rows.
std::vector<f32> Transform2D(const std::vector<bool>& feature, const u32 w, const u32 h)
{
    std::vector<f32> grid(feature.size());
    for (usize i = 0; i < feature.size(); ++i) grid[i] = feature[i] ? 0.0f : Far;

    const u32        n = std::max(w, h);
    std::vector<f32> f(n), d(n), z(n + 1);
    std::vector<u32> v(n);
    for (u32 x = 0; x < w; ++x) {
        for (u32 y = 0; y < h; ++y) f[y] = grid[y * w + x];
        Transform1D(f.data(), h, d.data(), v.data(), z.data());
        for (u32 y = 0; y < h; ++y) grid[y * w + x] = d[y];
    }
    for (u32 y = 0; y < h; ++y) {
        Transform1D(&grid[y * w], w, d.data(), v.data(), z.data());
        std::copy_n(d.begin(), w, grid.begin() + static_cast<isize>(y) * w);
    }
    return grid;
}
} // namespace

SdfBitmap SdfFromCoverage(const u8* coverage, const u32 w, const u32 h, const u32 upscale, const u32 padding,
                          const u8 onEdge, const f32 distScale)
{
    // The large grid, padded so distances reach out past the glyph.
    const u32         pad = padding * upscale;
    const u32         gw = w + pad * 2, gh = h + pad * 2;
    std::vector<bool> inside(static_cast<usize>(gw) * gh, false);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) inside[(y + pad) * gw + x + pad] = coverage[y * w + x] >= 128;
    std::vector<bool> outside(inside.size());
    for (usize i = 0; i < inside.size(); ++i) outside[i] = !inside[i];

    const std::vector<f32> toInside  = Transform2D(inside, gw, gh);  // from outside cells
    const std::vector<f32> toOutside = Transform2D(outside, gw, gh); // from inside cells

    // Sample at each output pixel's center; the outline lies half a cell
    // between an inside and an outside cell.
    SdfBitmap out{ .Width = (gw + upscale - 1) / upscale, .Height = (gh + upscale - 1) / upscale };
    out.Pixels.resize(static_cast<usize>(out.Width) * out.Height);
    for (u32 y = 0; y < out.Height; ++y)
        for (u32 x = 0; x < out.Width; ++x) {
            const u32   sx = std::min(x * upscale + upscale / 2, gw - 1), sy = std::min(y * upscale + upscale / 2, gh - 1);
            const usize i  = static_cast<usize>(sy) * gw + sx;
            const f32   cells = inside[i] ? -(std::sqrt(toOutside[i]) - 0.5f) : std::sqrt(toInside[i]) - 0.5f;
            const f32   value = static_cast<f32>(onEdge) - cells / static_cast<f32>(upscale) * distScale;
            out.Pixels[static_cast<usize>(y) * out.Width + x] = static_cast<u8>(std::clamp(value, 0.0f, 255.0f));
        }
    return out;
}
} // namespace Arcbit
