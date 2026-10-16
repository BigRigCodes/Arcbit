#include <arcbit/physics/DirectionArc.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <cmath>

namespace Arcbit
{
namespace
{
    // RadToDeg / Pi etc. live in core/Math.h.
    constexpr f32 FullTurn = 360.0f;
    constexpr f32 HalfTurn = 180.0f;

    // Wrap an angular delta into [-180, 180] so the abs() comparison against
    // an arc's half-width handles the 180/-180 boundary cleanly.
    inline f32 WrapDelta(f32 delta) noexcept
    {
        if (delta > HalfTurn) delta  -= FullTurn;
        if (delta < -HalfTurn) delta += FullTurn;
        return delta;
    }
} // anonymous namespace

bool IsContactBlocked(const std::vector<DirectionArc>& arcs,
                      const Vec2                       contactDirWorld,
                      const f32                        obstacleRotation) noexcept
{
    // Default convention: empty list = block all directions.
    if (arcs.empty()) return true;

    // Fast path — any single full-coverage arc trivially blocks.
    for (const auto& arc : arcs)
        if (arc.IsFullCoverage()) return true;

    // Convert the contact direction to the obstacle's local frame. Rotation
    // is the world-space rotation of the obstacle; rotating the contact by
    // -rotation expresses it in the local frame the arcs are defined in.
    f32 localX = contactDirWorld.X;
    f32 localY = contactDirWorld.Y;
    if (obstacleRotation != 0.0f) {
        const f32 c = std::cos(-obstacleRotation);
        const f32 s = std::sin(-obstacleRotation);
        localX = c * contactDirWorld.X - s * contactDirWorld.Y;
        localY = s * contactDirWorld.X + c * contactDirWorld.Y;
    }

    // Convert to degrees in [-180, 180]. atan2 returns radians in [-π, π].
    const f32 angleDeg = std::atan2(localY, localX) * RadToDeg;

    // Strict `<`: a contact exactly on the half-plane boundary (e.g. pure
    // east/west on a NorthOnly halfwidth-90° arc) is NOT inside the arc.
    // That matches "only the north face blocks" — a perpendicular contact is
    // east or west, not north — and is what tile-aligned movement needs so
    // sliding past a directional tile from the side isn't falsely blocked.
    for (const auto& arc : arcs) {
        const f32 delta = WrapDelta(angleDeg - arc.CenterDegrees);
        if (std::abs(delta) < arc.HalfWidthDegrees) return true;
    }
    return false;
}

} // namespace Arcbit
