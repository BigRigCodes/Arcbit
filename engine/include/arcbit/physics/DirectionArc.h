#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>

#include <vector>

namespace Arcbit
{
// One angular arc, expressed in the collider's local frame. The arc covers the
// range [CenterDegrees - HalfWidthDegrees, CenterDegrees + HalfWidthDegrees].
//
// Convention: degrees, with 0 = +X (east), 90 = +Y (south), -90 = north.
// A list of arcs on a Collider2D's BlockedFrom field describes the directions
// from which approaches will be treated as blocking contacts. Approaches whose
// contact direction does not fall inside any arc pass through the collider.
//
// Consumed by the resolver and PhysicsWorld::QueryTileBlocked via
// IsContactBlocked() below.
struct DirectionArc
{
    f32 CenterDegrees    = 0.0f;
    f32 HalfWidthDegrees = 180.0f; // 180 = full coverage (always block)

    // Field-wise equality; required for comparing BlockedFrom vectors when the
    // tile collider greedy-mesher decides whether two tiles can merge.
    bool operator==(const DirectionArc&) const = default;

    // Returns true when an arc spans the entire circle and never needs to be
    // tested in the narrowphase — useful as a fast-path check.
    [[nodiscard]] constexpr bool IsFullCoverage() const noexcept { return HalfWidthDegrees >= 180.0f; }

    // ---- Preset library --------------------------------------------------
    // Defaults are tuned per docs/physics.md: tree-style props get ±60°
    // arcs (leaving a clear ±30° lane on each side), single-direction
    // gates get ±90° (a clean half-plane).

    // Single arc covering the full 360°. Equivalent to leaving
    // Collider2D::BlockedFrom empty (the cheaper default).
    [[nodiscard]] static std::vector<DirectionArc> AllDirections() { return {{0.0f, 180.0f}}; }

    // Top + bottom arcs — tree-style, blocks N/S, lets E/W pass.
    [[nodiscard]] static std::vector<DirectionArc> Vertical(const f32 halfWidth = 60.0f)
    {
        return {{-90.0f, halfWidth}, {90.0f, halfWidth}};
    }

    // Left + right arcs — bush-style, blocks E/W, lets N/S pass.
    [[nodiscard]] static std::vector<DirectionArc> Horizontal(const f32 halfWidth = 60.0f)
    {
        return {{0.0f, halfWidth}, {180.0f, halfWidth}};
    }

    // One-sided gates — useful for ledges, cliff edges, north-facing barriers.
    [[nodiscard]] static std::vector<DirectionArc> NorthOnly(const f32 halfWidth = 90.0f) { return {{-90.0f, halfWidth}}; }
    [[nodiscard]] static std::vector<DirectionArc> SouthOnly(const f32 halfWidth = 90.0f) { return {{90.0f, halfWidth}}; }
    [[nodiscard]] static std::vector<DirectionArc> EastOnly(const f32  halfWidth = 90.0f) { return {{0.0f, halfWidth}}; }
    [[nodiscard]] static std::vector<DirectionArc> WestOnly(const f32  halfWidth = 90.0f) { return {{180.0f, halfWidth}}; }
};

// Decide whether a contact at `contactDirWorld` (unit vector pointing from
// the obstacle outward toward the mover's pre-contact side — the same vector
// Sweep::Result.Normal carries) should be treated as blocking, given the
// obstacle's `BlockedFrom` arcs and its world-space `obstacleRotation`
// (radians).
//
// Empty arcs list → returns true (block-all default; matches the
// Collider2D::BlockedFrom doc convention). Any IsFullCoverage() arc in the
// list short-circuits to true. Otherwise, the contact direction is
// transformed into the obstacle's local frame (rotate by -rotation), and
// returned true iff its angle falls within any arc.
[[nodiscard]] bool IsContactBlocked(const std::vector<DirectionArc>& arcs,
                                    Vec2                             contactDirWorld,
                                    f32                              obstacleRotation) noexcept;
} // namespace Arcbit
