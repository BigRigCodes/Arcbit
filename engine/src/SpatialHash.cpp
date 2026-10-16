#include <arcbit/physics/SpatialHash.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <algorithm>
#include <cmath>

namespace Arcbit
{
SpatialHash::SpatialHash(const f32 cellSize) : _cellSize(cellSize),
                                               _invCellSize(1.0f / cellSize)
{
    ARCBIT_ASSERT(cellSize > 0.0f, "SpatialHash cell size must be positive");
}

void SpatialHash::Insert(const ColliderId id, const AABB& aabb)
{
    ARCBIT_ASSERT(aabb.IsValid(), "SpatialHash::Insert called with invalid AABB");
    const auto [X0, Y0, X1, Y1] = CellsOf(aabb);
    for (i32 y = Y0; y <= Y1; ++y)
        for (i32 x = X0; x <= X1; ++x)
            _cells[PackKey(x, y)].push_back(id);
}

void SpatialHash::Remove(const ColliderId id, const AABB& aabb)
{
    ARCBIT_ASSERT(aabb.IsValid(), "SpatialHash::Remove called with invalid AABB");
    const auto [X0, Y0, X1, Y1] = CellsOf(aabb);
    for (i32 y = Y0; y <= Y1; ++y) {
        for (i32 x = X0; x <= X1; ++x) {
            const auto it = _cells.find(PackKey(x, y));
            if (it == _cells.end()) continue;
            auto& bucket = it->second;
            // swap-and-pop the first matching id; an id can only appear once per cell
            if (const auto idIt = std::find(bucket.begin(), bucket.end(), id); idIt != bucket.end()) {
                *idIt = bucket.back();
                bucket.pop_back();
            }
            if (bucket.empty()) _cells.erase(it);
        }
    }
}

void SpatialHash::Clear() noexcept { _cells.clear(); }

void SpatialHash::Query(const AABB& queryAABB, std::vector<ColliderId>& out) const
{
    out.clear();
    if (!queryAABB.IsValid()) return;

    const CellRange r = CellsOf(queryAABB);
    for (i32 y = r.Y0; y <= r.Y1; ++y) {
        for (i32 x = r.X0; x <= r.X1; ++x) {
            const auto it = _cells.find(PackKey(x, y));
            if (it == _cells.end()) continue;
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }

    // Dedupe — colliders that span multiple cells will appear once per cell.
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
}

SpatialHash::CellRange SpatialHash::CellsOf(const AABB& aabb) const noexcept
{
    // floor(min*inv) and floor(max*inv) — std::floor handles negatives correctly
    // (truncation toward zero would map -0.5 to 0, which would put cell -1 work
    // into cell 0 and break negative-coordinate queries).
    return {
        static_cast<i32>(std::floor(aabb.Min.X * _invCellSize)),
        static_cast<i32>(std::floor(aabb.Min.Y * _invCellSize)),
        static_cast<i32>(std::floor(aabb.Max.X * _invCellSize)),
        static_cast<i32>(std::floor(aabb.Max.Y * _invCellSize)),
    };
}

} // namespace Arcbit
