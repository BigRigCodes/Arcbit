#include <arcbit/ecs/World.h>
#include <arcbit/ecs/AnimatorStateMachine.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/physics/PhysicsCallbacks.h>
#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/physics/Sweep.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/tilemap/TileMap.h>
#include <arcbit/core/Log.h>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Arcbit
{
namespace
{
    // ---------------------------------------------------------------------------
    // Built-in system helpers
    // ---------------------------------------------------------------------------

    Vec2 GetRefSize(const FramePacket& packet)
    {
        return packet.ReferenceSize.X > 0.0f
               ? packet.ReferenceSize
               : Vec2{static_cast<f32>(packet.Width), static_cast<f32>(packet.Height)};
    }

    // ---------------------------------------------------------------------------
    // Built-in system implementations
    // ---------------------------------------------------------------------------

    void RegisterLifetimeSystem(World& world)
    {
        world.RegisterSystem("Lifetime", [](Scene& scene, const f32 dt) {
            std::vector<Entity> toDestroy;

            scene.GetWorld()
                 .Query<Lifetime>()
                 .Without<Disabled>()
                 .ForEach([&](const Entity e, Lifetime& lt) {
                     lt.Remaining -= dt;
                     if (lt.Remaining <= 0.0f)
                         toDestroy.push_back(e);
                 });

            // The Scene's Collider2D OnRemove hook drops broadphase records.
            for (const Entity e : toDestroy) scene.GetWorld().DestroyEntity(e);
        });
    }

    // ---------------------------------------------------------------------
    // Movement intent (22H) — controllers write MoveIntent, styles read it.
    // ---------------------------------------------------------------------

    // Unit cardinal step for tile movers: the dominant axis of `v` (X on a
    // tie), reduced to its sign. Guards against AI or script intents that are
    // diagonal or not unit length.
    Vec2 CardinalStep(const Vec2 v)
    {
        const auto sign = [](const f32 x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); };
        if (std::abs(v.X) >= std::abs(v.Y)) return { sign(v.X), 0.0f };
        return { 0.0f, sign(v.Y) };
    }

    // Give every entity with `Controller` a MoveIntent. Done outside the
    // controller's own query: adding a component moves the entity to another
    // archetype, which isn't safe mid-iteration.
    template<typename Controller>
    void EnsureMoveIntent(World& ecs)
    {
        std::vector<Entity> missing;
        ecs.Query<Controller>().template Without<MoveIntent>().ForEach(
            [&](const Entity e, Controller&) { missing.push_back(e); });
        for (const Entity e : missing) ecs.AddComponent<MoveIntent>(e);
    }

    // Samples the four move actions into MoveIntent. Tile movers get a
    // cardinal direction (most recently pressed axis wins); free movers get a
    // normalized diagonal. Without an InputManager on the scene (tests,
    // tools) intent is left as-is; while input is blocked by the UI, every
    // action reads as released.
    void RegisterInputMovementSystem(World& world)
    {
        world.RegisterSystem("InputMovement", [](Scene& scene, f32 /*dt*/) {
            const InputManager* input = scene.GetInput();
            if (!input) return;
            World&     ecs  = scene.GetWorld();
            const bool live = !scene.IsInputBlocked();
            EnsureMoveIntent<InputMovement>(ecs);

            ecs.Query<InputMovement, MoveIntent>()
               .Without<Disabled>()
               .ForEach([&](const Entity e, InputMovement& im, MoveIntent& intent) {
                   const MoveKeys keys{ .Left  = live && input->IsPressed(im.MoveLeft),
                                        .Right = live && input->IsPressed(im.MoveRight),
                                        .Up    = live && input->IsPressed(im.MoveUp),
                                        .Down  = live && input->IsPressed(im.MoveDown) };
                   const bool tileMover = ecs.GetComponent<SmoothTileMovement>(e) != nullptr ||
                                          ecs.GetComponent<SnapTileMovement>(e) != nullptr;
                   intent.Set(ResolveInputDirection(im, keys, tileMover));
               });
        });
    }

    // Held entities stand still whatever their controller or script wants.
    void RegisterHoldSystem(World& world)
    {
        world.RegisterSystem("Hold", [](Scene& scene, f32 /*dt*/) {
            scene.GetWorld().Query<const Held, MoveIntent>().ForEach(
                [](const Held&, MoveIntent& intent) { intent.Direction = {}; });
        });
    }

    // Intent-driven free movers move at Speed in the intended direction and
    // stop the moment intent goes to zero.
    void ApplyIntentToFreeMovers(World& ecs)
    {
        ecs.Query<const MoveIntent, FreeMovement>()
           .Without<Disabled>()
           .ForEach([](const MoveIntent& intent, FreeMovement& fm) {
               fm.Velocity = intent.Direction * fm.Speed;
           });
    }

    void ApplyIntentToSmoothTileMovers(World& ecs)
    {
        ecs.Query<const MoveIntent, SmoothTileMovement>()
           .Without<Disabled>()
           .ForEach([](const MoveIntent& intent, SmoothTileMovement& stm) {
               stm.QueuedDir = CardinalStep(intent.Direction);
               stm.HasQueued = stm.QueuedDir != Vec2{};
           });
    }

    // An external push slower than this (px/s, squared) has died out.
    constexpr f32 ExternalRestSq = 0.01f * 0.01f;

    void RegisterFreeMovementSystem(World& world)
    {
        // Integrate-only: clamp velocity, accumulate desired delta (velocity
        // plus the external push) into PendingMove, decay friction. Transform is *not* touched here — that
        // lands in CollisionResolutionSystem (22B.4) via the PendingMove path.
        world.RegisterSystem("FreeMovement", [](Scene& scene, f32 dt) {
            ApplyIntentToFreeMovers(scene.GetWorld());
            scene.GetWorld()
                 .Query<FreeMovement, PendingMove>()
                 .Without<Disabled>()
                 .ForEach([dt](FreeMovement& fm, PendingMove& pm) {
                     if (fm.MaxSpeed > 0.0f) {
                         const f32 sqLen = LengthSquared(fm.Velocity);
                         if (sqLen > fm.MaxSpeed * fm.MaxSpeed) {
                             const f32 inv = fm.MaxSpeed / std::sqrt(sqLen);
                             fm.Velocity   = fm.Velocity * inv;
                         }
                     }
                     pm.DesiredDelta = pm.DesiredDelta + (fm.Velocity + fm.External) * dt;

                     const f32 decay = std::exp(-fm.Friction * dt);
                     fm.Velocity     = fm.Velocity * decay;
                     fm.External     = fm.External * decay;
                     if (LengthSquared(fm.External) < ExternalRestSq) fm.External = {};
                 });
        });
    }

    // ---------------------------------------------------------------------
    // CollisionResolutionSystem helpers
    // ---------------------------------------------------------------------

    // Overlap deeper than this (px) is "inside"; anything shallower is just
    // touching, which the resolver leaves after every stop. Matches the
    // sweep's own touching tolerance.
    constexpr f32 PenetrationSlop = 1e-3f;

    // Layer/Mask gate between two colliders. Both directions must pair for the
    // contact to count — matches Collider2D.h's documentation.
    inline bool LayersPair(const u32 layerA, const u32 maskA,
                           const u32 layerB, const u32 maskB) noexcept
    {
        return ((layerA & maskB) != 0) && ((layerB & maskA) != 0);
    }

    // Synthesize the conservative swept AABB enclosing curr → curr+delta.
    // The broadphase queries against this rect to find any collider the mover
    // might touch on the way; pairs are then narrowphased with SweepAgainst.
    inline AABB SweptAABB(const AABB& curr, const Vec2 delta) noexcept
    {
        const AABB target = AABB::FromMinMax(
            { curr.Min.X + delta.X, curr.Min.Y + delta.Y },
            { curr.Max.X + delta.X, curr.Max.Y + delta.Y });
        return AABB::FromMinMax(
            { std::min(curr.Min.X, target.Min.X), std::min(curr.Min.Y, target.Min.Y) },
            { std::max(curr.Max.X, target.Max.X), std::max(curr.Max.Y, target.Max.Y) });
    }

    // Keep the broadphase cache in step with a Transform2D write so later
    // queries this tick see the new position.
    void RefreshCollider(const Entity e, const Collider2D* col, const Vec2 pos, PhysicsWorld& physics)
    {
        if (!col) return;
        const PhysicsWorld::ColliderId id = physics.FindColliderForEntity(e);
        if (id != PhysicsWorld::InvalidId) physics.UpdateCollider(id, *col, pos);
    }

    void CommitPosition(PhysicsWorld& physics, const Entity e, const Collider2D& col, Transform2D& t, const Vec2 pos)
    {
        t.Position = pos;
        RefreshCollider(e, &col, pos, physics);
    }

    // State shared by one resolution pass; the vectors are scratch reused by
    // every broadphase query so per-entity work doesn't churn the heap.
    struct ResolveContext
    {
        PhysicsWorld&                         Physics;
        World&                                Ecs;
        std::vector<PhysicsWorld::ColliderId> EntityHits = {};
        std::vector<TileColliderRect>         TileHits   = {};
    };

    // Earliest blocking contact of a sweep and the entity it was with
    // (invalid for a tile or no hit).
    struct MoveHit
    {
        Sweep::Result Result;
        Entity        Other = Entity::Invalid();
    };

    // The entity collider behind a broadphase id, when it can block `col`:
    // active, not a trigger, layers pairing, and backed by ECS components.
    struct Blocker
    {
        Entity             Owner;
        const Collider2D*  Col;
        const Transform2D* T;
        f32                Rotation;
        const std::vector<DirectionArc>* Arcs;
    };

    std::optional<Blocker> FindBlocker(ResolveContext& ctx, const PhysicsWorld::ColliderId id,
                                       const Entity self, const Collider2D& col)
    {
        const auto& rec = ctx.Physics.GetRecord(id);
        if (!rec.Active || rec.IsTrigger || rec.Owner == self) return std::nullopt; // triggers never block
        if (!LayersPair(col.Layer, col.Mask, rec.Layer, rec.Mask)) return std::nullopt;
        const Collider2D*  otherCol = ctx.Ecs.GetComponent<Collider2D>(rec.Owner);
        const Transform2D* otherT   = ctx.Ecs.GetTransform(rec.Owner);
        if (!otherCol || !otherT) return std::nullopt; // registered without ECS components
        return Blocker{ rec.Owner, otherCol, otherT, rec.Rotation, &rec.BlockedFrom };
    }

    // Keep `r` if it's the earliest so far.
    void KeepEarliest(MoveHit& best, const Sweep::Result& r, const Entity other)
    {
        if (r.ToI < best.Result.ToI) best = { r, other };
    }

    void SweepEntities(ResolveContext& ctx, const Entity mover, const Collider2D& col,
                       const Vec2 origin, const Vec2 delta, MoveHit& best)
    {
        for (const PhysicsWorld::ColliderId id : ctx.EntityHits) {
            const std::optional<Blocker> b = FindBlocker(ctx, id, mover, col);
            if (!b) continue;
            const Sweep::Result r = Sweep::SweepAgainst(col, origin, delta, *b->Col, b->T->Position);
            // Arc gate — a contact outside the obstacle's BlockedFrom arcs
            // passes through. A mover already inside the obstacle never gets
            // here: the sweep reports no hit for it (see Sweep.h).
            if (r.Hit && IsContactBlocked(*b->Arcs, r.Normal, b->Rotation)) KeepEarliest(best, r, b->Owner);
        }
    }

    void SweepTiles(ResolveContext& ctx, const Collider2D& col, const Vec2 origin, const Vec2 delta, MoveHit& best)
    {
        for (const TileColliderRect& rect : ctx.TileHits) {
            if ((rect.Layer & col.Mask) == 0) continue; // tiles always block back; only need self.Mask check
            Collider2D tileCol{};
            tileCol.Shape       = ColliderShape::Box;
            tileCol.HalfExtents = rect.WorldAABB.HalfExtents();
            tileCol.Kind        = BodyKind::Static;
            tileCol.Layer       = rect.Layer;

            const Sweep::Result r = Sweep::SweepAgainst(col, origin, delta, tileCol, rect.WorldAABB.Center());
            // Tile rects are axis-aligned (greedy mesh); pass rotation 0.
            if (r.Hit && IsContactBlocked(rect.BlockedFrom, r.Normal, 0.0f)) KeepEarliest(best, r, Entity::Invalid());
        }
    }

    // Sweep one mover against every entity collider and tile rect along
    // origin → origin + delta. Returns the earliest blocking contact (or
    // {ToI=1, Hit=false}); an entity contact is reported to PhysicsWorld for
    // the ContactEvents system (22F).
    MoveHit SweepAgainstWorld(ResolveContext& ctx, const Entity mover, const Collider2D& col,
                              const Vec2 origin, const Vec2 delta)
    {
        const AABB swept = SweptAABB(PhysicsWorld::ComputeWorldAABB(col, origin), delta);
        ctx.Physics.QueryAABB(swept, ctx.EntityHits);
        ctx.Physics.QueryTileColliders(swept, ctx.TileHits);

        MoveHit best{};
        SweepEntities(ctx, mover, col, origin, delta, best);
        SweepTiles(ctx, col, origin, delta, best);
        if (best.Result.Hit && best.Other.IsValid()) ctx.Physics.ReportContact(mover, best.Other, best.Result.Normal);
        return best;
    }

    // Shove a Pushable the mover ran into by the part of the remaining motion
    // that drives into it, scaled down by its resistance. The pushed object
    // is swept against the world, so it stops at walls (and other crates —
    // pushes don't chain). Returns true if it moved.
    bool PushObstacle(ResolveContext& ctx, const MoveHit& hit, const Vec2 delta)
    {
        const Pushable* pushable = ctx.Ecs.GetComponent<Pushable>(hit.Other);
        const Collider2D* col    = ctx.Ecs.GetComponent<Collider2D>(hit.Other);
        Transform2D* t           = ctx.Ecs.GetTransform(hit.Other);
        if (!pushable || !col || !t || ctx.Ecs.GetComponent<Disabled>(hit.Other)) return false;

        // The normal points from the obstacle toward the mover.
        const Vec2 remaining = delta * (1.0f - hit.Result.ToI);
        const f32  into      = -Dot(remaining, hit.Result.Normal);
        if (into <= 0.0f) return false;

        const Vec2    shove   = hit.Result.Normal * -(into * (1.0f - std::clamp(pushable->Resistance, 0.0f, 1.0f)));
        const MoveHit blocked = SweepAgainstWorld(ctx, hit.Other, *col, t->Position, shove);
        if (blocked.Result.ToI <= 0.0f) return false;
        CommitPosition(ctx.Physics, hit.Other, *col, *t, t->Position + shove * blocked.Result.ToI);
        return true;
    }

    // Where one mover ends up: sweep the full delta, shove a Pushable it runs
    // into (then sweep again, now that it moved), and slide once along the
    // contact tangent with whatever motion is left.
    Vec2 ResolveMove(ResolveContext& ctx, const Entity e, const Collider2D& col, const Vec2 origin, const Vec2 delta)
    {
        MoveHit hit = SweepAgainstWorld(ctx, e, col, origin, delta);
        if (hit.Result.Hit && hit.Other.IsValid() && PushObstacle(ctx, hit, delta))
            hit = SweepAgainstWorld(ctx, e, col, origin, delta);

        const Vec2 pos = origin + delta * hit.Result.ToI;
        if (!hit.Result.Hit) return pos;

        const Vec2 remaining = delta * (1.0f - hit.Result.ToI);
        const Vec2 tangent   = { -hit.Result.Normal.Y, hit.Result.Normal.X };
        const Vec2 slide     = tangent * Dot(remaining, tangent);
        if (slide == Vec2{}) return pos;
        return pos + slide * SweepAgainstWorld(ctx, e, col, pos, slide).Result.ToI;
    }

    // How deep two colliders overlap (0 when they don't).
    f32 Penetration(const Collider2D& a, const Vec2 posA, const Collider2D& b, const Vec2 posB)
    {
        const Narrowphase::Contact c = Narrowphase::ComputeContact(a, posA, b, posB);
        return c.Hit ? c.Depth : 0.0f;
    }

    // Remember colliders this move carried the mover into (through a
    // pass-through face of a directional collider) so push-out leaves those
    // overlaps alone. Overlaps that already existed at `from` weren't made by
    // this move.
    void RecordEnteredOverlaps(ResolveContext& ctx, const Entity e, const Collider2D& col,
                               const Vec2 from, const Vec2 to)
    {
        ctx.Physics.QueryAABB(PhysicsWorld::ComputeWorldAABB(col, to), ctx.EntityHits);
        for (const PhysicsWorld::ColliderId id : ctx.EntityHits) {
            const std::optional<Blocker> b = FindBlocker(ctx, id, e, col);
            if (!b || Penetration(col, to, *b->Col, b->T->Position) <= PenetrationSlop) continue;
            if (Penetration(col, from, *b->Col, b->T->Position) > PenetrationSlop) continue;
            ctx.Physics.EnteredOverlaps().insert({ .A = e, .B = b->Owner });
        }
    }

    void ResolveMover(ResolveContext& ctx, const Entity e, Transform2D& t, PendingMove& pm, const Collider2D& col)
    {
        const Vec2 delta = pm.DesiredDelta;
        pm.DesiredDelta  = {};
        if (delta == Vec2{}) return;

        const Vec2 origin = t.Position;
        if (col.IsTrigger) { // triggers don't block — apply motion straight through
            CommitPosition(ctx.Physics, e, col, t, origin + delta);
            return;
        }
        CommitPosition(ctx.Physics, e, col, t, ResolveMove(ctx, e, col, origin, delta));
        RecordEnteredOverlaps(ctx, e, col, origin, t.Position);
    }

    // Collision resolution — replaces the 22B.3 ApplyPendingMove stub.
    //
    // Two passes:
    //   1. Entities with (Transform2D, PendingMove, Collider2D): swept resolve
    //      against the broadphase (shoving Pushables), slide once along the
    //      contact tangent, then commit the new position and refresh the
    //      broadphase.
    //   2. Entities with (Transform2D, PendingMove) but NO Collider2D: just
    //      apply the delta. Preserves the stub behavior so non-physical movers
    //      (ships, debug props) still update each tick.
    void RegisterCollisionResolutionSystem(World& world)
    {
        world.RegisterSystem("CollisionResolution", [](Scene& scene, f32 /*dt*/) {
            ResolveContext ctx{ .Physics = scene.GetPhysics(), .Ecs = scene.GetWorld() };
            ctx.Ecs.Query<Transform2D, PendingMove, Collider2D>()
               .Without<Disabled>()
               .ForEach([&](const Entity e, Transform2D& t, PendingMove& pm, Collider2D& col) {
                   ResolveMover(ctx, e, t, pm, col);
               });

            ctx.Ecs.Query<Transform2D, PendingMove>()
               .Without<Collider2D>()
               .Without<Disabled>()
               .ForEach([](Transform2D& t, PendingMove& pm) {
                   t.Position      = t.Position + pm.DesiredDelta;
                   pm.DesiredDelta = {};
               });
        });
    }

    // Push one free mover out of every collider overlapping it that it didn't
    // walk into itself. The push is swept like a move, so it can't shove the
    // mover through a wall; a mover pinned between two colliders stays put
    // until there's room. Two free movers overlapping split the push.
    void PushOutOfOverlaps(ResolveContext& ctx, const Entity e, const Collider2D& col, Transform2D& t,
                           const ContactTracker::PairSet& entered, ContactTracker::PairSet& kept)
    {
        ctx.Physics.QueryAABB(PhysicsWorld::ComputeWorldAABB(col, t.Position), ctx.EntityHits);
        const std::vector<PhysicsWorld::ColliderId> candidates = ctx.EntityHits; // the push sweep reuses the scratch

        for (const PhysicsWorld::ColliderId id : candidates) {
            const std::optional<Blocker> b = FindBlocker(ctx, id, e, col);
            if (!b) continue;
            const Narrowphase::Contact c = Narrowphase::ComputeContact(col, t.Position, *b->Col, b->T->Position);
            if (!c.Hit || c.Depth <= PenetrationSlop) continue;

            const EntityPair pair{ .A = e, .B = b->Owner };
            if (entered.contains(pair)) { // the mover went in on its own
                kept.insert(pair);
                continue;
            }
            const f32  share = ctx.Ecs.GetComponent<PendingMove>(b->Owner) ? 0.5f : 1.0f;
            const Vec2 push  = c.Normal * (c.Depth * share); // the normal points toward the mover
            CommitPosition(ctx.Physics, e, col, t, t.Position + push * SweepAgainstWorld(ctx, e, col, t.Position, push).Result.ToI);
        }
    }

    // Push-out (22I): free movers overlapping a collider they didn't walk
    // into — a rotating gate turning into them, a prop placed on top of them —
    // are moved out along the contact normal before this tick's movement.
    // There's no other depenetration, so without this an obstacle that moves
    // into a mover just leaves it embedded. Entered-overlap records survive
    // only while the pair still overlaps.
    void RegisterPushOutSystem(World& world)
    {
        world.RegisterSystem("PushOut", [](Scene& scene, f32 /*dt*/) {
            ResolveContext          ctx{ .Physics = scene.GetPhysics(), .Ecs = scene.GetWorld() };
            ContactTracker::PairSet kept;
            const ContactTracker::PairSet entered = std::move(ctx.Physics.EnteredOverlaps());

            ctx.Ecs.Query<Transform2D, PendingMove, Collider2D>()
               .Without<Disabled>()
               .ForEach([&](const Entity e, Transform2D& t, PendingMove&, Collider2D& col) {
                   if (!col.IsTrigger) PushOutOfOverlaps(ctx, e, col, t, entered, kept);
               });
            ctx.Physics.EnteredOverlaps() = std::move(kept);
        });
    }

    Vec2 TileCenterUnder(const Vec2 p, const f32 ts)
    {
        return { std::round(p.X / ts) * ts, std::round(p.Y / ts) * ts };
    }

    // What a tile step needs to plan against the world.
    struct TileStepContext
    {
        World&        Ecs;
        PhysicsWorld& Physics;
        f32           TileSize;
    };

    // Sokoban-style push: move a Pushable one tile along `dir` when the cell
    // beyond it is free (tiles, entities, arcs — a second crate blocks, so
    // pushes don't chain). Resistance only matters at 1 (immovable). The
    // crate's collider moves to the new cell right away, so the pusher's own
    // step then finds its target clear.
    bool TryPushTile(TileStepContext& ctx, const Entity crate, const Vec2 dir)
    {
        const Pushable*   pushable = ctx.Ecs.GetComponent<Pushable>(crate);
        const Collider2D* col      = ctx.Ecs.GetComponent<Collider2D>(crate);
        Transform2D*      t        = ctx.Ecs.GetTransform(crate);
        if (!pushable || !col || !t || pushable->Resistance >= 1.0f || ctx.Ecs.GetComponent<Disabled>(crate))
            return false;

        const Vec2 from = TileCenterUnder(t->Position, ctx.TileSize);
        const Vec2 to   = from + dir * ctx.TileSize;
        if (ctx.Physics.QueryTileBlocked(crate, *col, from, to)) return false;
        t->Position = to;
        RefreshCollider(crate, col, to, ctx.Physics);
        return true;
    }

    // Asks the resolver whether a one-tile step is clear, pushing a Pushable
    // out of the way if one is in it. A step that runs into an entity
    // reports the contact (pushed or not) so ContactEvents fires for tile
    // movers too. Movers without a Collider2D are never blocked. `pushed`
    // receives the crate shoved, if any.
    bool TileStepClear(TileStepContext& ctx, const Entity e, const Collider2D* col,
                       const Vec2 origin, const Vec2 target, Entity* pushed = nullptr)
    {
        PhysicsWorld::BlockingHit hit;
        if (!col || !ctx.Physics.QueryTileBlocked(e, *col, origin, target, &hit)) return true;
        if (!hit.Other.IsValid()) return false; // a tile

        ctx.Physics.ReportContact(e, hit.Other, hit.Normal);
        const Vec2 dir = (target - origin) * (1.0f / ctx.TileSize);
        if (!TryPushTile(ctx, hit.Other, dir) || ctx.Physics.QueryTileBlocked(e, *col, origin, target)) return false;
        if (pushed) *pushed = hit.Other;
        return true;
    }

    // Consumes the queued direction and starts a step from the tile under the
    // mover. Returns false (and drops the queued move) when the step is
    // blocked. Progress is left untouched — the caller decides where the new
    // step starts so overshoot from the previous step can carry over.
    bool TryBeginTileStep(TileStepContext& ctx, const Entity e, const Transform2D& t, SmoothTileMovement& stm,
                          const Collider2D* col)
    {
        const Vec2 origin = TileCenterUnder(t.Position, ctx.TileSize);
        const Vec2 target = origin + stm.QueuedDir * ctx.TileSize;

        stm.HasQueued = false;
        stm._pushing  = Entity::Invalid();
        if (!TileStepClear(ctx, e, col, origin, target, &stm._pushing)) return false;

        stm.OriginWorld = origin;
        stm.TargetWorld = target;
        return true;
    }

    // A crate pushed by a smooth mover rides one tile ahead of it for the
    // whole step, so the two glide in lockstep. (If the crate moved on its
    // own it would sometimes lag a tick, and a held push would stall at every
    // tile.) At the end of the step it sits exactly on its new tile.
    void CarryPushed(TileStepContext& ctx, const SmoothTileMovement& stm, const Vec2 pusherPos)
    {
        if (!ctx.Ecs.IsValid(stm._pushing)) return;
        Transform2D* t = ctx.Ecs.GetTransform(stm._pushing);
        t->Position    = pusherPos + (stm.TargetWorld - stm.OriginWorld);
        RefreshCollider(stm._pushing, ctx.Ecs.GetComponent<Collider2D>(stm._pushing), t->Position, ctx.Physics);
    }

    void TickSmoothMover(TileStepContext& ctx, const Entity e, Transform2D& t, SmoothTileMovement& stm, const f32 dt)
    {
        const Collider2D* col = ctx.Ecs.GetComponent<Collider2D>(e);

        if (stm.Progress >= 1.0f) {
            if (!stm.HasQueued || !TryBeginTileStep(ctx, e, t, stm, col)) return;
            stm.Progress = 0.0f;
        }

        stm.Progress += stm.Speed * dt;
        if (stm.Progress >= 1.0f && stm.HasQueued) {
            const f32 overshoot = stm.Progress - 1.0f;
            t.Position          = stm.TargetWorld; // round from the exact tile center
            CarryPushed(ctx, stm, t.Position);     // finish the last push before planning the next
            if (TryBeginTileStep(ctx, e, t, stm, col)) stm.Progress = overshoot;
        }

        const f32 alpha = std::min(stm.Progress, 1.0f);
        t.Position      = stm.OriginWorld + (stm.TargetWorld - stm.OriginWorld) * alpha;
        RefreshCollider(e, col, t.Position, ctx.Physics);
        CarryPushed(ctx, stm, t.Position);
    }

    void RegisterSmoothTileMoveSystem(World& world)
    {
        // Plan-then-commit tile movement (Phase 22C):
        //   - Idle + queued: snap an origin, compute the candidate target, ask
        //     the resolver if the path is clear; drop the queued move if not.
        //   - Arrival + queued: chain straight into the next step in the same
        //     tick, carrying the progress overshoot. Without this, every tile
        //     boundary cost a dead tick (planning without moving), which reads
        //     as a ~4 Hz stutter against the smoothed camera.
        //   - Mid-lerp: write the interpolated Position and refresh the
        //     broadphase cache so QueryTileBlocked / FreeMovement see truth.
        // Movers without a Collider2D bypass the query (and the cache update)
        // and behave the same as before — preserves stub behavior.
        world.RegisterSystem("SmoothTileMove", [](Scene& scene, const f32 dt) {
            TileStepContext ctx{ .Ecs = scene.GetWorld(), .Physics = scene.GetPhysics(),
                                 .TileSize = scene.GetTileMap().GetTileSize() };
            ApplyIntentToSmoothTileMovers(ctx.Ecs);

            ctx.Ecs.Query<Transform2D, SmoothTileMovement>()
               .Without<Disabled>()
               .ForEach([&](const Entity e, Transform2D& t, SmoothTileMovement& stm) {
                   TickSmoothMover(ctx, e, t, stm, dt);
               });
        });
    }

    // Float slack so a cooldown that should land exactly on zero after N
    // ticks (0.15 s = 9 ticks at 60 Hz) doesn't wait an extra tick.
    constexpr f32 CooldownEpsilon = 1e-4f;

    // One snap mover's tick. A fresh direction steps at once; a held one
    // repeats every StepInterval, carrying the remainder so the average rate
    // is exact. A blocked step clears the cooldown and retries next tick.
    void TickSnapMover(TileStepContext& ctx, const Entity e, Transform2D& t, SnapTileMovement& snap,
                       const MoveIntent& intent, const f32 dt)
    {
        const Collider2D* col = ctx.Ecs.GetComponent<Collider2D>(e);
        const f32         ts  = ctx.TileSize;
        const Vec2 dir = CardinalStep(intent.Direction);
        if (dir == Vec2{}) {
            snap.Cooldown = 0.0f;
            return;
        }
        if (snap.Cooldown <= CooldownEpsilon) {
            const Vec2 origin = TileCenterUnder(t.Position, ts);
            const Vec2 target = origin + dir * ts;
            if (!TileStepClear(ctx, e, col, origin, target)) { // a pushed crate has already jumped ahead
                snap.Cooldown = 0.0f;
                return;
            }
            t.Position = target;
            RefreshCollider(e, col, t.Position, ctx.Physics);
            snap.Cooldown += snap.StepInterval;
        }
        snap.Cooldown -= dt;
    }

    // Instant tile-to-tile movement, driven only by MoveIntent. Same
    // plan-then-commit query as SmoothTileMove, minus the lerp.
    void RegisterSnapTileMoveSystem(World& world)
    {
        world.RegisterSystem("SnapTileMove", [](Scene& scene, const f32 dt) {
            TileStepContext ctx{ .Ecs = scene.GetWorld(), .Physics = scene.GetPhysics(),
                                 .TileSize = scene.GetTileMap().GetTileSize() };
            ctx.Ecs.Query<Transform2D, SnapTileMovement, const MoveIntent>()
               .Without<Disabled>()
               .ForEach([&](const Entity e, Transform2D& t, SnapTileMovement& snap, const MoveIntent& intent) {
                   TickSnapMover(ctx, e, t, snap, intent, dt);
               });
        });
    }

    // ---------------------------------------------------------------------
    // ContactEvents system helpers (22F)
    // ---------------------------------------------------------------------

    // A blocking contact survives a tick without a fresh hit while the two
    // shapes stay within this many pixels — a mover that stops against a wall
    // is no longer sweeping into it but is still touching it.
    constexpr f32 ContactSkin = 1.0f;

    // Destroyed and Disabled entities don't take part in contacts. Dropping
    // their pairs before the diff makes an existing contact Exit.
    bool Participates(World& world, const Entity e)
    {
        return world.IsValid(e) && world.GetComponent<Disabled>(e) == nullptr;
    }

    void DropNonParticipants(World& world, std::vector<EntityPair>& pairs)
    {
        std::erase_if(pairs, [&](const EntityPair& p) {
            return !Participates(world, p.A) || !Participates(world, p.B);
        });
    }

    // Call one callback slot of `Component` on `self`, if present. The
    // std::function is copied before the call: a callback that adds or removes
    // components moves the entity's archetype storage, which would leave a
    // reference into the component dangling mid-call.
    template<typename Component, typename Fn, typename... Args>
    void InvokeCallback(World& world, const Entity self, Fn Component::* slot, const Args&... args)
    {
        if (!world.IsValid(self)) return;
        const Component* cb = world.GetComponent<Component>(self);
        if (!cb || !(cb->*slot)) return;
        const Fn fn = cb->*slot;
        fn(self, args...);
    }

    // The scene-wide listener (scripts) hears each event too. Copied before
    // the call, like the callbacks, and skipped for an entity gone since.
    void NotifyListener(PhysicsWorld& physics, World& world, const PhysicsWorld::ContactEvent event, const Entity self,
                        const Entity other, const Vec2 normal = {})
    {
        if (!physics.GetContactListener() || !world.IsValid(self)) return;
        const PhysicsWorld::ContactListener listener = physics.GetContactListener();
        listener(event, self, other, normal);
    }

    void DispatchTriggerEvents(PhysicsWorld& physics, World& world, const ContactTracker::Events& ev)
    {
        using enum PhysicsWorld::ContactEvent;
        const auto both = [&](const EntityPair& p, auto slot, const PhysicsWorld::ContactEvent event) {
            InvokeCallback(world, p.A, slot, p.B);
            NotifyListener(physics, world, event, p.A, p.B);
            InvokeCallback(world, p.B, slot, p.A);
            NotifyListener(physics, world, event, p.B, p.A);
        };
        for (const EntityPair& p : ev.Entered) both(p, &TriggerCallback::OnEnter, TriggerEnter);
        for (const EntityPair& p : ev.Stayed) both(p, &TriggerCallback::OnStay, TriggerStay);
        for (const EntityPair& p : ev.Exited) both(p, &TriggerCallback::OnExit, TriggerExit);
    }

    void UpdateTriggerContacts(PhysicsWorld& physics, World& ecs)
    {
        std::vector<EntityPair> overlaps;
        physics.CollectTriggerOverlaps(overlaps);
        DropNonParticipants(ecs, overlaps);

        ContactTracker::Events events;
        physics.TriggerContacts().Update(overlaps, events);
        DispatchTriggerEvents(physics, ecs, events);
    }

    using NormalMap = std::unordered_map<EntityPair, Vec2, EntityPairHash>;

    // This tick's blocking pairs: fresh hits reported by the movers, plus
    // already-active pairs still within ContactSkin. Pairs are ordered by
    // entity index so A-hits-B and B-hits-A are one contact; `normals` holds
    // each fresh pair's normal, flipped as needed to point from B toward A.
    std::vector<EntityPair> GatherBlockingPairs(PhysicsWorld& physics, NormalMap& normals)
    {
        std::vector<EntityPair> pairs;
        for (const auto& [Mover, Other, Normal] : physics.ContactReports()) {
            const bool       moverFirst = Mover.Index < Other.Index;
            const EntityPair pair       = moverFirst ? EntityPair { .A = Mover, .B = Other } : EntityPair{ .A = Other, .B = Mover };
            pairs.push_back(pair);
            normals.try_emplace(pair, moverFirst ? Normal : Normal * -1.0f);
        }
        physics.ContactReports().clear();

        for (const EntityPair& p : physics.BlockingContacts().Active())
            if (physics.IsTouching(p.A, p.B, ContactSkin)) pairs.push_back(p);
        return pairs;
    }

    void UpdateBlockingContacts(PhysicsWorld& physics, World& ecs)
    {
        NormalMap               normals;
        std::vector<EntityPair> pairs = GatherBlockingPairs(physics, normals);
        DropNonParticipants(ecs, pairs);

        ContactTracker::Events events;
        physics.BlockingContacts().Update(pairs, events);
        for (const EntityPair& p : events.Entered) {
            const auto it = normals.find(p);
            const Vec2 n  = it != normals.end() ? it->second : Vec2{};
            InvokeCallback(ecs, p.A, &CollisionCallback::OnEnter, p.B, n);
            NotifyListener(physics, ecs, PhysicsWorld::ContactEvent::CollisionEnter, p.A, p.B, n);
            InvokeCallback(ecs, p.B, &CollisionCallback::OnEnter, p.A, n * -1.0f);
            NotifyListener(physics, ecs, PhysicsWorld::ContactEvent::CollisionEnter, p.B, p.A, n * -1.0f);
        }
        for (const EntityPair& p : events.Exited) {
            InvokeCallback(ecs, p.A, &CollisionCallback::OnExit, p.B);
            NotifyListener(physics, ecs, PhysicsWorld::ContactEvent::CollisionExit, p.A, p.B);
            InvokeCallback(ecs, p.B, &CollisionCallback::OnExit, p.A);
            NotifyListener(physics, ecs, PhysicsWorld::ContactEvent::CollisionExit, p.B, p.A);
        }
    }

    // Runs after every movement system so contacts reflect final positions,
    // whichever movement style (or teleport) put the entity there. Each pass
    // finishes detection before any callback runs, so callbacks may mutate the
    // world; a trigger callback that teleports a mover is seen by the
    // blocking pass only through the skin check, which is the desired result
    // (teleporting away ends the contact).
    void RegisterContactEventsSystem(World& world)
    {
        world.RegisterSystem("ContactEvents", [](Scene& scene, f32 /*dt*/) {
            UpdateTriggerContacts(scene.GetPhysics(), scene.GetWorld());
            UpdateBlockingContacts(scene.GetPhysics(), scene.GetWorld());
        });
    }

    void RegisterCameraFollowSystem(World& world)
    {
        world.RegisterSystem("CameraFollow", [](Scene& scene, const f32 dt) {
        scene.GetWorld()
                .Query<const Transform2D, const CameraTarget>()
                .Without<Disabled>()
                .ForEach(
                    [&](const Transform2D& t, const CameraTarget& ct) {
                        const f32 smoothing = ct.Lag < 1.0f ? 10.0f * (1.0f - ct.Lag) : 0.001f;
                        scene.GetCamera().Follow(t.Position, smoothing, dt);
                    });
        });
    }

    // -------------------------------------------------------------------------
    // Per-tile UV computation helpers
    // -------------------------------------------------------------------------

    UVRect ComputeTileUV(const TileAtlasEntry& entry, const u32 tileId,
                         const TileDef*        def, const f32   elapsed,
                         const u32             tileX, const u32 tileY)
    {
        const u32 localId = tileId - entry.BaseId;
        const u32 cols    = entry.Atlas.Columns();

        if (def && !def->Animation.empty()) {
            // Flip-book: position-based phase prevents lockstep marching.
            u32 totalMs = 0;
            for (const auto& f : def->Animation) totalMs += f.DurationMs;
            if (totalMs == 0) return entry.Atlas.GetUV(localId % cols, localId / cols);

            const u32 phaseMs = (tileX * 37u + tileY * 53u) % totalMs;
            u32       t       = (static_cast<u32>(elapsed * 1000.0f) + phaseMs) % totalMs;
            u32       accum   = 0;
            for (const auto& f : def->Animation) {
                if (t < accum + f.DurationMs)
                    return entry.Atlas.GetUV(f.TileX, f.TileY);
                accum += f.DurationMs;
            }
            const auto& last = def->Animation.back();
            return entry.Atlas.GetUV(last.TileX, last.TileY);
        }

        // Static tile — use the ID's natural position in the atlas grid.
        UVRect uv = entry.Atlas.GetUV(localId % cols, localId / cols);

        if (def && (def->UVScroll.X != 0.0f || def->UVScroll.Y != 0.0f)) {
            const f32 sx = std::fmod(elapsed * def->UVScroll.X, 1.0f);
            const f32 sy = std::fmod(elapsed * def->UVScroll.Y, 1.0f);
            uv.U0        += sx;
            uv.U1        += sx;
            uv.V0        += sy;
            uv.V1        += sy;
        }
        return uv;
    }

    void RegisterTilemapRenderSystem(World& world)
    {
        using Clock     = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        world.RegisterRenderSystem("TilemapRender",
                                   [startTime = Clock::now()](Scene& scene, FramePacket& packet) mutable {
                                       const TileMap& tileMap = scene.GetTileMap();
                                       if (tileMap.GetChunks().empty()) return;

                                       const Camera2D& cam = scene.GetCamera();
                                       const Vec2 camPos = cam.GetEffectivePosition();
                                       const Vec2 ref = GetRefSize(packet);
                                       const f32 ts = tileMap.GetTileSize();
                                       const f32 elapsed = std::chrono::duration<f32>(Clock::now() - startTime).count();

                                       // Chunk world half-size for coarse culling.
                                       const f32  chunkWorld = static_cast<f32>(ChunkSize) * ts;
                                       const Vec2 chunkHalf  = {chunkWorld * 0.5f, chunkWorld * 0.5f};

                                       packet.TilemapChunksTotal = static_cast<u32>(tileMap.GetChunks().size());

                                       for (const auto& [key, chunk] : tileMap.GetChunks()) {
                                           // Decode chunk coords from key.
                                           const i32 chunkX = static_cast<i32>(static_cast<u32>(key >> 32));
                                           const i32 chunkY = static_cast<i32>(static_cast<u32>(key & 0xFFFFFFFF));

                                           // Coarse chunk visibility: center of chunk in world space.
                                           const Vec2 chunkCenter = {
                                               (static_cast<f32>(chunkX) * chunkWorld) + chunkWorld * 0.5f - ts * 0.5f,
                                               (static_cast<f32>(chunkY) * chunkWorld) + chunkWorld * 0.5f - ts * 0.5f,
                                           };
                                           if (!cam.IsVisible(chunkCenter, {
                                                                  chunkWorld + ts * 2.0f, chunkWorld + ts * 2.0f
                                                              }, ref))
                                               continue;

                                           ++packet.TilemapChunksRendered;

                                           for (u32 layer = 0; layer < LayerCount; ++layer) {
                                               for (u32 cell = 0; cell < ChunkSize * ChunkSize; ++cell) {
                                                   const u32 tileId = chunk.Tiles[layer][cell];
                                                   if (tileId == 0) continue;

                                                   const TileAtlasEntry* entry = tileMap.FindAtlas(tileId);
                                                   if (!entry || !entry->Atlas.IsValid()) continue;

                                                   const u32 localX = cell % ChunkSize;
                                                   const u32 localY = cell / ChunkSize;
                                                   const i32 tileX = chunkX * static_cast<i32>(ChunkSize) + static_cast<
                                                       i32>(localX);
                                                   const i32 tileY = chunkY * static_cast<i32>(ChunkSize) + static_cast<
                                                       i32>(localY);

                                                   const Vec2 worldPos = tileMap.TileToWorld(tileX, tileY);
                                                   if (!cam.IsVisible(worldPos, {ts, ts}, ref)) continue;

                                                   const TileDef* def = tileMap.FindTileDef(tileId);
                                                   const UVRect   uv  = ComputeTileUV(*entry, tileId, def, elapsed,
                                                       static_cast<u32>(tileX),
                                                       static_cast<u32>(tileY));

                                                   // Ground = far below Y-sort range, Objects = Y-sorted,
                                                   // Flat objects = just above ground (always below entities),
                                                   // Overlay = far above.
                                                   i32 sortLayer = -1000000;
                                                   if (layer == 1) {
                                                       const bool flat = def && def->Flat;
                                                       sortLayer = flat ? -999999
                                                                       : static_cast<i32>(worldPos.Y + ts);
                                                   } else if (layer == 2)
                                                       sortLayer = 1000000;

                                                   Sprite s{};
                                                   s.Texture  = entry->Atlas.GetTexture();
                                                   s.Sampler  = entry->Sampler;
                                                   s.Position = worldPos;
                                                   s.Size     = {ts, ts};
                                                   s.UV       = uv;
                                                   s.Layer    = sortLayer;
                                                   packet.Sprites.push_back(s);
                                                   ++packet.TilemapTilesRendered;
                                               }
                                           }
                                       }
                                   });
    }

    void RegisterSpriteRenderSystem(World& world)
    {
        world.RegisterRenderSystem("SpriteRender", [](Scene& scene, FramePacket& packet) {
            const Camera2D& cam    = scene.GetCamera();
            const Vec2      camPos = cam.GetEffectivePosition();
            const Vec2      ref    = GetRefSize(packet);

        scene.GetWorld()
                .Query<const Transform2D, const SpriteRenderer>()
                .Without<Disabled>()
                .ForEach(
                    [&](const Entity e, const Transform2D& t, const SpriteRenderer& sr) {
                        Vec2 pos = t.Position;
                        if (const auto* p = scene.GetWorld().GetComponent<Parallax>(e))
                        {
                            pos.X += camPos.X * (1.0f - p->ScrollFactor.X);
                            pos.Y += camPos.Y * (1.0f - p->ScrollFactor.Y);
                        }
                        // Pivot offset: shifts the rendered quad so the sprite's
                        // logical anchor aligns with Transform2D.Position.
                        // Pivot {0.5,0.5} = centre (no offset); {0.5,1.0} = bottom-centre.
                        pos.X += (0.5f - sr.Pivot.X) * t.Scale.X;
                        pos.Y += (0.5f - sr.Pivot.Y) * t.Scale.Y;
                        if (!cam.IsVisible(pos, t.Scale, ref))
                            return;

                        Sprite s{};
                        s.Texture   = sr.Texture;
                        s.Sampler   = sr.Sampler;
                        s.Position  = pos;
                        s.Size      = t.Scale;
                        s.UV        = sr.FlipX
                                          ? UVRect{ sr.UV.U1, sr.UV.V0, sr.UV.U0, sr.UV.V1 }
                                          : sr.UV;
                        s.Tint      = sr.Tint;
                        s.Rotation  = t.Rotation;
                        s.Layer     = sr.Layer;
                        packet.Sprites.push_back(s);
                    });
        });
    }

    void RegisterAnimatorStateMachineSystem(World& world)
    {
        world.RegisterSystem("AnimatorStateMachine", [](Scene& scene, const f32 /*dt*/) {
        scene.GetWorld()
                .Query<AnimatorStateMachine, Animator>()
                .Without<Disabled>()
                .ForEach([](AnimatorStateMachine& sm, Animator& anim) {
                    if (!sm.Sheet || sm.CurrentStateName().empty())
                        return;

                    const auto* state = sm.CurrentState();
                    if (!state)
                        return;

                    // Compute clip progress fraction for exit-time checks.
                    f32 progress = 0.0f;
                    if (anim.Clip && !anim.Clip->Frames.empty())
                    {
                        const u32 count = static_cast<u32>(anim.Clip->Frames.size());
                        progress = anim.Finished
                                       ? 1.0f
                                       : static_cast<f32>(anim.FrameIndex) / static_cast<f32>(count);
                    }

                    const std::string next = sm.EvaluateTransitions(progress);
                    if (next.empty())
                        return;

                    sm.TransitionTo(next);
                    const auto* newState = sm.CurrentState();
                    if (!newState)
                        return;

                    anim.Clip       = sm.Sheet->GetAnimation(newState->ClipName);
                    anim.FrameIndex = 0;
                    anim.Elapsed    = 0.0f;
                    anim.Finished   = false;
                });
        });
    }

    void RegisterAnimatorSystem(World& world)
    {
        world.RegisterSystem("Animator", [](Scene& scene, const f32 dt) {
        scene.GetWorld()
                .Query<Animator, SpriteRenderer>()
                .Without<Disabled>()
                .ForEach([dt](Animator& anim, SpriteRenderer& sr) {
                    if (!anim.Playing || !anim.Clip || !anim.Sheet || anim.Clip->Frames.empty())
                        return;

                    anim.Elapsed += dt;

                    const f32 frameSecs = anim.Clip->Frames[anim.FrameIndex].DurationMs / 1000.0f;
                    if (anim.Elapsed >= frameSecs)
                    {
                        anim.Elapsed -= frameSecs;
                        const u32 count    = static_cast<u32>(anim.Clip->Frames.size());
                        const u32 prevFrame = anim.FrameIndex;
                        ++anim.FrameIndex;

                        if (anim.FrameIndex >= count)
                        {
                            if (anim.Clip->Loop)
                                anim.FrameIndex = 0;
                            else
                            {
                                anim.FrameIndex = count - 1;
                                anim.Finished   = true;
                            }
                        }

                        // Fire frame events when the frame index actually changes.
                        if (anim.FrameIndex != prevFrame && anim.OnEvent)
                            for (const auto& ev : anim.Clip->Frames[anim.FrameIndex].Events)
                                anim.OnEvent(ev);
                    }

                    const std::string& frameName = anim.Clip->Frames[anim.FrameIndex].FrameName;
                    if (const auto frame = anim.Sheet->GetFrame(frameName))
                    {
                        sr.UV    = frame->UV;
                        sr.Pivot = frame->Pivot;
                    }
                });
        });
    }

    // Starts, moves, and stops spatial sounds. A source that leaves (entity
    // destroyed, component removed, scene unloaded) is stopped by Scene's
    // AudioSource OnRemove hook, not here.
    void RegisterAudioSystem(World& world)
    {
        world.RegisterSystem("Audio", [](Scene& scene, f32 /*dt*/) {
            // Move the listener with the camera so attenuation is camera-relative.
            AudioManager::SetListenerPosition(scene.GetCamera().GetEffectivePosition());

            scene.GetWorld()
                 .Query<const Transform2D, AudioSource>()
                 .Without<Disabled>()
                 .ForEach([](const Transform2D& t, AudioSource& src) {
                     if (!src._handle && src.Playing)
                         src._handle = AudioManager::CreateSpatialSound(src.Path, src.Loop, src.Volume);
                     if (!src._handle) return;

                     AudioManager::SetSpatialSoundPosition(src._handle, t.Position, src.Radius);
                     if (!src.Playing) { // stopped at runtime — release it
                         AudioManager::DestroySpatialSound(src._handle);
                         src._handle = nullptr;
                     }
                 });
        });
    }

    // ---------------------------------------------------------------------------
    // Shadow raycast helpers
    // ---------------------------------------------------------------------------

    // DDA grid traversal: returns distance from origin along unit-vector dir
    // to the nearest solid tile, or maxDist if none is found within range.
    f32 RayAABBDist(const Vec2     origin, const Vec2 dir, const f32 maxDist,
                    const TileMap& tileMap)
    {
        const f32 ts     = tileMap.GetTileSize();
        const f32 halfTs = ts * 0.5f;

        // TileToWorld returns tile centers (n*ts), so tile n visually spans
        // [n*ts - halfTs, n*ts + halfTs]. Shifting by +halfTs makes floor(x/ts)
        // align with those center-anchored boundaries.
        const f32 sx = origin.X + halfTs;
        const f32 sy = origin.Y + halfTs;

        i32 tileX = static_cast<i32>(std::floor(sx / ts));
        i32 tileY = static_cast<i32>(std::floor(sy / ts));

        const i32 stepX = dir.X >= 0.0f ? 1 : -1;
        const i32 stepY = dir.Y >= 0.0f ? 1 : -1;

        const f32 invX = dir.X != 0.0f ? ts / std::abs(dir.X) : 1e30f;
        const f32 invY = dir.Y != 0.0f ? ts / std::abs(dir.Y) : 1e30f;

        f32 tMaxX = dir.X != 0.0f
                    ? (dir.X > 0.0f ? ((tileX + 1) * ts - sx) : (sx - tileX * ts)) / std::abs(dir.X)
                    : 1e30f;
        f32 tMaxY = dir.Y != 0.0f
                    ? (dir.Y > 0.0f ? ((tileY + 1) * ts - sy) : (sy - tileY * ts)) / std::abs(dir.Y)
                    : 1e30f;

        f32 t = 0.0f;
        while (t < maxDist) {
            if (tileMap.BlocksLight(tileX, tileY))
                return t;

            if (tMaxX < tMaxY) {
                t     = tMaxX;
                tMaxX += invX;
                tileX += stepX;
            }
            else {
                t     = tMaxY;
                tMaxY += invY;
                tileY += stepY;
            }
        }
        return maxDist;
    }

    // Casts ShadowResolution rays from origin, recording the distance to the
    // first solid tile at each polar angle bucket.
    ShadowMapData ComputeShadowMap(const u32 lightIndex, const Vec2 origin,
                                   const f32 radius, const TileMap& tileMap)
    {
        ShadowMapData sm{};
        sm.LightIndex = lightIndex;
        for (u32 i = 0; i < ShadowResolution; ++i) {
            const f32  angle = (static_cast<f32>(i) / ShadowResolution) * TwoPi;
            const Vec2 dir   = {std::cos(angle), std::sin(angle)};
            sm.Distances[i]  = RayAABBDist(origin, dir, radius, tileMap);
        }
        return sm;
    }

    void RegisterLightRenderSystem(World& world)
    {
        world.RegisterRenderSystem("LightRender", [](Scene& scene, FramePacket& packet) {
            const Camera2D& cam     = scene.GetCamera();
            const Vec2      ref     = GetRefSize(packet);
            const TileMap&  tileMap = scene.GetTileMap();

            scene.GetWorld()
                 .Query<const Transform2D, const LightEmitter>()
                 .Without<Disabled>()
                 .ForEach(
                     [&](const Transform2D& t, const LightEmitter& le) {
                         if (!cam.IsLightVisible(t.Position, le.Radius, ref))
                             return;

                         const u32  lightIndex = static_cast<u32>(packet.Lights.size());
                         PointLight pl{};
                         pl.Position   = t.Position;
                         pl.Radius     = le.Radius;
                         pl.Intensity  = le.Intensity;
                         pl.LightColor = le.LightColor;
                         packet.Lights.push_back(pl);

                         if (le.CastsShadows && packet.ShadowMaps.size() < MaxShadowLights)
                             packet.ShadowMaps.push_back(
                                 ComputeShadowMap(
                                     lightIndex, t.Position, le.Radius, tileMap));
                     });
        });
    }
} // anonymous namespace


// ---------------------------------------------------------------------------
// Forward declaration — called from Scene.cpp
// ---------------------------------------------------------------------------
void RegisterBuiltinSystems(World& world);

// ---------------------------------------------------------------------------
// Registration entry point — called from Scene::Scene()
// ---------------------------------------------------------------------------

void RegisterBuiltinSystems(World& world)
{
    // Update phase — order matches the blueprint execution table.
    RegisterLifetimeSystem(world);
    RegisterInputMovementSystem(world);         // InputMovement → MoveIntent
    RegisterHoldSystem(world);                  // Held → no MoveIntent direction
    RegisterPushOutSystem(world);               // out of colliders that moved into a free mover
    RegisterFreeMovementSystem(world);          // MoveIntent → Velocity; writes PendingMove
    RegisterSmoothTileMoveSystem(world);        // MoveIntent → QueuedDir; writes Transform directly
    RegisterSnapTileMoveSystem(world);          // MoveIntent → instant tile steps
    RegisterCollisionResolutionSystem(world);   // sweeps PendingMove → Transform, refreshes broadphase
    RegisterContactEventsSystem(world);         // trigger + blocking-contact callbacks on final positions
    RegisterCameraFollowSystem(world);
    RegisterAnimatorStateMachineSystem(world); // evaluates transitions, updates Animator clip
    RegisterAnimatorSystem(world);             // advances frames, fires events, writes UV
    RegisterAudioSystem(world);                // spatial sound lifecycle + listener update

    // Render-collect phase — tilemap first so its sprites enter the shared sort.
    RegisterTilemapRenderSystem(world);
    RegisterSpriteRenderSystem(world);
    RegisterLightRenderSystem(world);
}
} // namespace Arcbit
