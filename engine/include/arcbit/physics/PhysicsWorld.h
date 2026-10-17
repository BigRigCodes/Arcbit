#pragma once

#include <functional>
#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/ecs/World.h>
#include <arcbit/physics/AABB.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/physics/ContactTracker.h>
#include <arcbit/physics/DirectionArc.h>
#include <arcbit/physics/SpatialHash.h>
#include <arcbit/physics/TileColliderRect.h>
#include <arcbit/render/RenderHandle.h>

#include <unordered_map>
#include <vector>

namespace Arcbit
{
struct FramePacket;
}

namespace Arcbit
{
class TileMap; // forward decl — narrowphase against tile colliders lands later

// PhysicsWorld owns the broadphase (SpatialHash) plus the cache of per-collider
// world AABBs that callers would otherwise have to track themselves to satisfy
// SpatialHash::Remove. Each registered collider gets a stable ColliderId until
// it is unregistered.
//
// Per-frame movement systems should:
//   1. Register colliders at entity creation (or on first frame).
//   2. Call UpdateCollider whenever the entity's world position changes —
//      though the built-in CollisionResolutionSystem already does this for any
//      entity with (Transform2D, PendingMove, Collider2D).
//   3. Unregister at entity destruction.
//
// The collision resolution pass itself lives in `CollisionResolutionSystem`
// (Systems.cpp), not on PhysicsWorld — the world is a pure broadphase/cache
// service that the ECS system queries each tick.
class PhysicsWorld
{
public:
    using ColliderId                      = SpatialHash::ColliderId;
    static constexpr ColliderId InvalidId = ~0u;

    // Per-collider state cached by the broadphase. Owner is the ECS entity that
    // registered the collider; WorldAABB is the cached bounding AABB used to
    // remove the collider from the spatial hash on Update/Unregister; the
    // remaining fields are copies of Collider2D fields (plus the world
    // Position it was registered at) so ECS-free queries such as
    // QueryTileBlocked can rebuild the exact shape and filter without a
    // component lookup. Active==false means the slot is vacant (free-list
    // candidate).
    //
    // Cache invariant: any mutation to a Collider2D's cached fields requires
    // a UpdateCollider call to refresh this record. Position / Rotation /
    // HalfExtents / Radius changes already require it (they affect WorldAABB);
    // Shape / Offset / Layer / Mask / IsTrigger / BlockedFrom changes are
    // equally subject.
    struct ColliderRecord
    {
        Entity                    Owner       = Entity::Invalid();
        AABB                      WorldAABB   = {};
        Vec2                      Position    = {};
        ColliderShape             Shape       = ColliderShape::Box;
        Vec2                      Offset      = {};
        Vec2                      HalfExtents = {};
        f32                       Radius      = 0.0f;
        BodyKind                  Kind        = BodyKind::Kinematic;
        u32                       Layer       = 0;
        u32                       Mask        = 0;
        f32                       Rotation    = 0.0f;
        bool                      IsTrigger   = false;
        bool                      Active      = false;
        std::vector<DirectionArc> BlockedFrom = {};
    };

    explicit PhysicsWorld(f32 cellSize);

    // Insert a collider associated with `entity`, located at `worldPosition`.
    // The collider data is copied; subsequent edits to the source struct do not
    // affect the broadphase until UpdateCollider is called.
    ColliderId RegisterCollider(Entity entity, const Collider2D& collider,
                                Vec2   worldPosition);

    // Drop a previously-registered collider. The id becomes invalid; callers
    // should not reuse it. Subsequent calls on a stale id are a no-op (debug
    // builds assert).
    void UnregisterCollider(ColliderId id);

    // Recompute the world AABB for `id` based on the supplied collider data
    // and world position, swap it into the broadphase, and update the cache.
    // Cheaper than Unregister + Register because the slot stays put.
    void UpdateCollider(ColliderId id, const Collider2D& collider, Vec2 worldPosition);

    // Pure broadphase query. Caller must run a narrowphase test on each result.
    void QueryAABB(const AABB& worldAABB, std::vector<ColliderId>& out) const { _hash.Query(worldAABB, out); }

    // Append all greedy-meshed tile collider rects whose chunks overlap the
    // query AABB. Cleared first. No-op (just clears `out`) if no TileMap is
    // attached. Caller still narrowphases each rect — chunk overlap doesn't
    // mean the individual rect overlaps the query.
    void QueryTileColliders(const AABB& worldAABB, std::vector<TileColliderRect>& out) const;

    // Plan-then-commit query for tile movement: would moving `mover` from
    // `originWorld` to `targetWorld` collide with anything blocking?
    //
    // Same dispatcher as the FreeMovement resolver — broadphase entity hits +
    // overlapping tile rects + `Sweep::SweepAgainst` — but yes/no instead of
    // producing slide motion, so it returns at the first blocking contact.
    //
    // Caller passes the mover's `Collider2D` (PhysicsWorld is intentionally
    // ECS-free). The mover's broadphase entry is found via
    // FindColliderForEntity and self-filtered out of the results.
    //
    // Other entities are tested with their exact cached shape (box, rotated
    // box, or circle — see ColliderRecord). Layer/Mask pair check + IsTrigger
    // filter applied before the swept test runs.
    //
    // When `hit` is non-null and the move is blocked, it receives the first
    // blocker found (Other is Entity::Invalid() for a tile collider) and the
    // contact normal (pointing toward the mover).
    struct BlockingHit
    {
        Entity Other  = Entity::Invalid();
        Vec2   Normal = {};
    };
    [[nodiscard]] bool QueryTileBlocked(Entity mover, const Collider2D& col,
                                        Vec2   originWorld, Vec2 targetWorld,
                                        BlockingHit* hit = nullptr) const noexcept;

    // True when the cached shapes of `a` and `b` overlap with `a` grown by
    // `skin` on every side — i.e. they touch or sit within `skin` pixels.
    // Keeps a blocking contact alive after the mover stops sweeping into it.
    // False if either entity has no registered collider.
    [[nodiscard]] bool IsTouching(Entity a, Entity b, f32 skin) const noexcept;

    // Blocking hits reported by movers this tick (resolver + tile movement),
    // consumed by the ContactEvents system. Normal points from Other toward
    // Mover. Entity-vs-entity only — tile hits are not reported.
    struct ContactReport
    {
        Entity Mover  = Entity::Invalid();
        Entity Other  = Entity::Invalid();
        Vec2   Normal = {};
    };
    void ReportContact(const Entity mover, const Entity other, const Vec2 normal)
    {
        _contactReports.push_back({ mover, other, normal });
    }
    [[nodiscard]] std::vector<ContactReport>& ContactReports() noexcept { return _contactReports; }

    // Trigger overlap pass (22F). Clears `out`, then appends (trigger, other)
    // for every non-trigger collider whose exact shape overlaps an active
    // trigger and passes the Layer/Mask pair check. Trigger-vs-trigger pairs
    // and tile colliders are never reported. Reads only the record cache, so
    // positions are as of each collider's last Register/UpdateCollider.
    void CollectTriggerOverlaps(std::vector<EntityPair>& out) const;

    // Per-scene contact state, driven by the built-in ContactEvents system
    // (Systems.cpp): trigger overlaps, and blocking contacts between entities.
    [[nodiscard]] ContactTracker& TriggerContacts() noexcept { return _triggerContacts; }

    // (mover, collider) pairs where the mover's own move carried it inside
    // the collider — through a pass-through face of a directional collider.
    // The push-out system leaves these overlaps alone and drops each pair once
    // it stops overlapping; the resolver adds new ones (22I).
    [[nodiscard]] ContactTracker::PairSet& EnteredOverlaps() noexcept { return _enteredOverlaps; }
    [[nodiscard]] ContactTracker& BlockingContacts() noexcept { return _blockingContacts; }

    // Every contact event the ContactEvents system dispatches, once for each
    // entity of the pair (self, other) - alongside the entities' own
    // TriggerCallback / CollisionCallback, which still run. Scripts listen
    // here (SceneScripts); normal is set for CollisionEnter only.
    enum class ContactEvent : u8
    {
        TriggerEnter,
        TriggerStay,
        TriggerExit,
        CollisionEnter,
        CollisionExit,
    };
    using ContactListener = std::function<void(ContactEvent event, Entity self, Entity other, Vec2 normal)>;
    void                                 SetContactListener(ContactListener listener) { _contactListener = std::move(listener); }
    [[nodiscard]] const ContactListener& GetContactListener() const noexcept { return _contactListener; }

    // Tilemap pointer for the (future) tile-synthesized collider path. Non-owning;
    // the Scene owns the tilemap and outlives the PhysicsWorld.
    void                   SetTileMap(TileMap* tilemap) noexcept { _tilemap = tilemap; }
    [[nodiscard]] TileMap* GetTileMap() const noexcept { return _tilemap; }

    [[nodiscard]] const SpatialHash& Hash() const noexcept { return _hash; }
    [[nodiscard]] usize              ColliderCount() const noexcept;

    // O(1) lookup from broadphase ColliderId to its cached record. The
    // returned reference is invalidated by Register / Unregister / Update.
    // Asserts in debug builds when `id` is out of range.
    [[nodiscard]] const ColliderRecord& GetRecord(ColliderId id) const noexcept;

    // Reverse lookup: find the ColliderId an entity registered. Returns
    // InvalidId when the entity has never registered (or has been
    // unregistered). Resolver uses this to filter "self" out of broadphase
    // results, and to call UpdateCollider after committing the new position.
    // Assumes one collider per entity — re-registration of the same entity
    // overwrites the previous mapping.
    [[nodiscard]] ColliderId FindColliderForEntity(Entity entity) const noexcept;

    // ---- Debug draw (developer/editor tool, not a player setting) ---------
    // Toggled at runtime by a dev key binding in demo builds and by the editor
    // IPC channel in Phase 40. CollectDebugDraw is a no-op while disabled.
    void               SetDebugDraw(bool enabled) noexcept { _debugDraw = enabled; }
    [[nodiscard]] bool GetDebugDraw() const noexcept { return _debugDraw; }

    // Emit outline rectangles for every registered collider (color-coded by
    // BodyKind / IsTrigger) and for every cached tile collider rect. Caller
    // supplies a 1×1 white texture + sampler used for the outline strokes —
    // the engine's UI white texture (RenderThread::GetUIWhiteTexture) is the
    // intended source. No-op if debug draw is disabled.
    void CollectDebugDraw(FramePacket&  packet,
                          TextureHandle whiteTex,
                          SamplerHandle whiteSampler) const;

    // Compute a world-space bounding AABB for a collider at the given position.
    // Handles axis-aligned box (fast path), rotated box (OBB bounds), and circle.
    [[nodiscard]] static AABB ComputeWorldAABB(const Collider2D& collider, Vec2 worldPosition);

private:
    // Pack an entity into a u64 key for the entity→ColliderId map. Generation
    // is in the upper 32 bits so reused indices with bumped generations get a
    // distinct key.
    [[nodiscard]] static constexpr u64 PackEntity(const Entity e) noexcept
    {
        return (static_cast<u64>(e.Generation) << 32) | static_cast<u64>(e.Index);
    }

    SpatialHash                         _hash;
    std::vector<ColliderRecord>         _colliders;
    std::vector<ColliderId>             _freeList;
    std::unordered_map<u64, ColliderId> _entityToCollider; // packed entity → id; one collider per entity
    ContactTracker                      _triggerContacts;
    ContactTracker                      _blockingContacts;
    ContactListener                     _contactListener;
    ContactTracker::PairSet             _enteredOverlaps; // see EnteredOverlaps()
    std::vector<ContactReport>          _contactReports;
    TileMap*                            _tilemap   = nullptr;
    bool                                _debugDraw = false;
};
} // namespace Arcbit
