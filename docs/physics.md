# Collision & Physics

Arcbit's collision is **kinematic-first**: there's no gravity, mass or rigid
body simulation. Movement is authored - by `FreeMovement`, `SmoothTileMovement`
or `SnapTileMovement` (`docs/ecs.md`, "Movement") - and collision's job is to
clamp it against the world: tile solids, props, other characters. On top of that
sit sub-tile colliders, directional gating (walk under a tree from the side),
rotated colliders, triggers and contact callbacks, pushable objects and
push-out.

Arcbit uses its own collision rather than a physics library such as Box2D:
everything stays in world pixels (no unit conversion), the broadphase reuses
the tile grid, tile movers ask "can I step there?" directly, and directional
gating and slide rules are the engine's own. A library's solver, mass and
integrators would be unused weight for a top-down RPG.

Source: `engine/include/arcbit/physics/` and `engine/src/PhysicsWorld.cpp`,
`Narrowphase.cpp`, `Sweep.cpp`, `SpatialHash.cpp`, `ContactTracker.cpp`,
`DirectionArc.cpp`; the systems are in `engine/src/Systems.cpp`.

---

## Coordinates

- Collider positions, sizes and velocities are in **world pixels**, like
  `Transform2D`, `Camera2D` and sprites.
- A collider sits at its entity's `Transform2D.Position` plus its `Offset`.
- +X is right, +Y is down.

---

## Body kinds

| Kind        | Position owner            | Use                                     |
|-------------|---------------------------|-----------------------------------------|
| `Static`    | nobody - it never moves   | trees, rocks, walls, props              |
| `Kinematic` | a movement system / script | the player, NPCs, crates, moving gates  |

Static bodies stay in the broadphase as registered; kinematic ones are updated
as they move (`PhysicsWorld::UpdateCollider`, which the movement systems call).

A trigger isn't a kind: `Collider2D::IsTrigger` makes either kind report
overlaps without blocking - a static door zone, or a hitbox following its
entity.

---

## Colliders

```cpp
enum class ColliderShape : u8 { Box, Circle };

struct Collider2D
{
    ColliderShape Shape       = ColliderShape::Box;
    Vec2          Offset      = { 0.0f, 0.0f };
    f32           Rotation    = 0.0f;            // radians; 0 = AABB fast path
    Vec2          HalfExtents = { 16.0f, 16.0f }; // Box only
    f32           Radius      = 16.0f;            // Circle only

    BodyKind                  Kind        = BodyKind::Kinematic;
    u32                       Layer       = CollisionLayers::Default;
    u32                       Mask        = CollisionLayers::AllLayers;
    bool                      IsTrigger   = false;
    std::vector<DirectionArc> BlockedFrom = {}; // empty = block from all directions
};
```

A `Box` with `Rotation == 0` takes the fast AABB path (AABB-AABB, AABB-circle).
Any other rotation makes it an OBB: SAT for OBB-OBB, transform-into-local for
OBB-circle. The path is picked per pair, so one rotated chest among axis-aligned
crates only costs extra on its own contacts. Rotated sweeps substep (small
AABB-of-OBB steps); rotation is rare enough that this is cheap.

Boxes cover almost everything; circles suit round things. There are no
polygons - an irregular prop takes two colliders.

For a character, a box slightly smaller than the sprite feels better than a
tight one: it grazes obstacles instead of catching on corners.

In code, register a collider with the scene's physics after adding it
(`scene.GetPhysics().RegisterCollider(entity, collider, position)`); scene
files and the scene loader do this for you, and removing the component or
destroying the entity unregisters it.

---

## Tile collision

The tilemap is the biggest source of static geometry, and it never becomes
entities. `TileDef.Solid` marks a tile as blocking; adjacent solid tiles are
**greedy-meshed** per chunk into as few rectangles as possible (a 20x20 wall is
one rectangle, not 400) - rows along +X first, then equal-width rows along +Y.
Tiles only merge when they agree on everything collision cares about:

```cpp
struct TileDef {
    bool                      Solid       = false;
    std::vector<DirectionArc> BlockedFrom = {};                   // empty = all directions
    u32                       Layer       = CollisionLayers::Wall;
    // ...
};
```

A chunk's rectangles are rebuilt when one of its tiles changes, so editing a
tile re-meshes only its chunk. Tile rectangles don't live in the spatial hash;
queries walk the chunks they overlap and add the chunk's rectangles to the
entity colliders the hash returns.

---

## Sub-tile obstacles

A small rock on a tile is an entity with its own collider; the tile under it
needn't be solid:

```cpp
const Entity rock = world.CreateEntity();
world.GetTransform(rock)->Position = tileMap.TileToWorld(5, 7);
const Collider2D col{ .HalfExtents = { 6.0f, 6.0f }, .Kind = BodyKind::Static };
world.AddComponent<Collider2D>(rock, col);
(void)scene.GetPhysics().RegisterCollider(rock, col, world.GetTransform(rock)->Position);
```

A free mover stops at the rock's edge. A tile mover can't step onto that tile
at all: tile steps are **plan-then-commit** - the mover's collider is swept from
its tile center to the target's before the step starts, and any blocking contact
on the way cancels the step, so it stays put (Pokemon-style: cheap, predictable,
always on the grid). There's no partial step that stops mid-tile.

---

## Directional collision

Some things block from some directions only:

- **Tree**: the trunk blocks approaches from above and below; you walk under
  the canopy from the sides.
- **One-way ledge**: blocks coming off it, not climbing on (or the reverse).
- **Bush**: blocks horizontal movement only.

A four-way N / S / E / W mask is too coarse for free movement - an approach 30
degrees off vertical would snap one way or the other. `BlockedFrom` is a list of
**arcs** instead, `(center, half-width)` in degrees in the collider's local
frame:

```cpp
struct DirectionArc
{
    f32 CenterDegrees;    // 0 = +X (east), 90 = +Y (south), -90 = north
    f32 HalfWidthDegrees;

    // Presets:
    static std::vector<DirectionArc> AllDirections();                 // { 0, 180 }
    static std::vector<DirectionArc> Vertical(f32 halfWidth = 60);    // top and bottom
    static std::vector<DirectionArc> Horizontal(f32 halfWidth = 60);  // left and right
    static std::vector<DirectionArc> NorthOnly(f32 halfWidth = 90);   // also SouthOnly, EastOnly, WestOnly
};
```

The resolver takes the contact's direction (where on the collider the mover
hit it), turns it into the collider's local frame - so a rotated collider's
arcs rotate with it - and blocks only if it falls inside an arc. Outside every
arc, the contact is ignored and the mover passes through.

```cpp
tree.BlockedFrom  = DirectionArc::Vertical();      // sides pass through
cliff.BlockedFrom = DirectionArc::NorthOnly(80.0f); // blocks only from above
post.BlockedFrom  = { { -45.0f, 60.0f } };          // a corner barrier, north-east
```

Arcs gate getting **in**. A mover already inside a collider (it came through a
pass-through face, or was put there) isn't blocked by it again until it has
left, so it can back out or carry on through. Touching is different: moving
into a touching face is a contact (subject to the arcs); sliding along it or
moving away is free.

The half-width only matters when the contact direction can be angled - circle
colliders and rotated boxes. Box-against-box contacts always report a face
normal (north, south, east or west), so any half-width between 0 and 90 behaves
the same there: a diagonal approach blocks or passes by the face it touches.

Arcs gate blocking only. Triggers fire on any overlap; a trigger that cares
which way it was entered reads that in its callback.

---

## Triggers and contact callbacks

A trigger collider reports overlaps and never blocks. Two optional components
take the callbacks - on any entity with a `Collider2D`:

```cpp
struct TriggerCallback
{
    std::function<void(Entity self, Entity other)> OnEnter;
    std::function<void(Entity self, Entity other)> OnStay; // every tick while overlapping
    std::function<void(Entity self, Entity other)> OnExit;
};

struct CollisionCallback // blocking contacts
{
    std::function<void(Entity self, Entity other, Vec2 normal)> OnEnter; // normal points from other toward self
    std::function<void(Entity self, Entity other)>              OnExit;
};
```

Both sides of a pair hear the event. Contacts are tracked per pair across ticks
(`ContactTracker`) and diffed, so Enter and Exit fire once each. Scripts get the
same events as hooks - `OnTriggerEnter / Stay / Exit`, `OnCollisionEnter / Exit`
(`docs/scripting.md`).

Contacts don't go on the event bus: one event per contact would flood it. A
game publishes the ones that matter from its callback, in one line
(`docs/events.md`).

Typical uses: a door or warp zone (`OnEnter` switches scenes), a hazard
(`OnStay` deals damage), a pickup (`OnEnter` collects it), an NPC bump.

---

## Movement and collision, tick by tick

The built-in update systems run in this order (`docs/ecs.md` has the full
table):

```
Lifetime
InputMovement        MoveIntent from input
Hold                 Held entities: no MoveIntent direction
PushOut              free movers out of colliders that moved into them
FreeMovement         MoveIntent -> velocity -> PendingMove
SmoothTileMove       plan-then-commit tile steps, lerped
SnapTileMove         plan-then-commit tile steps, instant
CollisionResolution  sweep PendingMoves, clamp, slide, shove Pushables
ContactEvents        trigger and contact callbacks, on final positions
CameraFollow, FacingAnimation, AnimatorStateMachine, Animator, Audio
```

### Free movers

`FreeMovement` turns intent into a velocity (or keeps an authored one, decayed
by `Friction`), clamps it to `MaxSpeed`, adds the `External` push channel
(knockback, conveyors - decayed by friction, never clamped) and writes the tick's
delta into `PendingMove`. `CollisionResolution` then sweeps the collider along
it; at the first blocking contact it clamps to the surface, removes the motion
into the contact normal and sweeps the remainder once more, so a wall slide
keeps its tangent motion. A corner stops it cleanly.

### Tile movers

Before a step, the mover asks `PhysicsWorld::QueryTileBlocked` - tile solids
and their arcs, then a sweep of its collider from this tile center to the
next. Blocked, the step is dropped. Clear, it commits: `SmoothTileMovement`
lerps there (with no collision tests in transit - the path was clear) and
chains straight into a queued step on arrival; `SnapTileMovement` jumps there,
repeating every `StepInterval` while the direction is held.

### Pushing

Two kinematic colliders meeting: whichever is moving is blocked by the other.

A `Pushable` gets shoved instead. When a free mover's sweep hits one, the
pushable moves by the part of the remaining motion that drives into it, times
`1 - Resistance`, swept against the world so it stops at walls; the pusher is
swept again and follows. Pushes don't chain: a crate pushed into a crate stops.

Tile movers push Sokoban-style: a step blocked by a `Pushable` moves it one tile
on, if the cell beyond is clear, and the step is planned again. A smooth mover
carries the crate one tile ahead of itself for the step, so a held push never
stalls at a tile boundary.

### Push-out

Sweeps stop movers at surfaces, but an obstacle can move *into* a mover - a
rotating gate, a prop spawned or teleported onto it. `PushOut` runs before
movement and moves free movers out of such overlaps along the contact normal,
swept so it never shoves them through a wall; two overlapping free movers take
half each. Overlaps a mover made itself, entering a directional collider through
a pass-through face, are recorded by the resolver
(`PhysicsWorld::EnteredOverlaps`) and left alone until the pair separates - so
one-way colliders still work. Tile movers are never pushed out (it would break
their grid alignment).

---

## Broadphase

A uniform spatial hash with cells the size of a tile. Each cell lists the
entity colliders touching it; a collider crossing a cell boundary updates its
membership. Characters are roughly tile-sized and tile-distributed, so a query
touches one to four cells. Queries return the hash's colliders plus the
greedy-meshed tile rectangles of the chunks they overlap.

`PhysicsWorld` is created lazily by `Scene::GetPhysics()`, sized by
`WorldConfig.TileSize` - set the tile size before the first call.

---

## Collision layers

Each collider has a `Layer` (one bit: what it **is**) and a `Mask` (what it
**collides with**). Two colliders interact only if each one's layer is in the
other's mask. That covers NPCs passing through each other (their masks leave out
`NPC`), a ghosting enemy (clear its mask for a while), friendly fire (a player's
projectiles mask in `Enemy`, not `Player`).

The engine's layers (`CollisionLayers`):

| Bit | Name         | For                                          |
|-----|--------------|----------------------------------------------|
| 0   | `Default`    | anything unclassified                        |
| 1   | `Player`     | the player                                   |
| 2   | `NPC`        | friendly / neutral characters                |
| 3   | `Enemy`      | hostile characters                           |
| 4   | `Wall`       | tile geometry (the default for tile solids)  |
| 5   | `Prop`       | static props: trees, rocks                   |
| 6   | `Pickup`     | items, coins                                 |
| 7   | `Projectile` | projectiles                                  |
| 8   | `Trigger`    | trigger volumes: doors, regions              |

Bits 9 to 31 are a game's (`CollisionLayers::FirstUserBit`): use `1u << 9` and
up in code. Scene files write the engine layers by name (`"layer": "Prop"`,
`"mask": ["Player", "Wall"]`, `"mask": "all"`) and a game's bits as numbers.
Naming a game's layers - in `project.arcbit` - comes with Phase 38.

---

## Debug draw

Collider outlines are a developer tool, not a player setting. `PhysicsWorld`
has a flag:

```cpp
scene.GetPhysics().SetDebugDraw(true);
```

or `"physics": { "debug_draw": true }` in a scene file, or the console's
`colliders` command; the demos toggle it with F2. Entity colliders draw green
(kinematic), red (static) or yellow (trigger); tile rectangles red. Circles draw
as their bounding box until a line renderer lands (Phase 34) - keep that in mind
when a circle seems to collide early at a corner. The editor (Phase 40) will
flip it from outside the game.

---

## Files

```
engine/include/arcbit/physics/
    AABB.h, OBB.h           shapes, overlap tests
    BodyKind.h              Static / Kinematic
    Collider2D.h            the component
    CollisionLayers.h       engine layer bits
    ContactTracker.h        per-pair contacts across ticks
    DirectionArc.h          arcs and presets
    Narrowphase.h           pair tests, contact normals
    PhysicsCallbacks.h      TriggerCallback, CollisionCallback
    PhysicsWorld.h          registration, queries, QueryTileBlocked, debug draw
    SpatialHash.h           the broadphase
    Sweep.h                 swept tests, substepped OBB sweeps
    TileColliderRect.h      greedy-meshed tile rectangles
```

Tests: `tests/physics/` (geometry, narrowphase, sweeps, arcs, the hash,
contacts, PhysicsWorld) and `tests/ecs/` (movement, responses).
