#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>

namespace Arcbit
{
// Axis-aligned bounding box in world coordinates. Min and Max are inclusive
// on the low end and exclusive on the high end semantically (matches the
// half-open intervals used by the SpatialHash), but Overlaps treats them as
// closed since the difference is irrelevant under floating-point.
struct AABB
{
    Vec2 Min = {};
    Vec2 Max = {};

    [[nodiscard]] static constexpr AABB FromCenterHalfExtents(const Vec2 center,
                                                              const Vec2 halfExtents) noexcept
    {
        return {
            {center.X - halfExtents.X, center.Y - halfExtents.Y},
            {center.X + halfExtents.X, center.Y + halfExtents.Y}
        };
    }

    [[nodiscard]] static constexpr AABB FromMinMax(const Vec2 min, const Vec2 max) noexcept { return {min, max}; }

    [[nodiscard]] constexpr Vec2 Center() const noexcept { return {(Min.X + Max.X) * 0.5f, (Min.Y + Max.Y) * 0.5f}; }

    [[nodiscard]] constexpr Vec2 HalfExtents() const noexcept
    {
        return {(Max.X - Min.X) * 0.5f, (Max.Y - Min.Y) * 0.5f};
    }

    [[nodiscard]] constexpr Vec2 Size() const noexcept { return {Max.X - Min.X, Max.Y - Min.Y}; }

    [[nodiscard]] constexpr bool Overlaps(const AABB& o) const noexcept
    {
        return !(Max.X < o.Min.X || Min.X > o.Max.X ||
            Max.Y < o.Min.Y || Min.Y > o.Max.Y);
    }

    [[nodiscard]] constexpr bool Contains(const Vec2 p) const noexcept
    {
        return p.X >= Min.X && p.X <= Max.X && p.Y >= Min.Y && p.Y <= Max.Y;
    }

    // Sanity check; well-formed AABBs have Min <= Max component-wise.
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Min.X <= Max.X && Min.Y <= Max.Y; }
};
} // namespace Arcbit
