#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/physics/AABB.h>

#include <unordered_map>
#include <vector>

namespace Arcbit
{
// Uniform-grid spatial hash for the physics broadphase. Each grid cell holds
// the list of collider IDs whose AABB touches that cell.
//
// Cell size is fixed at construction (typically the tilemap tile size, so the
// hash naturally aligns with tile-synthesized colliders). The hash itself is
// agnostic to what a ColliderId means — PhysicsWorld owns the mapping back
// to entities.
//
// Insert/Remove must be paired with the *same* AABB that was inserted (the
// hash doesn't store AABBs internally; the caller tells the hash which cells
// to touch on removal). When a collider moves, callers should Remove with the
// old AABB and Insert with the new one.
class SpatialHash
{
public:
    using ColliderId = u32;

    explicit SpatialHash(f32 cellSize);

    void Insert(ColliderId id, const AABB& aabb);
    void Remove(ColliderId id, const AABB& aabb);
    void Clear() noexcept;

    // Clears `out` and fills it with the unique collider IDs whose stored
    // cells overlap `queryAABB`. Results are deduplicated and sorted ascending.
    // Broadphase only — callers must run a narrowphase test on each result.
    void Query(const AABB& queryAABB, std::vector<ColliderId>& out) const;

    [[nodiscard]] f32   CellSize() const noexcept { return _cellSize; }
    [[nodiscard]] usize CellCount() const noexcept { return _cells.size(); }

private:
    struct CellRange
    {
        i32 X0, Y0, X1, Y1;
    };

    [[nodiscard]] CellRange CellsOf(const AABB& aabb) const noexcept;

    [[nodiscard]] static constexpr u64 PackKey(const i32 x, const i32 y) noexcept
    {
        return (static_cast<u64>(static_cast<u32>(x)) << 32) |
                static_cast<u64>(static_cast<u32>(y));
    }

    f32 _cellSize;
    f32 _invCellSize;

    std::unordered_map<u64, std::vector<ColliderId>> _cells;
};
} // namespace Arcbit
