# Arcbit 2D Physics & Collision — Design Document

Phase 22 of the Arcbit blueprint. This document covers the architecture for
collision, movement-blocking, triggers/interactions, sub-tile obstacles,
directional gating (e.g. walking under a tree from the side), and the
integration with the existing movement style components.

The system is **kinematic-first**, not dynamics-first: the engine target is a
top-down RPG, not a platformer or a physics sandbox. We don't simulate gravity,
mass, or impulses. Movement is authored — by `FreeMovement`, `SmoothTileMovement`,
or `SnapTileMovement` — and physics' job is to clamp it against the world.

---

## Goals

- **Stop movement on solid geometry**: tile solids, world props (trees, rocks,
  walls), and other characters block entity movement appropriately.
- **Sub-tile precision**: a small rock occupying a single tile should stop the
  player at the rock, not at the next tile boundary.
- **Directional gating**: a tree may block top/bottom approaches but allow
  walking underneath from the sides; a one-way ledge may only block one axis.
- **Interaction triggers**: walking into a door tile fires a callback;
  proximity to an NPC arms an interact prompt; sensor volumes don't block movement.
- **Customizable colliders**: each entity declares its own shape (AABB, circle,
  optionally a polygon), offset from `Transform2D.Position`, and layer/mask.
- **Clean per-style integration**: tile-style movement uses discrete plan-then-commit
  collision queries; free movement uses continuous swept collision with slide.
- **Live editing & debug**: wireframe debug draw of every collider, contact point
  visualization, and per-entity collider authoring through the editor (Phase 40).

Non-goals (for Phase 22):

- Rigid body dynamics (forces, masses, restitution, joints, constraints).
- Compound shapes / fixture trees. One shape per `Collider2D` is enough; an entity
  can carry multiple `Collider2D` components if it needs more (rare).
- Continuous Collision Detection (CCD) for tunneling at very high speeds. Sweep
  resolution covers the common cases; characters in this engine never move
  fast enough to tunnel through normal-sized obstacles.

Rotation **is** supported but treated as the slow path: a collider with
`Rotation == 0` is an AABB and goes through the fast AABB code; non-zero
rotation switches to the OBB code path (SAT-based). Rotation is expected to
be rare — most entities and props are axis-aligned — but the option is
available where it matters (rotated chests, projectiles fired at angles,
spinning hazards).

---

## Library Choice — Custom vs Box2D v3

The blueprint originally listed Box2D v3 via vcpkg. Reopening that decision now
that the requirements are clearer.

**What Box2D buys us:**
- A battle-tested broadphase (dynamic AABB tree).
- Rich shape types (capsules, polygons, chains).
- Sensor fixtures, contact callbacks, distance/raycast queries out of the box.
- Future-proofing if the project ever wants real dynamics (knockback, rolling
  barrels, projectile bounce).

**What it costs:**
- Pixel ↔ meter conversion at every API boundary (Box2D's tuning assumes
  1 m = 1 unit; sub-meter or huge worlds hit numerical issues).
- A C API (Box2D v3) bolted onto our C++ ECS with hand-written `b2BodyId` ↔
  `Entity` indirection.
- Most of its machinery (mass, inertia, integrators, solver iterations) is dead
  weight for a kinematic top-down RPG.
- One more vendored dependency that can break vcpkg builds.

**What custom buys us:**
- Coordinates stay in world pixels — no unit conversion, no scale tuning.
- Spatial broadphase is *trivial* given we already have a tile grid (uniform
  hash by tile coordinate; ~30 lines of code).
- Direct knowledge of our movement component model — `SmoothTileMovement` can
  ask "is tile (x,y) reachable?" without round-tripping through a body API.
- Total control over directional gating, slide rules, and trigger semantics.
- Estimated implementation: ~1500–2500 lines across broadphase, narrowphase,
  resolver, and ECS components.

**Recommendation: custom.** The requirements (no dynamics, axis-aligned shapes,
tile-grid-friendly broadphase, directional gating) all favour a purpose-built
system. Box2D's biggest wins (constraint solver, polygon SAT with rotation,
CCD) are exactly the features we don't need. The cost of writing what we *do*
need is modest, and the system stays small enough to understand end-to-end.

If a future phase introduces ragdolls, vehicles, or any rotation-heavy
mechanic, Box2D can be added then as an additional system without disrupting
this one.

The remainder of this document assumes the custom path. Switching to Box2D
later mostly affects the broadphase + narrowphase implementation; the public
component API and integration with the movement systems should stay the same.

---

## Coordinate System & Units

- All collider positions, sizes, velocities are in **world pixels**, matching
  the rest of the engine (Transform2D, Camera2D, sprite renderer).
- An entity's collider is **relative to `Transform2D.Position`**. The collider's
  `Offset` field positions it within the entity's local space.
- World axes: +X right, +Y down (matches sprite/UI conventions).

---

## Body Kinds

Two kinds, distinguished by who owns the position and how collisions resolve:

| Kind        | Position owner          | Resolves into other bodies? | Use case                                      |
|-------------|-------------------------|-----------------------------|-----------------------------------------------|
| `Static`    | World — never moves     | No — others resolve into it | Trees, rocks, walls, terrain colliders        |
| `Kinematic` | Movement system / script | Yes — pushed by resolver   | Player, NPCs, pushable boxes, AI agents       |

`Static` bodies are uploaded once and live in the broadphase forever. `Kinematic`
bodies update each tick before resolution.

Triggers are not a kind: `Collider2D::IsTrigger` turns either kind into a
trigger that only generates overlap events and never blocks movement — a
static door tile or interaction zone, or a kinematic hitbox that follows its
entity. (Early drafts had a `BodyKind::Trigger`; nothing honored it, so it was
removed in favor of the flag.)

There is no `Dynamic` kind in Phase 22. If a future phase needs it, we add it.

---

## Collider Shapes

Phase 22 ships with two shape primitives. Rectangles default to AABB and become
OBB when `Rotation != 0`.

```cpp
enum class ColliderShape : u8 { Box, Circle };

struct Collider2D
{
    ColliderShape Shape = ColliderShape::Box;
    Vec2          Offset {0.0f, 0.0f};   // local-space, relative to Transform2D.Position
    f32           Rotation = 0.0f;       // radians; 0 = axis-aligned (fast path)
    Vec2          HalfExtents {16.0f, 16.0f}; // Box only — half-width, half-height
    f32           Radius = 16.0f;        // Circle only

    BodyKind                  Kind        = BodyKind::Kinematic;
    std::vector<DirectionArc> BlockedFrom = DirectionArc::AllDirections();
    u32                       Layer       = 1;     // bit index — what this collider IS
    u32                       Mask        = ~0u;   // bit field — what this collider COLLIDES WITH
    bool                      IsTrigger   = false;
};
```

**Box vs OBB.** `Box` with `Rotation == 0` runs the fast AABB code path
(AABB↔AABB, AABB↔Circle). Any non-zero rotation flips it to the OBB code path
(SAT for OBB↔OBB, transform-into-local-space for OBB↔Circle). The fast and
slow paths are dispatched per-pair, so a rotated chest sitting next to dozens
of axis-aligned crates only pays the OBB cost on contacts that actually
involve the chest.

**Why two shapes (and not polygons)?** Boxes cover almost every case (props,
walls, tile solids, character bodies). Circles are useful for round entities
(slime, bomb radius, NPC interact range). Convex polygons add complexity for
narrow real benefit on a top-down grid; if a sprite needs an irregular shape,
attach two `Collider2D` components.

**Authoring tip**: for a character, a *slightly smaller-than-sprite* box
usually feels better than a tight one — they appear to graze obstacles without
catching corners.

---

## Tile Collision

The tilemap is the single largest source of static colliders. We integrate it
in the most lightweight way possible: **the broadphase queries the tile grid
directly**, no per-tile `Collider2D` entity is generated.

When the resolver needs colliders in a region, it asks:

1. The dynamic broadphase (entity colliders) — returns nearby entities.
2. The tilemap — returns AABBs synthesized on the fly from solid tile cells.

Adjacent solid tiles are merged into larger AABBs by a **greedy mesher** so
the broadphase sees one rectangle per contiguous solid region instead of one
per tile. A 20×20 wall is one AABB, not 400. This pays for itself immediately
on any non-trivial map and runs once per tilemap chunk at load (or on edit),
so we ship it from day one rather than as a profiling afterthought.

Greedy meshing rules:

- Merge runs of solid tiles along +X first into rows, then merge rows of equal
  width along +Y into rectangles. Standard greedy quad meshing.
- Tiles are merged only when their `TileDef` agrees on every collision-relevant
  field: `Solid`, `BlockedFrom` arcs, `Layer`. Tiles with different directional
  rules form separate rectangles even when adjacent.
- The mesh is rebuilt per-chunk on tile mutation. Per-chunk granularity keeps
  rebuild cost small and bounded — editing one tile only re-meshes its chunk.
- Tile colliders never appear in the dynamic broadphase hash; the tilemap
  exposes a `QueryTileColliders(worldAABB)` adapter that walks chunks the
  query rect overlaps and returns the merged rects in that area.

`TileDef.Solid` (already present from Phase 19) is the source flag. New flags
on `TileDef` are added for finer control:

```cpp
struct TileDef {
    // ... existing fields ...
    std::vector<DirectionArc> BlockedFrom = DirectionArc::AllDirections();
    u32                       Layer       = 1;     // tile collision layer
};
```

This keeps the tilemap as the canonical source of "where can I walk" data —
no parallel grid, no out-of-sync risk.

---

## Sub-Tile Obstacles

The user's small-rock-on-a-tile case. The rock is just an entity with its own
`Collider2D` — the underlying tile does *not* need to be marked solid:

```cpp
auto rock = world.CreateEntity();
world.AddComponent(rock, Transform2D{ .Position = TileCenter(5, 7) });
world.AddComponent(rock, SpriteRenderer{ /* small_rock sprite */ });
world.AddComponent(rock, Collider2D{
    .Shape       = ColliderShape::Box,
    .HalfExtents = {6.0f, 6.0f},   // much smaller than a 32px tile
    .Kind        = BodyKind::Static,
});
```

The collider lives in the dynamic broadphase as a static-but-entity-owned shape.
When the player walks onto that tile, free-movement collision stops them at the
rock's edge.

### Sub-Tile Stop in `SmoothTileMovement`

`SmoothTileMovement` lerps strictly tile-centre → tile-centre. Two policies for
how it interacts with sub-tile obstacles:

**(A) Tile-aligned, plan-then-commit (default):** Before starting a move,
sweep the entity's collider from current tile centre to target tile centre. If
*any* contact occurs along the path, the move is rejected — the entity stays
on its current tile. Pokémon-style behaviour. Cheap, predictable, snappy.

**(B) Allow partial moves (opt-in flag):** Same sweep, but on contact the
entity stops mid-tile at the contact point and the `Progress` field is frozen.
The entity is no longer tile-aligned until it resumes movement to a clear tile.

```cpp
struct SmoothTileMovement {
    // ... existing fields ...
    bool AllowPartialMove = false;   // (B) when true; (A) default
};
```

The user's described case (stop at the rock, don't reach centre) is policy
(B). Recommendation: ship (A) in Phase 22 and add (B) as a follow-up if a
specific scene needs it. The implementation cost of (B) is small (~50 lines)
once the sweep already exists; the worry is gameplay tuning — partial moves
can leave the player off-grid for tile-grid-aware systems (interact ray, NPC
pathfinding, scripted cutscenes), and that needs care.

---

## Directional Collision

Some props block from certain approach directions but not others. Examples:

- **Tree:** trunk-area collider blocks vertical approaches; sides allow
  walk-through (you walk under the canopy).
- **One-way ledge:** blocks moving down off the ledge but allows moving up onto
  it (or vice versa).
- **Bushes:** block horizontal movement only (you can hop over from above).

A four-bit N/S/E/W mask is too coarse for free movement: a player approaching
a tree at 30° off the vertical axis would either snap to a "north" hit and
block, or snap to "east" and pass straight through, with no smooth in-between.
Phase 22 uses **angular arcs** instead — a list of `(center, half-width)`
pairs in degrees, expressed in the collider's local space:

```cpp
struct DirectionArc
{
    f32 CenterDegrees;   // 0 = +X (east); 90 = +Y (south); -90 = north
    f32 HalfWidthDegrees;

    static std::vector<DirectionArc> AllDirections();   // [{0, 180}]
    static std::vector<DirectionArc> Vertical();        // top + bottom arcs
    static std::vector<DirectionArc> Horizontal();      // left + right arcs
    static std::vector<DirectionArc> NorthOnly(f32 halfWidth = 90.0f);
    static std::vector<DirectionArc> SouthOnly(f32 halfWidth = 90.0f);
    // ... small library of presets to cover common cases
};
```

The resolver computes the **contact direction** as the unit vector from the
static collider's center to the contact point (i.e., where on the collider
the moving entity hit it). It expresses that vector in the collider's *local*
frame — so if the collider is rotated, the arc rotates with it — and checks
whether the angle falls inside any of the `BlockedFrom` arcs. If yes, the
contact is a blocker; if no, the contact is dropped and movement passes
through.

Arcs gate getting *in*. A mover that is already inside a collider (it came
through a pass-through face, or was placed there) is not blocked by that
collider again until it has left, so it can back out the way it came or
continue through. Merely touching is different: moving into the touching face
is a contact (subject to the arcs), while sliding along it or moving away is
free.

```cpp
// Tree (axis-aligned): block top/bottom approaches; sides pass through.
// Default tunable: each arc covers ±60° around its axis, leaving a clear
// ±30° lane on each side.
treeCollider.BlockedFrom = DirectionArc::Vertical();

// Cliff edge dropping south: block movement coming from above (north side).
cliffCollider.BlockedFrom = DirectionArc::NorthOnly(80.0f);

// Custom: block a 120° arc centered north-east (a corner-only barrier).
postCollider.BlockedFrom  = { { -45.0f, 60.0f } };
```

**Local-frame rotation.** Storing arcs in the collider's local frame means a
rotated tree still blocks "from above and below" relative to the tree —
authors don't have to recompute angles when they rotate a prop. AABB-fast-path
colliders have `Rotation = 0`, so local frame == world frame and the angle
math collapses to a few comparisons.

**Tunable softness.** The half-width in each arc is the gameplay-tunable knob.
A wide arc (≥80°) feels solid and predictable; a narrow arc (≤30°) feels like
a thin barrier you slip past at small approach angles. The presets ship with
sensible defaults but every prop can override.

**Performance.** A typical entity has 0 or 1 arcs (default `AllDirections()`
fast-paths to "always block"). Arc count rarely exceeds 2–3. The check is a
handful of float ops per contact, paid only on contacts that pass broadphase.

**Note on triggers + directional:** Directional rules apply to blocking
contacts only. Triggers always fire on overlap — directional gating doesn't
gate event delivery (it gates resolution). If a designer wants directional
trigger gating ("only when entered from the north"), they read the contact
direction in the trigger callback and decide there.

---

## Triggers and Interaction

A `Collider2D` with `IsTrigger = true` does not contribute to resolution. It
generates `OnTriggerEnter` / `OnTriggerStay` / `OnTriggerExit` events instead.

Use cases:

- **Door / warp tile**: trigger on the door collider; `OnTriggerEnter`
  loads the next scene.
- **Interactable prop range**: a trigger around an NPC arms a "press E to talk"
  prompt; the player only sends an interact action while overlapping.
- **Damage volumes**: lava tiles or spike pits with a trigger fire damage
  events on `OnTriggerStay`.
- **Region scripts** (Phase 26): triggers fire scripted callbacks when the
  player enters a named area.

Phase 22 ships a callback-based API and the corresponding ECS event:

```cpp
struct TriggerCallback {
    std::function<void(Entity self, Entity other)> OnEnter;
    std::function<void(Entity self, Entity other)> OnStay;  // optional, every frame
    std::function<void(Entity self, Entity other)> OnExit;
};
```

The Lua scripting integration in Phase 26 will surface these as
`OnTriggerEnter(self, other)` script hooks; the Event Bus integration in
Phase 24 will publish `Trigger.Entered(triggerId, otherId)` events.

---

## Movement Integration

How the movement systems and the physics system fit together — this is where
the design needs to be explicit, because the existing systems don't yet know
about collision.

### Per-tick order

```
GameLoop tick:
  1. Input pump
  2. ECS update systems (in registration order):
     - LifetimeSystem
     - InputMovement / AIMovement (write MoveIntent; style systems read it)
     - --- physics planning phase ---
     - TileMovementPlanSystem    (SmoothTileMovement / SnapTileMovement)
     - FreeMovementIntegrateSystem (writes a tentative new position into Transform2D)
     - --- physics resolution phase ---
     - CollisionResolutionSystem (sweep, clamp, slide, fire trigger events)
     - --- post-physics ---
     - CameraFollowSystem
     - AnimatorStateMachineSystem (consumes final position)
  3. Render systems
```

The split into "planning" and "resolution" phases lets each movement style
write its tentative motion (or planned destination tile) and have a single
`CollisionResolutionSystem` arbitrate. It also gives a single point where
trigger events fire, regardless of which movement style is in play.

### `FreeMovement` integration

```
1. FreeMovementIntegrateSystem:
   - apply friction:    velocity *= exp(-friction * dt)
   - clamp to MaxSpeed
   - desiredDelta = velocity * dt
   - writes desiredDelta to a hidden PendingMove component

2. CollisionResolutionSystem:
   - for each entity with PendingMove + Collider2D:
       sweep collider from current Position by desiredDelta
       on first blocking contact:
           clamp position to contact point
           remove the velocity component along the contact normal (slide)
       continue swept-axis once after slide (one re-sweep) so a wall slide
           doesn't lose the tangent motion
   - commits final Position to Transform2D
```

One re-sweep is enough for the "slide along a wall" feel without recursive
solver depth. Players who hit a corner stop cleanly.

### `SmoothTileMovement` integration

```
1. TileMovementPlanSystem:
   - if Progress >= 1 and a direction is queued (or held):
       targetTile = currentTile + queuedDir
       ask CollisionResolutionSystem.QueryTileBlocked(entity, targetTile)
         which:
           - reads tile solid + BlockedFrom
           - sweeps entity collider from origin centre → target centre
           - reports contact (or none)
       if blocked:
           if AllowPartialMove: stop at contact distance, advance Progress proportionally
           else: drop the move, stay in place
       else:
           commit OriginWorld / TargetWorld; Progress = 0

2. (no per-tick sweep during transit — once the move is committed, the entity
    lerps freely; tile movement guarantees the path is clear)
```

This avoids per-frame collision tests during transit, keeps tile movement
snappy, and matches Pokémon's discrete-step behaviour.

### `SnapTileMovement` integration

Identical to `SmoothTileMovement` except the entity teleports to the target
on a successful query (no lerp). Same plan-then-commit flow. Landed in Phase
22H as `SnapTileMoveSystem`, with a `StepInterval` key-repeat.

### Pushing other entities

Two kinematic colliders meeting in the middle: the resolver gives priority to
whichever entity is currently moving. If both move in the same tick, the one
that ran first wins.

To shove something (a crate, a barrel), give it a `Pushable` component (22I).
When a free mover's sweep hits it, the resolver moves the pushable by the part
of the remaining motion that drives into it, scaled by `1 - Resistance`,
sweeping it against the world so it stops at walls. Then it sweeps the pusher
again, so the pusher follows at the pushable's speed. Shoves don't chain:
a crate pushed into a second crate stops.

### Push-out (depenetration)

Kinematic-first means nothing pushes a mover out of a collider. Normally that's
fine: sweeps stop movers at the surface. But an obstacle can move *into* a
mover: a rotating gate, a prop spawned or teleported on top of it. The push-out
system (22I) runs before movement each tick and moves free movers out of such
overlaps along the contact normal, by the penetration depth. The push is swept
like a move, so it never shoves a mover through a wall, and two free movers
overlapping each take half.

What it leaves alone is the overlap a mover made itself by walking into a
directional collider through a pass-through face. The resolver records those
(mover, collider) pairs when a move ends inside a collider it wasn't inside
before (`PhysicsWorld::EnteredOverlaps`); push-out skips them and drops each
record once the pair stops overlapping. Without that, one-way colliders would
spit the mover back out. Tile movers aren't pushed (it would break grid
alignment).

---

## Collision Events

For non-trigger contacts, two flavours of event are emitted:

**Engine-bus events** (Phase 24, Event Bus):

- `Collision.Started(a, b, normal, point)` — first frame of contact between
  two collider entities.
- `Collision.Ended(a, b)` — last frame of contact.

Skipped: `Collision.Stay`, because publishing per-tick contact events for
every overlapping pair scales poorly and almost never reads cleanly in game
code. Code that needs per-tick state should query the contact list directly.

**ECS-side callbacks** on `Collider2D` (or a paired `CollisionCallback`
component):

```cpp
struct CollisionCallback {
    std::function<void(Entity self, Entity other, Vec2 normal)> OnEnter;
    std::function<void(Entity self, Entity other)>              OnExit;
};
```

Trigger events use the `TriggerCallback` shown earlier.

---

## Spatial Broadphase

Uniform spatial hash keyed on the tilemap's tile size. Each cell holds a list
of entity collider IDs. Insert / remove on collider create / destroy / move;
a collider that crosses a cell boundary updates its membership.

Why a hash and not Box2D's AABB tree:

- The world's entities are already roughly tile-distributed.
- Hash ops are O(1) per cell touched; tile-aligned movement touches 1–4 cells.
- Implementation is tiny.

```
SpatialHash:
  cellSize: f32 (tilemap tile size)
  cells:   unordered_map<u64 cellKey, vector<ColliderId>>
  Insert(id, aabb):  for each cell in aabb.cells: cells[key].push_back(id)
  Remove(id, aabb):  for each cell in aabb.cells: erase id
  Query(aabb):       collect ids from all cells the aabb touches; dedupe
```

When the broadphase is asked for nearby colliders, it returns the union of
(a) entity colliders from the hash and (b) tile-synthesized AABBs from the
tilemap (queried by tile rect, not the hash).

---

## Debug Draw

**Not a player-facing setting.** Collider visualization is a developer/editor
tool — players never need it, so it doesn't belong in the graphics settings
screen. Instead, the engine exposes a `PhysicsDebugDraw` flag on
`PhysicsWorld` that any caller can flip on or off:

- **In demo / dev builds:** the game binds a debug action (e.g. `F2`) that
  toggles the flag. The same convention applies to other debug overlays —
  the tilemap's tile grid is its own toggle (e.g. `F3`); the existing FPS /
  debug-info overlay already follows this pattern but should be moved off
  `Settings::Graphics` for consistency in a future cleanup pass.
- **In the AvaloniaUI editor (Phase 40):** the editor sends signals over its
  IPC channel to flip the flag on the running game; no key-binding needed.
- **In shipped game builds:** the binding is simply not registered. Players
  have no way to enable it.

When enabled, the renderer emits:

- Collider outline in green (kinematic), red (static), yellow (trigger).
- Contact points last frame as small crosses.
- For directional colliders: arrows along each `BlockedFrom` arc, with arc
  span shown by the arrow spread.
- Selected entity (in editor): collider in cyan with handles for editing.

Implemented through the existing line-renderer path — when Phase 34 lands the
proper `DrawLine` API, debug draw migrates onto it. Until then, debug draw
emits short axis-aligned sprite-batch rectangles (matches what the FPS overlay
already does).

**Pending on Phase 34**: circle colliders currently render as their bounding
AABB. Once `DrawLine` exists, replace that with a proper N-segment polyline
circle (16 segments is a reasonable default). Bounding AABBs are actively
misleading when debugging circle-vs-something issues — grazing contacts near
the AABB corners look like collisions even when the underlying geometry test
correctly reports "no overlap". Tracked in the `CollectDebugDraw` source.

---

## Components & File Layout

```
engine/include/arcbit/physics/
    BodyKind.h              // enum BodyKind
    CollisionDirection.h    // enum CollisionDirection + ops
    Collider2D.h            // Collider2D component
    PhysicsWorld.h          // public API: Init, Tick, QuerySwept, QueryAABB, ...
    SpatialHash.h           // internal but headerable for tests

engine/src/
    Collider2D.cpp          // tiny — defaults, hot-reloadable JSON load
    PhysicsWorld.cpp        // broadphase + narrowphase + resolver + events
    PhysicsSystems.cpp      // ECS systems (Plan + Resolution)
    SpatialHash.cpp
```

`Tilemap` and `TileDef` get the new `BlockedFrom` / `Layer` fields but no new
files — physics queries the existing tilemap directly through a small adapter
in `PhysicsWorld`.

### Components added to ECS

```cpp
// Movement intent / planning (added by FreeMovement integration; consumed
// by CollisionResolutionSystem; cleared at end of frame).
struct PendingMove { Vec2 DesiredDelta; };

// Optional callbacks attached to any collider (avoids forcing function
// pointer storage onto every Collider2D).
struct CollisionCallback { /* OnEnter, OnExit */ };
struct TriggerCallback   { /* OnEnter, OnStay, OnExit */ };
```

---

## Collision Layers

Each `Collider2D` has a `Layer` (single bit — *what this collider IS*) and a
`Mask` (bitfield — *what this collider COLLIDES WITH*). Two colliders interact
only if `(a.Layer & b.Mask) != 0` AND `(b.Layer & a.Mask) != 0`. This is the
standard layer/mask scheme; it covers cases like:

- *Free-roaming NPC overlap* — one NPC's `Mask` simply omits the NPC layer,
  so NPCs pass through each other while still colliding with walls and the player.
- *Phasing enemies* — temporarily clear an enemy's `Mask` to ghost it through
  walls.
- *Projectile-vs-faction* — friendly projectiles mask in `Enemy`, omit `Player`.

### Engine defaults

The engine ships with a small set of pre-defined layers at fixed bit indices.
Project code can use these by name without any setup:

| Bit | Name        | Used for                                           |
|-----|-------------|----------------------------------------------------|
| 0   | `Default`   | Anything not classified — fallback                 |
| 1   | `Player`    | The player character                               |
| 2   | `NPC`       | Friendly / neutral NPCs                            |
| 3   | `Enemy`     | Hostile NPCs                                       |
| 4   | `Wall`      | Static world geometry, tile colliders              |
| 5   | `Prop`      | Static world props (trees, rocks, decoration)      |
| 6   | `Pickup`    | Items, coins, anything `OnTriggerEnter` collects   |
| 7   | `Projectile`| In-flight projectiles                              |
| 8   | `Trigger`   | Generic trigger volumes (door tiles, region marks) |

Bits 9–31 are reserved for game-defined layers.

### Authoring custom layers

`project.arcbit` (Phase 38) gains a `physics.layers` block:

```json
{
  "physics": {
    "layers": {
      "9":  "Boss",
      "10": "InvisibleWall",
      "11": "FishingBobber"
    }
  }
}
```

The same block can override the engine defaults' names if a project wants to
rename them, but the bit index of the engine layers stays fixed (renaming
`Enemy` to `Monster` is fine; reassigning bit 3 to something else is not — it
would break engine systems that reference `Enemy` by name).

Until Phase 38 lands, project code registers layers in C++ at startup
(`PhysicsWorld::RegisterLayer(9, "Boss")`).

---

## Implementation Phases

Suggested order of work — each step builds on the previous and ends in a
runnable / verifiable state:

### Phase 22A — Core types & static collision ✓
- [x] `BodyKind`, `Collider2D` headers + ECS registration. (`DirectionArc`
      lands in 22D — until then `BlockedFrom` is treated as "always block".)
- [x] `SpatialHash` with insert/remove/query.
- [x] `PhysicsWorld` skeleton: holds the hash, tilemap pointer, tick stub.
- [x] AABB↔AABB and AABB↔Circle narrowphase (the rotation==0 fast path).
- [x] Tile-synthesized colliders from `TileDef.Solid`, with greedy-mesh
      rectangle merging per chunk; rebuild on tile mutation.
- [x] Debug draw (kinematic green / static red), gated by a runtime
      `PhysicsDebugDraw` flag — toggled from a dev key binding in demos and
      from the editor IPC channel in Phase 40. **Not** a player-facing setting.

### Phase 22B — `FreeMovement` collision
- [x] `PendingMove` component + `FreeMovementIntegrateSystem` writes into it.
- [x] `CollisionResolutionSystem`: swept resolution, slide along contact normal,
      one re-sweep.
- [x] Layer / Mask filtering.
- [x] Free-movement demo: player can't walk through trees, slides along walls.

### Phase 22C — Tile movement collision
- [x] `PhysicsWorld::QueryTileBlocked(entity, targetTile)` for plan-then-commit.
- [x] `TileMovementPlanSystem` integrates `SmoothTileMovement` and (22H)
      `SnapTileMovement` with the query.
- [x] Tile-style demo: player can't enter solid tiles.

### Phase 22D — Directional collision (arcs)
- [x] `DirectionArc` type + preset library (`AllDirections`, `Vertical`,
      `Horizontal`, `NorthOnly`, etc.).
- [x] Narrowphase emits a contact direction; arc check filters blocking
      contacts. Arcs are evaluated in the collider's local frame so they
      rotate with the body.
- [x] `TileDef.BlockedFrom` field + tilemap loader / arc spec support.
- [x] Tree demo: walk under from sides, blocked top/bottom; verify smooth
      behaviour at off-axis approach angles for free movement.

### Phase 22E — Rotated colliders (OBB) ✓
- [x] OBB↔OBB narrowphase (SAT) + OBB↔Circle (transform-into-local).
- [x] OBB sweep — for Phase 22 ship a substepping fallback (split sweep into
      N small AABB-of-OBB-bounds steps); promote to a true OBB sweep only if
      profiling demands it. Rotation is rare.
- [x] Demo: rotated chest blocks correctly; spinning hazard rotates its
      directional arc with it.

### Phase 22F — Triggers & events ✓
- [x] `IsTrigger` flag on `Collider2D`.
- [x] `TriggerCallback` / `CollisionCallback` ECS components.
- [x] Enter/Exit detection (per-pair persistent set, diff each tick).
- [x] Door / warp-tile demo: trigger entity teleports the player.

### Phase 22G — Polish & integration
Parked behind Phase 24 (Event Bus) and Phase 40 (editor). After 22F, work
moves to 22H (movement controllers, tracked in `BLUEPRINT.md`); the
partial-move item below can land any time.
- [ ] Optional sub-tile partial-move policy (`SmoothTileMovement.AllowPartialMove`).
- [ ] Event Bus publishing (`Collision.Started`, `Collision.Ended`,
      `Trigger.Entered`, `Trigger.Exited`) — wired live when Phase 24 lands; until
      then the callback API is the integration surface.
- [ ] Editor (Phase 40) — collider authoring, live preview, directional arrows.

---

## Open Questions for Implementation

1. **Default arc half-widths.** The two-sided presets (`Vertical`,
   `Horizontal`) default to ±60° per arc; the one-sided presets (`NorthOnly`,
   ...) default to ±90° (a clean half-plane). Every preset takes the
   half-width as a parameter, and per-collider / per-tile arcs can set any
   value, so the knob is already authorable per prop — the editor (Phase 40)
   only needs to expose it.

   **The width is inert for box-vs-box contacts.** The arc check consumes
   `Sweep::Result.Normal`, and the slab method (axis-aligned box vs
   axis-aligned box — the common case, and every collider in
   `TileMoveArcDemo`) always reports a pure face normal (N/S/E/W). Any
   half-width strictly between 0° and 90° therefore behaves identically
   there; a diagonal
   approach blocks or passes based solely on which face it touches. The
   width only matters when the contact normal can be angled: circle
   colliders (the `ArcDemo` player is one) and rotated boxes. From 22E the
   substep path reports penetration normals rather than center-to-center
   vectors, so even there the width mostly bites at *corner* contacts —
   face contacts read as the face normal in the obstacle's local frame.

   Tuning (and any project-wide default — which would belong in project
   config, not player `Settings`) is deferred until 22E lands and the
   corner behavior can be playtested in `ArcDemo` / `ObbDemo`. Until then
   ±60° stays as the working default.
