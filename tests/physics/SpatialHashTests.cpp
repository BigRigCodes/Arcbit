#include <arcbit/physics/SpatialHash.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace Arcbit;

namespace
{
using ColliderId = SpatialHash::ColliderId;

bool Has(const std::vector<ColliderId>& ids, const ColliderId id)
{
    return std::ranges::find(ids, id) != ids.end();
}
} // namespace

TEST_CASE("SpatialHash insert, query, and remove", "[physics][broadphase]")
{
    SpatialHash h(32.0f);

    constexpr AABB a = AABB::FromCenterHalfExtents({ 16.0f, 16.0f }, { 8.0f, 8.0f });  // cell (0,0)
    constexpr AABB b = AABB::FromCenterHalfExtents({ 80.0f, 16.0f }, { 8.0f, 8.0f });  // cell (2,0)
    constexpr AABB c = AABB::FromCenterHalfExtents({ 32.0f, 32.0f }, { 16.0f, 16.0f }); // cells (0..1, 0..1)
    h.Insert(1u, a);
    h.Insert(2u, b);
    h.Insert(3u, c);

    std::vector<ColliderId> result;

    SECTION("queries return only colliders sharing a cell")
    {
        h.Query(a, result);
        CHECK(Has(result, 1u));
        CHECK(Has(result, 3u));
        CHECK_FALSE(Has(result, 2u));

        h.Query(b, result);
        CHECK(result == std::vector<ColliderId>{ 2u });
    }

    SECTION("a multi-cell query returns each id once")
    {
        h.Query(AABB::FromCenterHalfExtents({ 40.0f, 40.0f }, { 64.0f, 64.0f }), result);
        auto sorted = result;
        std::ranges::sort(sorted);
        CHECK(std::ranges::unique(sorted).begin() == sorted.end());
    }

    SECTION("remove drops only that id")
    {
        h.Remove(1u, a);
        h.Query(a, result);
        CHECK_FALSE(Has(result, 1u));
        CHECK(Has(result, 3u));
    }
}

TEST_CASE("SpatialHash handles negative coordinates and Clear", "[physics][broadphase]")
{
    SpatialHash    h(32.0f);
    constexpr AABB box = AABB::FromCenterHalfExtents({ -16.0f, -16.0f }, { 8.0f, 8.0f });
    h.Insert(42u, box);

    std::vector<ColliderId> result;
    h.Query(box, result);
    CHECK(result == std::vector<ColliderId>{ 42u });

    h.Clear();
    CHECK(h.CellCount() == 0);
}
