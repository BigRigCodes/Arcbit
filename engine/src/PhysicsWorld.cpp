#include <arcbit/physics/PhysicsWorld.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>
#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/OBB.h>
#include <arcbit/physics/Sweep.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/tilemap/TileMap.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <vector>

namespace Arcbit
{
namespace
{
    // Refresh every Collider2D-derived field of a record (everything except
    // Owner / WorldAABB / Active, which the broadphase bookkeeping owns).
    void CopyColliderFields(PhysicsWorld::ColliderRecord& entry, const Collider2D& collider,
                            const Vec2                    worldPosition)
    {
        entry.Position    = worldPosition;
        entry.Shape       = collider.Shape;
        entry.Offset      = collider.Offset;
        entry.HalfExtents = collider.HalfExtents;
        entry.Radius      = collider.Radius;
        entry.Kind        = collider.Kind;
        entry.Layer       = collider.Layer;
        entry.Mask        = collider.Mask;
        entry.Rotation    = collider.Rotation;
        entry.IsTrigger   = collider.IsTrigger;
        entry.BlockedFrom = collider.BlockedFrom;
    }
} // anonymous namespace

PhysicsWorld::PhysicsWorld(const f32 cellSize) : _hash(cellSize) {}

PhysicsWorld::ColliderId PhysicsWorld::RegisterCollider(const Entity      entity,
                                                        const Collider2D& collider,
                                                        const Vec2        worldPosition)
{
    const AABB worldAABB = ComputeWorldAABB(collider, worldPosition);

    ColliderId id;
    if (!_freeList.empty()) {
        id = _freeList.back();
        _freeList.pop_back();
    }
    else {
        id = static_cast<ColliderId>(_colliders.size());
        _colliders.emplace_back();
    }

    ColliderRecord& entry = _colliders[id];
    entry                 = ColliderRecord{ .Owner = entity, .WorldAABB = worldAABB, .Active = true };
    CopyColliderFields(entry, collider, worldPosition);
    _hash.Insert(id, worldAABB);
    _entityToCollider[PackEntity(entity)] = id;
    return id;
}

void PhysicsWorld::UnregisterCollider(const ColliderId id)
{
    ARCBIT_ASSERT(id < _colliders.size() && _colliders[id].Active,
                  "PhysicsWorld::UnregisterCollider called with stale or invalid id");
    if (id >= _colliders.size() || !_colliders[id].Active) return;

    _hash.Remove(id, _colliders[id].WorldAABB);
    _entityToCollider.erase(PackEntity(_colliders[id].Owner));
    _colliders[id].Active = false;
    _colliders[id].Owner  = Entity::Invalid();
    _freeList.push_back(id);
}

void PhysicsWorld::UpdateCollider(const ColliderId id, const Collider2D& collider,
                                  const Vec2       worldPosition)
{
    ARCBIT_ASSERT(id < _colliders.size() && _colliders[id].Active,
                  "PhysicsWorld::UpdateCollider called with stale or invalid id");
    if (id >= _colliders.size() || !_colliders[id].Active) return;

    ColliderRecord& entry   = _colliders[id];
    const AABB      newAABB = ComputeWorldAABB(collider, worldPosition);

    // Refresh the shape/filter cache even when the bounds stay put. The
    // caller may have changed fields that leave the AABB identical (a rotated
    // square turning by 90 degrees, a Layer change, new arcs).
    CopyColliderFields(entry, collider, worldPosition);

    // Cheap early-out — if the AABB is bit-identical the cell membership is too.
    if (entry.WorldAABB.Min.X == newAABB.Min.X && entry.WorldAABB.Min.Y == newAABB.Min.Y &&
        entry.WorldAABB.Max.X == newAABB.Max.X && entry.WorldAABB.Max.Y == newAABB.Max.Y)
        return;

    _hash.Remove(id, entry.WorldAABB);
    entry.WorldAABB = newAABB;
    _hash.Insert(id, newAABB);
}

void PhysicsWorld::QueryTileColliders(const AABB&                    worldAABB,
                                      std::vector<TileColliderRect>& out) const
{
    out.clear();
    if (!_tilemap) return;

    // Map the query AABB into chunk coordinates.
    const f32 tileSize  = _tilemap->GetTileSize();
    const f32 chunkSize = tileSize * static_cast<f32>(TileMap::GetChunkSize());
    const i32 cx0       = static_cast<i32>(std::floor(worldAABB.Min.X / chunkSize));
    const i32 cy0       = static_cast<i32>(std::floor(worldAABB.Min.Y / chunkSize));
    const i32 cx1       = static_cast<i32>(std::floor(worldAABB.Max.X / chunkSize));
    const i32 cy1       = static_cast<i32>(std::floor(worldAABB.Max.Y / chunkSize));

    for (i32 cy = cy0; cy <= cy1; ++cy) {
        for (i32 cx = cx0; cx <= cx1; ++cx) {
            const auto& rects = _tilemap->GetChunkColliders(cx, cy);
            // Cheap per-rect AABB filter — most chunk rects won't overlap a
            // small query (e.g. one entity's swept AABB).
            for (const auto& rect : rects)
                if (rect.WorldAABB.Overlaps(worldAABB))
                    out.push_back(rect);
        }
    }
}

// Synthesize a Box collider from a tile collider rect so it can be fed to
// Sweep::SweepAgainst. Exact — greedy-meshed tile rects are axis-aligned.
namespace
{
    Collider2D BoxFromAABB(const AABB& aabb, const u32 layer) noexcept
    {
        Collider2D c{};
        c.Shape       = ColliderShape::Box;
        c.HalfExtents = aabb.HalfExtents();
        c.Kind        = BodyKind::Static;
        c.Layer       = layer;
        return c;
    }

    // Rebuild an entity collider's exact shape from its cached record. Pair
    // with `rec.Position` as the obstacle's world position.
    Collider2D ObstacleFromRecord(const PhysicsWorld::ColliderRecord& rec) noexcept
    {
        Collider2D c{};
        c.Shape       = rec.Shape;
        c.Offset      = rec.Offset;
        c.Rotation    = rec.Rotation;
        c.HalfExtents = rec.HalfExtents;
        c.Radius      = rec.Radius;
        c.Kind        = rec.Kind;
        c.Layer       = rec.Layer;
        return c;
    }

    inline bool LayersPair(const u32 layerA, const u32 maskA,
                           const u32 layerB, const u32 maskB) noexcept
    {
        return ((layerA & maskB) != 0) && ((layerB & maskA) != 0);
    }
} // anonymous namespace

bool PhysicsWorld::QueryTileBlocked(const Entity      mover, const Collider2D& col,
                                    const Vec2        originWorld, const Vec2  targetWorld) const noexcept
{
    const Vec2 delta = {targetWorld.X - originWorld.X, targetWorld.Y - originWorld.Y};
    if (delta.X == 0.0f && delta.Y == 0.0f) return false; // zero-length move can't hit anything new

    // Conservative swept AABB enclosing the path. Same union trick the resolver uses.
    const AABB curr   = ComputeWorldAABB(col, originWorld);
    const AABB target = ComputeWorldAABB(col, targetWorld);
    const AABB swept  = AABB::FromMinMax(
        {std::min(curr.Min.X, target.Min.X), std::min(curr.Min.Y, target.Min.Y)},
        {std::max(curr.Max.X, target.Max.X), std::max(curr.Max.Y, target.Max.Y)});

    const ColliderId selfId = FindColliderForEntity(mover);

    // Entity broadphase pass.
    std::vector<ColliderId> entityHits;
    QueryAABB(swept, entityHits);
    for (const ColliderId otherId : entityHits) {
        if (otherId == selfId) continue;
        const ColliderRecord& rec = _colliders[otherId];
        if (!rec.Active || rec.IsTrigger) continue;
        if (!LayersPair(col.Layer, col.Mask, rec.Layer, rec.Mask)) continue;

        const Collider2D obstacle    = ObstacleFromRecord(rec);
        const Vec2       obstaclePos = rec.Position;
        // Skip blockers the mover already overlaps at origin. Tile movement
        // asks "does the destination cell block my approach?" — the source
        // cell isn't part of that question. Without this skip, the slab
        // method's started-overlapping convention (normal opposite delta,
        // ToI=0) makes a stationary contact with the source tile look like
        // a fresh approach, tripping the source's arc and trapping the
        // mover inside any arc'd tile they legitimately stepped onto.
        // Free-movement's resolver keeps overlap as a real contact for
        // push-back; this skip is scoped to the tile-step path only.
        if (Narrowphase::Overlap(col, originWorld, obstacle, obstaclePos)) continue;
        const Sweep::Result r = Sweep::SweepAgainst(col, originWorld, delta, obstacle, obstaclePos);
        if (!r.Hit) continue;
        if (!IsContactBlocked(rec.BlockedFrom, r.Normal, rec.Rotation)) continue;
        return true;
    }

    // Tile collider pass — tiles always block back, so we only check self.Mask.
    std::vector<TileColliderRect> tileHits;
    QueryTileColliders(swept, tileHits);
    for (const TileColliderRect& rect : tileHits) {
        if ((rect.Layer & col.Mask) == 0) continue;

        const Collider2D obstacle    = BoxFromAABB(rect.WorldAABB, rect.Layer);
        const Vec2       obstaclePos = rect.WorldAABB.Center();
        // Same source-tile skip as the entity pass above — see comment there.
        if (Narrowphase::Overlap(col, originWorld, obstacle, obstaclePos)) continue;
        const Sweep::Result r = Sweep::SweepAgainst(col, originWorld, delta, obstacle, obstaclePos);
        if (!r.Hit) continue;
        // Tile rects don't carry rotation (greedy mesh is axis-aligned), so
        // pass 0 — local frame == world frame.
        if (!IsContactBlocked(rect.BlockedFrom, r.Normal, 0.0f)) continue;
        return true;
    }

    return false;
}

usize PhysicsWorld::ColliderCount() const noexcept { return _colliders.size() - _freeList.size(); }

const PhysicsWorld::ColliderRecord& PhysicsWorld::GetRecord(const ColliderId id) const noexcept
{
    ARCBIT_ASSERT(id < _colliders.size(), "PhysicsWorld::GetRecord: id out of range");
    return _colliders[id];
}

PhysicsWorld::ColliderId PhysicsWorld::FindColliderForEntity(const Entity entity) const noexcept
{
    const auto it = _entityToCollider.find(PackEntity(entity));
    return it == _entityToCollider.end() ? InvalidId : it->second;
}

// ---------------------------------------------------------------------------
// Debug draw — outlines via four thin world-space sprite quads per rect
// ---------------------------------------------------------------------------

namespace
{
    // Layer high enough to sort above tilemap (overlay = 1000000) and entities.
    constexpr i32 PhysicsDebugLayer = 2'000'000;

    // Outline thickness in *world* pixels. Camera zoom scales it visually; if you
    // zoom way in the lines look thicker. Acceptable for a debug tool.
    constexpr f32 PhysicsDebugThickness = 1.5f;

    void EmitRectOutline(FramePacket&        packet, const AABB&           aabb, const Color tint,
                         const TextureHandle whiteTex, const SamplerHandle whiteSampler)
    {
        const f32  t   = PhysicsDebugThickness;
        const Vec2 min = aabb.Min;
        const Vec2 max = aabb.Max;
        const f32  w   = max.X - min.X;
        const f32  h   = max.Y - min.Y;

        auto push = [&](const Vec2 center, const Vec2 size) {
            Sprite s{};
            s.Texture  = whiteTex;
            s.Sampler  = whiteSampler;
            s.Position = center;
            s.Size     = size;
            s.Tint     = tint;
            s.Layer    = PhysicsDebugLayer;
            packet.Sprites.push_back(s);
        };

        // Top edge
        push({min.X + w * 0.5f, min.Y + t * 0.5f}, {w, t});
        // Bottom edge
        push({min.X + w * 0.5f, max.Y - t * 0.5f}, {w, t});
        // Left edge — full height (overlaps top/bottom corners; harmless)
        push({min.X + t * 0.5f, min.Y + h * 0.5f}, {t, h});
        // Right edge
        push({max.X - t * 0.5f, min.Y + h * 0.5f}, {t, h});
    }

    // Same four-strip outline for a rotated box: each strip is placed along
    // the box's local axes and carries the box rotation (Sprite.Rotation uses
    // the same convention as Collider2D.Rotation).
    void EmitObbOutline(FramePacket&        packet, const OBB&            box, const f32 rotation,
                        const Color         tint, const TextureHandle whiteTex,
                        const SamplerHandle whiteSampler)
    {
        const f32  t  = PhysicsDebugThickness;
        const Vec2 he = box.HalfExtents;

        auto push = [&](const Vec2 localCenter, const Vec2 size) {
            Sprite s{};
            s.Texture  = whiteTex;
            s.Sampler  = whiteSampler;
            s.Position = box.Center + box.ToWorldDir(localCenter);
            s.Size     = size;
            s.Rotation = rotation;
            s.Tint     = tint;
            s.Layer    = PhysicsDebugLayer;
            packet.Sprites.push_back(s);
        };

        push({0.0f, -he.Y + t * 0.5f}, {he.X * 2.0f, t}); // local top
        push({0.0f, he.Y - t * 0.5f}, {he.X * 2.0f, t});  // local bottom
        push({-he.X + t * 0.5f, 0.0f}, {t, he.Y * 2.0f}); // local left
        push({he.X - t * 0.5f, 0.0f}, {t, he.Y * 2.0f});  // local right
    }
} // anonymous namespace

void PhysicsWorld::CollectDebugDraw(FramePacket&        packet,
                                    const TextureHandle whiteTex,
                                    const SamplerHandle whiteSampler) const
{
    // ReSharper disable once CppDFAConstantConditions
    if (!_debugDraw) return;

    // Entity colliders — color by kind / trigger flag.
    // TODO(Phase 34): switch circle colliders from a bounding AABB outline to a
    // proper N-segment circle drawn via DrawLine — bounding AABBs are
    // misleading when debugging circle-vs-something interactions because the
    // visible outline doesn't match the actual shape used for narrowphase
    // (especially for grazing contacts at corners).
    for (const auto& entry : _colliders) {
        if (!entry.Active) continue;
        const Color tint = entry.IsTrigger
                           ? Color::Yellow()
                           : (entry.Kind == BodyKind::Static)
                             ? Color::Red()
                             : Color::Green();
        if (entry.Shape == ColliderShape::Box && entry.Rotation != 0.0f) {
            const OBB box = OBB::FromCollider(ObstacleFromRecord(entry), entry.Position);
            EmitObbOutline(packet, box, entry.Rotation, tint, whiteTex, whiteSampler);
            continue;
        }
        EmitRectOutline(packet, entry.WorldAABB, tint, whiteTex, whiteSampler);
    }

    // Tile colliders — every cached chunk's greedy-meshed rects, in red.
    if (_tilemap) {
        for (const auto& key : _tilemap->GetChunks() | std::views::keys) {
            const i32 cx = static_cast<i32>(static_cast<u32>(key >> 32));
            const i32 cy = static_cast<i32>(static_cast<u32>(key));
            for (const auto& rects = _tilemap->GetChunkColliders(cx, cy); const auto& rect : rects)
                EmitRectOutline(packet, rect.WorldAABB, Color::Red(), whiteTex, whiteSampler);
        }
    }
}

AABB PhysicsWorld::ComputeWorldAABB(const Collider2D& collider, const Vec2 worldPosition)
{
    const Vec2 center = {
        worldPosition.X + collider.Offset.X,
        worldPosition.Y + collider.Offset.Y
    };

    if (collider.Shape == ColliderShape::Circle) {
        return AABB::FromCenterHalfExtents(center, {collider.Radius, collider.Radius});
    }

    // Box — fast path when not rotated.
    if (collider.Rotation == 0.0f) { return AABB::FromCenterHalfExtents(center, collider.HalfExtents); }

    // Rotated box — bounding AABB of the OBB.
    // For an OBB with half-extents (hx, hy) rotated by θ:
    //   aabbHalf.x = |cos θ|*hx + |sin θ|*hy
    //   aabbHalf.y = |sin θ|*hx + |cos θ|*hy
    const f32  absCos    = std::abs(std::cos(collider.Rotation));
    const f32  absSin    = std::abs(std::sin(collider.Rotation));
    const Vec2 boundHalf = {
        absCos * collider.HalfExtents.X + absSin * collider.HalfExtents.Y,
        absSin * collider.HalfExtents.X + absCos * collider.HalfExtents.Y,
    };
    return AABB::FromCenterHalfExtents(center, boundHalf);
}

// ---------------------------------------------------------------------------
// SelfTest — register / query / move / unregister roundtrip, debug builds only.
// ---------------------------------------------------------------------------

void PhysicsWorld::SelfTest()
{
    #ifdef ARCBIT_DEBUG
    PhysicsWorld world(32.0f);

    // Two static box colliders sitting in different cells.
    const Collider2D rockShape{
        .Shape       = ColliderShape::Box,
        .HalfExtents = {6.0f, 6.0f},
        .Kind        = BodyKind::Static,
    };
    const Entity rockEntity{.Index = 1, .Generation = 0};
    const Entity treeEntity{.Index = 2, .Generation = 0};

    const ColliderId rock = world.RegisterCollider(rockEntity, rockShape, {16.0f, 16.0f});
    const ColliderId tree = world.RegisterCollider(treeEntity, rockShape, {200.0f, 16.0f});

    ARCBIT_ASSERT(world.ColliderCount() == 2,
                  "PhysicsWorld::SelfTest: expected two registered colliders");

    std::vector<ColliderId> result;

    // Query a region around the rock — should find rock, not tree.
    world.QueryAABB(AABB::FromCenterHalfExtents({16.0f, 16.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.size() == 1 && result[0] == rock,
                  "PhysicsWorld::SelfTest: rock query returned wrong set");

    // Move the rock far away. The old query region should now be empty; a query
    // at the new position should find it.
    world.UpdateCollider(rock, rockShape, {1000.0f, 1000.0f});
    world.QueryAABB(AABB::FromCenterHalfExtents({16.0f, 16.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.empty(),
                  "PhysicsWorld::SelfTest: stale AABB cache — rock still in old cell");
    world.QueryAABB(AABB::FromCenterHalfExtents({1000.0f, 1000.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.size() == 1 && result[0] == rock,
                  "PhysicsWorld::SelfTest: rock missing from new cell after Update");

    // Unregister the rock — its slot should free, count drops to 1.
    world.UnregisterCollider(rock);
    ARCBIT_ASSERT(world.ColliderCount() == 1,
                  "PhysicsWorld::SelfTest: count did not drop after Unregister");
    world.QueryAABB(AABB::FromCenterHalfExtents({1000.0f, 1000.0f}, {8.0f, 8.0f}), result);
    ARCBIT_ASSERT(result.empty(),
                  "PhysicsWorld::SelfTest: rock still in hash after Unregister");

    // Next register should reuse the freed slot.
    const Entity     boxEntity{.Index = 3, .Generation = 0};
    const ColliderId reused = world.RegisterCollider(boxEntity, rockShape, {50.0f, 50.0f});
    ARCBIT_ASSERT(reused == rock,
                  "PhysicsWorld::SelfTest: free-list did not reuse the vacant slot");

    // Circle and rotated-box AABB sanity — sizes match expectation.
    const Collider2D circleShape{
        .Shape  = ColliderShape::Circle,
        .Radius = 10.0f,
    };
    const AABB circleAABB = ComputeWorldAABB(circleShape, {0.0f, 0.0f});
    ARCBIT_ASSERT(circleAABB.Min.X == -10.0f && circleAABB.Max.X == 10.0f,
                  "PhysicsWorld::SelfTest: circle AABB extents wrong");

    const Collider2D rotatedShape{
        .Shape       = ColliderShape::Box,
        .Rotation    = 1.5707963267948966f, // 90°
        .HalfExtents = {4.0f, 16.0f},
    };
    const AABB rotatedAABB = ComputeWorldAABB(rotatedShape, {0.0f, 0.0f});
    // 90° rotation swaps the half-extents.
    constexpr f32 tol = 0.001f;
    ARCBIT_ASSERT(std::abs(rotatedAABB.HalfExtents().X - 16.0f) < tol &&
                  std::abs(rotatedAABB.HalfExtents().Y - 4.0f) < tol,
                  "PhysicsWorld::SelfTest: rotated-box bounding AABB wrong");

    // ---- Tile collider greedy mesh -----------------------------------------
    // Standalone TileMap configured with a single solid TileDef. We don't load
    // any atlas — IsSolid only needs FindTileDef + GetTile to work, so writing
    // tile IDs directly with SetTile and registering a TileDef is enough.
    {
        TileMap tilemap;
        tilemap.SetTileSize(32.0f);

        // Tile id 1 is solid. Default Layer = Wall, BlockedFrom empty.
        TileDef wallDef{};
        wallDef.Solid = true;
        tilemap.RegisterTile(1, wallDef);

        PhysicsWorld pw(32.0f);
        pw.SetTileMap(&tilemap);

        std::vector<TileColliderRect> rects;

        // Empty tilemap → no rects.
        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.empty(),
                      "PhysicsWorld::SelfTest: empty tilemap should produce no rects");

        // Pattern A: 3-tile horizontal line at (0,0)-(2,0). Greedy must merge
        // into one 3x1 rect.
        constexpr u32 ObjectsLayer = 1;
        tilemap.SetTile(0, 0, ObjectsLayer, 1);
        tilemap.SetTile(1, 0, ObjectsLayer, 1);
        tilemap.SetTile(2, 0, ObjectsLayer, 1);

        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 1,
                      "PhysicsWorld::SelfTest: 3-tile row should greedy-merge to 1 rect");
        ARCBIT_ASSERT(std::abs(rects[0].WorldAABB.Size().X - 96.0f) < tol &&
                      std::abs(rects[0].WorldAABB.Size().Y - 32.0f) < tol,
                      "PhysicsWorld::SelfTest: 3x1 rect dimensions wrong");

        // Pattern B: Add a 2x2 block at (5,0)-(6,1). Now the chunk holds
        // a 3x1 rect plus a 2x2 rect — non-adjacent → no merging.
        tilemap.SetTile(5, 0, ObjectsLayer, 1);
        tilemap.SetTile(6, 0, ObjectsLayer, 1);
        tilemap.SetTile(5, 1, ObjectsLayer, 1);
        tilemap.SetTile(6, 1, ObjectsLayer, 1);

        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 2,
                      "PhysicsWorld::SelfTest: non-adjacent shapes should produce 2 rects");

        // Coverage check: total area must equal 3 + 4 = 7 tiles worth.
        constexpr f32 tileArea  = 32.0f * 32.0f;
        f32           totalArea = 0.0f;
        for (const auto& r : rects)
            totalArea += r.WorldAABB.Size().X * r.WorldAABB.Size().Y;
        ARCBIT_ASSERT(std::abs(totalArea - 7.0f * tileArea) < tol,
                      "PhysicsWorld::SelfTest: greedy mesh coverage incorrect");

        // Pattern C: SetTile invalidates the chunk cache. Erasing the 3-tile
        // row should leave just the 2x2 block.
        tilemap.SetTile(0, 0, ObjectsLayer, 0);
        tilemap.SetTile(1, 0, ObjectsLayer, 0);
        tilemap.SetTile(2, 0, ObjectsLayer, 0);
        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 1 &&
                      std::abs(rects[0].WorldAABB.Size().X - 64.0f) < tol &&
                      std::abs(rects[0].WorldAABB.Size().Y - 64.0f) < tol,
                      "PhysicsWorld::SelfTest: cache not invalidated by SetTile");

        // Pattern D: Two solid TileDefs with *different* Layer values must NOT
        // merge even when adjacent — they belong to different collision classes.
        TileDef wallDef2{};
        wallDef2.Solid = true;
        wallDef2.Layer = CollisionLayers::Prop; // distinct from wallDef's Wall
        tilemap.RegisterTile(2, wallDef2);

        // Clear the 2x2 block first to start with a clean slate at (10, 0).
        tilemap.SetTile(5, 0, ObjectsLayer, 0);
        tilemap.SetTile(6, 0, ObjectsLayer, 0);
        tilemap.SetTile(5, 1, ObjectsLayer, 0);
        tilemap.SetTile(6, 1, ObjectsLayer, 0);

        tilemap.SetTile(10, 0, ObjectsLayer, 1); // Wall
        tilemap.SetTile(11, 0, ObjectsLayer, 2); // Prop

        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 2,
                      "PhysicsWorld::SelfTest: tiles with differing Layer must not merge");

        // Pattern E: chunks limit the mesh — solid tiles spanning a chunk
        // boundary form one rect per chunk. ChunkSize == 16 with tileSize 32
        // means tile X coordinates [0..15] are chunk 0; [16..31] are chunk 1.
        // Clear and lay a 2-tile horizontal strip across the boundary at (15,5)-(16,5).
        tilemap.SetTile(10, 0, ObjectsLayer, 0);
        tilemap.SetTile(11, 0, ObjectsLayer, 0);
        tilemap.SetTile(15, 5, ObjectsLayer, 1);
        tilemap.SetTile(16, 5, ObjectsLayer, 1);
        pw.QueryTileColliders(AABB::FromMinMax({-1000.0f, -1000.0f}, {1000.0f, 1000.0f}), rects);
        ARCBIT_ASSERT(rects.size() == 2,
                      "PhysicsWorld::SelfTest: chunk boundary should split greedy mesh");

        LOG_DEBUG(Engine, "PhysicsWorld::SelfTest: greedy tile mesh passed");
    }

    // ---- QueryTileBlocked ----------------------------------------------------
    // Plan-then-commit yes/no: clear path, blocked by static box, blocked by
    // tile rect, ignored trigger, self-filter works.
    {
        TileMap tilemap;
        tilemap.SetTileSize(32.0f);
        TileDef wallDef{};
        wallDef.Solid = true;
        tilemap.RegisterTile(1, wallDef);

        PhysicsWorld pw(32.0f);
        pw.SetTileMap(&tilemap);

        const Collider2D moverCol{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {4.0f, 4.0f},
            .Kind        = BodyKind::Kinematic,
            .Layer       = CollisionLayers::Player,
        };
        const Entity moverEntity{.Index = 10, .Generation = 0};
        pw.RegisterCollider(moverEntity, moverCol, {0.0f, 0.0f});

        // Static box obstacle on the +X axis.
        const Collider2D obstacleCol{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {6.0f, 6.0f},
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Prop,
        };
        const Entity obstacleEntity{.Index = 11, .Generation = 0};
        pw.RegisterCollider(obstacleEntity, obstacleCol, {50.0f, 0.0f});

        // Trigger at (0, 50).
        const Collider2D triggerCol{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {6.0f, 6.0f},
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Trigger,
            .IsTrigger   = true,
        };
        const Entity triggerEntity{.Index = 12, .Generation = 0};
        pw.RegisterCollider(triggerEntity, triggerCol, {0.0f, 50.0f});

        // Solid tile at column 6 (world center {192, 0}).
        constexpr u32 ObjectsLayer = 1;
        tilemap.SetTile(6, 0, ObjectsLayer, 1);

        // 1. Clear path — short hop in -Y direction, nothing in the way.
        ARCBIT_ASSERT(!pw.QueryTileBlocked(moverEntity, moverCol, {0.0f, 0.0f}, {0.0f, -32.0f}),
                      "QueryTileBlocked: clear path falsely blocked");

        // 2. Path crosses static box obstacle at (50, 0).
        ARCBIT_ASSERT(pw.QueryTileBlocked(moverEntity, moverCol, {0.0f, 0.0f}, {60.0f, 0.0f}),
                      "QueryTileBlocked: missed static box on path");

        // 3. Path crosses tile rect at column 6.
        ARCBIT_ASSERT(pw.QueryTileBlocked(moverEntity, moverCol, {160.0f, 0.0f}, {220.0f, 0.0f}),
                      "QueryTileBlocked: missed tile collider on path");

        // 4. Path crosses trigger only — must not block.
        ARCBIT_ASSERT(!pw.QueryTileBlocked(moverEntity, moverCol, {0.0f, 0.0f}, {0.0f, 60.0f}),
                      "QueryTileBlocked: trigger should not block");

        // 5. Self-filter — moving from current position back through self
        // shouldn't register as a hit. Use a very short delta around origin so
        // nothing else is in the swept AABB.
        ARCBIT_ASSERT(!pw.QueryTileBlocked(moverEntity, moverCol, {0.0f, 0.0f}, {1.0f, 0.0f}),
                      "QueryTileBlocked: self-filter let mover collide with itself");

        // 6. Vertical-arc entity collider — blocks N/S approach, lets E/W pass.
        // Build a fresh tree entity with BlockedFrom = Vertical(); the existing
        // obstacle uses default empty arcs and would interfere with the E/W
        // pass-through test, so put the tree well away from it.
        const Collider2D tree{
            .Shape       = ColliderShape::Box,
            .HalfExtents = {6.0f, 6.0f},
            .Kind        = BodyKind::Static,
            .Layer       = CollisionLayers::Prop,
            .BlockedFrom = DirectionArc::Vertical(),
        };
        const Entity treeEntity{.Index = 13, .Generation = 0};
        pw.RegisterCollider(treeEntity, tree, {-200.0f, 0.0f});
        // Approach from the west (mover east of tree, moving west into it) —
        // contact normal points east (+X), which is in the *Horizontal* direction.
        // Vertical arcs cover N/S only, so this contact passes through.
        ARCBIT_ASSERT(!pw.QueryTileBlocked(moverEntity, moverCol, {-180.0f, 0.0f}, {-200.0f, 0.0f}),
                      "QueryTileBlocked: vertical-arc tree must let E/W approach pass");
        // Approach from the south (mover north of tree, moving south into it) —
        // contact normal points north, which falls inside the Vertical arc.
        ARCBIT_ASSERT(pw.QueryTileBlocked(moverEntity, moverCol, {-200.0f, -20.0f}, {-200.0f, 0.0f}),
                      "QueryTileBlocked: vertical-arc tree must block N/S approach");

        // 7. Vertical-arc tile rect — same idea, but on a tile collider.
        TileDef ledgeDef{};
        ledgeDef.Solid       = true;
        ledgeDef.BlockedFrom = DirectionArc::NorthOnly();
        tilemap.RegisterTile(2, ledgeDef);
        // Place the ledge tile at column -6 (world center {-192, 0}), well
        // separated from the tree at -200 to keep tests independent.
        tilemap.SetTile(-6, 5, ObjectsLayer, 2);
        // Approach from the south (moving north into the ledge): contact normal
        // points south, outside NorthOnly arc → passes.
        ARCBIT_ASSERT(!pw.QueryTileBlocked(moverEntity, moverCol, {-192.0f, 180.0f}, {-192.0f, 160.0f}),
                      "QueryTileBlocked: NorthOnly ledge must let S→N approach pass");
        // Approach from the north (moving south onto the ledge): contact normal
        // points north → inside NorthOnly arc → blocks.
        ARCBIT_ASSERT(pw.QueryTileBlocked(moverEntity, moverCol, {-192.0f, 130.0f}, {-192.0f, 150.0f}),
                      "QueryTileBlocked: NorthOnly ledge must block N→S approach");

        LOG_DEBUG(Engine, "PhysicsWorld::SelfTest: QueryTileBlocked passed");
    }

    // ---- QueryTileBlocked vs rotated obstacle (22E) --------------------------
    // The query must test the obstacle's real shape: a step through the empty
    // corner of a 45° plank's bounding AABB passes, a step across the plank
    // blocks. Pre-22E the query used the bounding AABB and blocked both.
    {
        PhysicsWorld pw(32.0f);

        const Collider2D moverCol{ .Shape = ColliderShape::Box, .HalfExtents = { 4.0f, 4.0f } };
        const Entity     moverEntity{ .Index = 20, .Generation = 0 };
        pw.RegisterCollider(moverEntity, moverCol, { 110.0f, -26.0f });

        const Collider2D plank{
            .Shape       = ColliderShape::Box,
            .Rotation    = QuarterPi,
            .HalfExtents = { 40.0f, 2.0f },
            .Kind        = BodyKind::Static,
        };
        const Entity plankEntity{ .Index = 21, .Generation = 0 };
        pw.RegisterCollider(plankEntity, plank, { 100.0f, 0.0f });

        ARCBIT_ASSERT(pw.GetRecord(pw.FindColliderForEntity(plankEntity)).WorldAABB.Contains({ 120.0f, -26.0f }),
                      "QueryTileBlocked: test setup — path should cross the plank's bounding AABB");
        ARCBIT_ASSERT(!pw.QueryTileBlocked(moverEntity, moverCol, { 110.0f, -26.0f }, { 130.0f, -26.0f }),
                      "QueryTileBlocked: rotated plank blocked through its empty bounding-AABB corner");
        ARCBIT_ASSERT(pw.QueryTileBlocked(moverEntity, moverCol, { 100.0f, -20.0f }, { 100.0f, 20.0f }),
                      "QueryTileBlocked: step across a rotated plank not blocked");

        LOG_DEBUG(Engine, "PhysicsWorld::SelfTest: rotated QueryTileBlocked passed");
    }

    LOG_DEBUG(Engine, "PhysicsWorld::SelfTest passed");
    #endif
}
} // namespace Arcbit
