#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/Collider2D.h>

#include <cmath>

namespace Arcbit
{
// Oriented bounding box in world space — the narrowphase's working form of a
// Box collider. An axis-aligned box is simply an OBB with AxisX = (1, 0), so
// rotated-vs-unrotated pairs share the SAT code without a special case.
//
// Rotation convention matches Transform2D, the sprite shader, and
// IsContactBlocked: local → world is [c -s; s c] in the engine's +Y-down
// space, so positive angles read clockwise on screen. AxisX is the box's
// local +X in world space, AxisY its local +Y; both are unit length.
struct OBB
{
    Vec2 Center      = {};
    Vec2 AxisX       = { 1.0f, 0.0f };
    Vec2 AxisY       = { 0.0f, 1.0f };
    Vec2 HalfExtents = {};

    // Build from a Box collider at `worldPos` (Offset applied, unrotated by
    // the collider's own Rotation — matches ComputeWorldAABB). Skips the trig
    // when Rotation is zero.
    [[nodiscard]] static OBB FromCollider(const Collider2D& collider, const Vec2 worldPos) noexcept
    {
        OBB box;
        box.Center      = { worldPos.X + collider.Offset.X, worldPos.Y + collider.Offset.Y };
        box.HalfExtents = collider.HalfExtents;
        if (collider.Rotation != 0.0f) {
            const f32 c = std::cos(collider.Rotation);
            const f32 s = std::sin(collider.Rotation);
            box.AxisX   = { c, s };
            box.AxisY   = { -s, c };
        }
        return box;
    }

    // World point → box-local coordinates (origin at Center, axes aligned
    // with the box). Inverse of the rotation is the transpose, hence dots.
    [[nodiscard]] Vec2 ToLocal(const Vec2 worldPoint) const noexcept
    {
        const Vec2 d = worldPoint - Center;
        return { Dot(d, AxisX), Dot(d, AxisY) };
    }

    // Box-local direction → world space.
    [[nodiscard]] Vec2 ToWorldDir(const Vec2 localDir) const noexcept
    {
        return AxisX * localDir.X + AxisY * localDir.Y;
    }

    // Half the box's extent when projected onto unit `axis` — the SAT
    // "radius" of the box along that axis.
    [[nodiscard]] f32 ProjectedRadius(const Vec2 axis) const noexcept
    {
        return HalfExtents.X * std::abs(Dot(AxisX, axis)) + HalfExtents.Y * std::abs(Dot(AxisY, axis));
    }
};
} // namespace Arcbit
