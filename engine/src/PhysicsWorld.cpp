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
                                    const Vec2        originWorld, const Vec2  targetWorld,
                                    BlockingHit*      hit) const noexcept
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
        if (hit) *hit = { rec.Owner, r.Normal };
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
        if (hit) *hit = { Entity::Invalid(), r.Normal };
        return true;
    }

    return false;
}

void PhysicsWorld::CollectTriggerOverlaps(std::vector<EntityPair>& out) const
{
    out.clear();
    std::vector<ColliderId> hits;
    for (const ColliderRecord& trigger : _colliders) {
        if (!trigger.Active || !trigger.IsTrigger) continue;

        const Collider2D triggerShape = ObstacleFromRecord(trigger);
        QueryAABB(trigger.WorldAABB, hits);
        for (const ColliderId id : hits) {
            const ColliderRecord& other = _colliders[id];
            if (!other.Active || other.IsTrigger) continue; // also skips the trigger itself
            if (!LayersPair(trigger.Layer, trigger.Mask, other.Layer, other.Mask)) continue;
            if (!Narrowphase::Overlap(triggerShape, trigger.Position, ObstacleFromRecord(other), other.Position))
                continue;
            out.push_back({ trigger.Owner, other.Owner });
        }
    }
}

bool PhysicsWorld::IsTouching(const Entity a, const Entity b, const f32 skin) const noexcept
{
    const ColliderId idA = FindColliderForEntity(a);
    const ColliderId idB = FindColliderForEntity(b);
    if (idA == InvalidId || idB == InvalidId) return false;

    const ColliderRecord& recA  = _colliders[idA];
    const ColliderRecord& recB  = _colliders[idB];
    Collider2D            grown = ObstacleFromRecord(recA);
    grown.HalfExtents           = { grown.HalfExtents.X + skin, grown.HalfExtents.Y + skin };
    grown.Radius               += skin;
    return Narrowphase::Overlap(grown, recA.Position, ObstacleFromRecord(recB), recB.Position);
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

} // namespace Arcbit
