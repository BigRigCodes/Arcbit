# Arcbit ECS — Design Document

Phase 16 of the Arcbit blueprint. This document covers the architecture, API,
component catalogue, system execution order, and integration strategy for the
entity-component-system that all game objects will use from this phase forward.

---

## Why ECS, and why archetype-based?

The two mainstream ECS layouts are **sparse-set** (entt-style) and **archetype**
(Unity DOTS-style). Arcbit uses archetypes.

For a 2D RPG with largely stable entity compositions — players, NPCs,
decorations, terrain tiles — archetypes win on cache locality. All entities
with the same component set live in tightly-packed contiguous arrays. Iterating
"all entities with Transform2D + SpriteRenderer" walks a single hot array per
archetype with no pointer chasing. Component addition/removal (moving an entity
to a new archetype) is infrequent and happens at setup time, so the overhead
there is acceptable.

---

## Core Data Model

### Entity

A generational index that prevents use-after-free bugs:

```cpp
struct Entity {
    u32 Index;       // position in the entity table
    u32 Generation;  // incremented on destroy; stale handles are rejected
};
```

The entity table is a flat array mapping `Index → (Archetype*, slot, generation)`.
When an entity is destroyed its generation is bumped; any handle still held by
game code will fail the generation check and be rejected by the World.

### Component type IDs

Assigned once at program startup via a static counter template — no RTTI,
no macros, no registration boilerplate:

```cpp
template<typename T>
ComponentTypeID TypeOf() {
    static const ComponentTypeID id = TypeRegistry::NextID();
    return id;
}
```

IDs are contiguous integers starting at 0. The first call for a given `T`
assigns its ID; subsequent calls return the same value.

### Component mask

A `u64` bitmask where bit N is set when component `TypeID == N` is present.
This caps the engine at **64 distinct component types**, which is ample for
this project (approximately 20 are planned across all phases). Expanding to
`std::bitset<128>` is a one-line change if needed later.

### Archetype

A bucket that owns all entities sharing an identical component set:

```cpp
struct Archetype {
    ComponentMask                                      Mask;
    std::unordered_map<ComponentTypeID, ComponentArray> Columns;
    std::vector<Entity>                                 Entities;
};
```

All column vectors are the same length — entry N across every column belongs
to the same entity. Destroying an entity is a swap-and-pop on every column
simultaneously, keeping each array dense.

### World

Owns all archetypes and the entity table. Archetypes are looked up in O(1) via
an `unordered_map<ComponentMask, Archetype>`.

**Removal hooks.** `World::OnRemove<T>(callback)` runs whenever a `T` leaves
an entity - `RemoveComponent<T>`, `DestroyEntity`, or `DestroyAllEntities` -
so outside resources tied to a component are released on every path. `Scene`
uses them to drop a `Collider2D` from the physics broadphase and stop an
`AudioSource`'s sound; its destructor destroys every entity, so a scene that
unloads leaves no invisible walls or orphaned loops behind.

---

## Scene

`Scene` is the top-level container for a running game world:

```
Scene
├── World          — all entities, archetypes, and systems
├── Camera2D       — active camera (driven by CameraFollowSystem)
├── WorldConfig    — scene-wide constants (tile size, gravity)
├── TileMap        — the scene's tiles and atlases
└── PhysicsWorld   — created on first GetPhysics(), wired to the TileMap
```

`Application` owns a `SceneManager` (`GetScenes()`) holding a stack of scenes;
`GetScene()` is the active one. A scene is a C++ subclass overriding the
lifecycle hooks (`OnPrepare`, `OnLoad`, `OnUnload`, `OnSuspend`, `OnResume`,
`OnUpdate`, `OnRender`) or a data-driven `FileScene` built from an
`.arcscene` file. Registering scenes, switching with fades, the scene stack,
loading in the background and the boot flow are in [scenes.md](scenes.md).
A game that registers no scenes gets one plain, unnamed `Scene` it fills in
`OnStart`.

Textures and sprite sheets a scene loads through `Scene::LoadTexture` /
`LoadSpriteSheet` are reference-counted and released when it unloads.

### WorldConfig

Scene-wide constants that systems read instead of hard-coding per-entity:

```cpp
struct WorldConfig {
    f32  TileSize = 16.0f;           // world pixels per tile (used by tile movement systems)
    Vec2 Gravity  = { 0.0f, 0.0f }; // pixels/s² (zero = top-down, no gravity)
};
```

The game dev sets this once per scene. Movement style components never store
tile size — they read it from here.

---

## Required Components

Every entity **must** carry these two components. The World enforces this at
`CreateEntity` time.

| Component     | Purpose |
|---------------|---------|
| `Transform2D` | World position, scale, and rotation — the canonical spatial state for every entity |
| `Tag`         | A single string classification used for filtering queries (`"Player"`, `"Enemy"`, `"Wall"`) |

`Tag` defaults to `"Entity"` if not supplied. `Transform2D` defaults to origin
with unit scale and zero rotation.

### Why not Name?

`Name` is intentionally **not** required. Terrain tiles, pooled enemies, and
decorative props do not need stored names — requiring them would create thousands
of unnecessary `std::string` allocations with no value.

The editor derives a display label on the fly from Tag + the first four
characters of the entity's scene-file UUID:

```
Wall   #3f25
Player #a91c
Chest  #77bd
```

`Name` is added explicitly for entities with game-design identity: the player,
named NPCs, quest objects, anything referenced by name in a dialog graph or
Lua script.

---

## Component Catalogue

### Transform2D *(required)*

```cpp
struct Transform2D {
    Vec2 Position = {};
    Vec2 Scale    = { 1.0f, 1.0f };
    f32  Rotation = 0.0f;            // radians, clockwise
};
```

The single source of truth for where an entity is in the world. All render and
movement systems read and write here.

### Tag *(required)*

```cpp
struct Tag {
    std::string Value = "Entity";
};
```

Used as a coarse filter in queries (see `WithTag()` below). Choosing a tag is
the game dev's responsibility; the engine has no reserved tag strings.

### Name *(optional)*

```cpp
struct Name {
    std::string Value;
};
```

Add only when the entity needs a stable human-readable identity. The editor
uses it in the entity list and dialog graph node pickers.

### SpriteRenderer *(optional)*

Visual representation without duplicating position (Transform2D owns that).

```cpp
struct SpriteRenderer {
    TextureHandle Texture;
    SamplerHandle Sampler;
    UVRect        UV     = { 0.0f, 0.0f, 1.0f, 1.0f };
    Color         Tint   = Color::White();
    i32           Layer  = 0;
};
```

`SpriteRenderSystem` reads `(Transform2D, SpriteRenderer)` and pushes a `Sprite`
into `FramePacket.Sprites`. Frustum culling is applied at this point. If the
entity also has a `Parallax` component the position is adjusted before submission
(see `Parallax` below).

UV rectangles come from `SpriteSheet`, which loads the unified Arcbit sprite
format (see `docs/sprite-format.md`). The format covers named frames, tile grids,
and animation clips in one file. The `Animator` component (Phase 17) will drive
`UV` automatically from the active clip; until then it is set directly by game
code via `SpriteSheet::GetTile` or `SpriteSheet::GetSprite`.

### LightEmitter *(optional)*

Light properties without position (Transform2D owns that).

```cpp
struct LightEmitter {
    f32   Radius     = 200.0f;
    f32   Intensity  = 1.0f;
    Color LightColor = Color::NaturalLight();
};
```

`LightRenderSystem` reads `(Transform2D, LightEmitter)`, applies frustum culling
via `Camera2D::IsLightVisible`, and pushes a `PointLight` into
`FramePacket.Lights`.

### Parallax *(optional)*

Scroll rate multiplier for background and foreground layers.

```cpp
struct Parallax {
    Vec2 ScrollFactor = { 0.5f, 0.5f };
    // 0.0 = fixed (sky, UI overlay)
    // 1.0 = normal world layer (moves 1:1 with camera)
    // 0.5 = mid-ground (moves at half camera speed)
};
```

`SpriteRenderSystem` adds `cameraPos * (1 - scrollFactor)` to the entity's world
position before submitting it to the packet. No Transform2D mutation; no extra
draw batches.

Derivation: the shader subtracts cameraPos from world position, so injecting
`cameraPos * (1 - s)` into world position gives an effective camera offset of
`cameraPos * s` — exactly the desired scroll rate.

### CameraTarget *(optional)*

Marks the entity the scene camera should follow. Only one entity should carry
this at a time; if multiple carry it, `CameraFollowSystem` picks the first one
found.

```cpp
struct CameraTarget {
    f32 Lag = 0.1f;  // lerp factor per second; 0.0 = instant snap, 1.0 = no follow
};
```

### Disabled *(optional)*

Empty marker component. All built-in systems use `.Without<Disabled>()` so
disabled entities are invisible to them. Useful for entity pooling, off-screen
NPCs, and inactive triggers.

```cpp
struct Disabled {};
```

### Lifetime *(optional)*

Auto-destroys the entity after a fixed duration. Intended for projectiles,
particles, and temporary effects.

```cpp
struct Lifetime {
    f32 Remaining;  // seconds until DestroyEntity is called
};
```

---

## Movement Components

Movement uses a **mix-and-match** design: a controller component decides
*where* the entity wants to go; a style component decides *how* it gets there.
Any controller pairs with any style. The two meet at `MoveIntent`: controllers
write it, style systems read it, and speed belongs to the style.

### MoveIntent

```cpp
struct MoveIntent {
    Vec2 Direction;               // zero = idle; length <= 1; cardinal for tile movers
    Vec2 Facing = { 0.0f, 1.0f }; // last nonzero Direction; starts facing down
    void Set(Vec2 dir);           // writes Direction and keeps Facing up to date
};
```

Game code reads `MoveIntent` for facing and animation selection instead of
polling input. Scripts and cutscenes can add one and call `Set` to drive any
style without a controller. Style systems reduce a non-cardinal intent to its
dominant axis for tile movers, so an AI heading like `(0.3, 0.8)` steps down.

### Controller components

#### InputMovement

```cpp
struct InputMovement {
    ActionID MoveLeft;
    ActionID MoveRight;
    ActionID MoveUp;
    ActionID MoveDown;
    // Most-recent-key state, maintained by the system:
    u8   HeldMask       = 0;
    bool PreferVertical = false;
    bool PreferLeft     = false;
    bool PreferUp       = false;
};
```

`InputMovementSystem` reads the four actions each tick and writes `MoveIntent`
(adding one if the entity lacks it). The logic lives in
`ResolveInputDirection`. The most recently pressed key wins every conflict:

- Opposite keys on one axis: the one pressed last. Holding right and then
  pressing left walks left; releasing left resumes right. This applies to
  free movers too.
- Free movers get diagonals, normalized to unit length.
- Tile movers (`SmoothTileMovement` or `SnapTileMovement` present) get one axis:
  the one whose key was pressed most recently. Holding right and then pressing
  up turns up; releasing up resumes right. Keys on both axes going down on the
  same tick prefer X.

While a `BlocksInput` UI screen is up, every action reads as released, so the
entity stops instead of keeping its last intent. Application wires its
`InputManager` into the scene (`Scene::SetInput`); without one (tests, tools)
the system leaves intent alone.

#### AIMovement

```cpp
struct AIMovement {
    Vec2 TargetPosition;
    bool HasTarget = false;
};
```

Driven externally by the pathfinding system (Phase 30). `AIMovementSystem`
will translate `TargetPosition` into `MoveIntent`, so the style layer is
shared with `InputMovement`.

### Style components

#### FreeMovement

Pixel-perfect velocity integration. Use for combat, platformer sections, or
any entity that needs sub-tile positioning.

```cpp
struct FreeMovement {
    Vec2 Velocity;
    f32  Friction = 8.0f;   // Velocity (without intent) and External scale by exp(-Friction * dt)
    f32  MaxSpeed = 0.0f;   // clamp |Velocity| after integration; 0 = unlimited; External isn't clamped
    f32  Speed    = 100.0f; // world pixels/s at full MoveIntent
    Vec2 External;          // world push in px/s, added on top of Velocity
    void AddImpulse(Vec2 velocity);
};
```

With a `MoveIntent`, `Velocity` is set to `Direction * Speed` every tick:
instant start and stop, so `Friction` has no visible effect on it. Without
one, `Velocity` is yours to drive (ships, projectiles) and `Friction` decays
it.

`External` (Phase 22I) is movement from the world: knockback, a bumper, a
conveyor. `AddImpulse` adds to it; it moves the entity on top of `Velocity`
and always decays with `Friction`, so it works on intent-driven movers too
(writing an impulse to `Velocity` would be overwritten next tick). A burst of
speed `v` travels about `v / Friction` pixels. Acceleration curves are future
work.

#### Pushable

```cpp
struct Pushable {
    f32 Resistance = 0.5f; // 0 = moves as fast as the pusher; 1 = immovable
};
```

A mover walking into a `Pushable` entity (which needs a `Collider2D`) shoves
it instead of stopping. Pushes stop at walls, and the pusher with them; they
don't chain through a second `Pushable`.

- **Free movers** shove it along by the part of their motion that drives into
  it, scaled by `1 - Resistance`, swept against the world.
- **Tile movers** push it one tile, Sokoban-style, when the cell beyond is
  free (`Resistance` only matters at 1: immovable). A smooth mover carries the
  crate one tile ahead of itself for the whole step, so a held push glides
  without stalling at each tile; a snap mover moves it instantly. Keep a
  tile-pushed crate on the grid by not letting free movers shove it too.

#### SmoothTileMovement

Lerps from tile center to tile center. Input is queued during transit so the
next direction is consumed the moment the entity lands. With a `MoveIntent`,
`QueuedDir` / `HasQueued` are rewritten from it every tick; without one, queue
directly.

```cpp
struct SmoothTileMovement {
    f32  Speed     = 4.0f;    // tiles/second (tile size comes from the TileMap)
    Vec2 OriginWorld;
    Vec2 TargetWorld;
    f32  Progress  = 1.0f;   // 0→1 lerp progress; 1.0 means arrived
    Vec2 QueuedDir;
    bool HasQueued = false;
};
```

#### SnapTileMovement

Moves instantly to the adjacent tile. No interpolation — suitable for puzzle
games and strict grid-based overworlds. Driven only by `MoveIntent`. A fresh
direction steps at once; a held one repeats every `StepInterval`. A blocked
step reports the contact (so `CollisionCallback` fires) and retries next tick.

```cpp
struct SnapTileMovement {
    f32 StepInterval = 0.15f; // seconds between steps while a direction is held
    f32 Cooldown     = 0.0f;  // time until the next step may start
};
```

### Pairing examples

| Entity | Controller | Style |
|--------|-----------|-------|
| Overworld player | `InputMovement` | `SmoothTileMovement` |
| Puzzle NPC | `AIMovement` | `SnapTileMovement` |
| Combat enemy | `AIMovement` | `FreeMovement` |
| Combat player | `InputMovement` | `FreeMovement` |

---

## System Registration

Systems are registered as named `std::function` callbacks — no base class, no
virtual dispatch. Each system may provide an update function, a render-collect
function, or both.

```cpp
// Update system — runs every fixed tick
scene.GetWorld().RegisterSystem("FreeMovement",
    [](Scene& scene, f32 dt) {
        scene.GetWorld().Query<Transform2D, FreeMovement>()
            .Without<Disabled>()
            .Each([dt](Transform2D& t, FreeMovement& fm) {
                // integrate velocity, apply friction
            });
    });

// Render system — runs every frame, fills FramePacket
scene.GetWorld().RegisterRenderSystem("SpriteRender",
    [](Scene& scene, FramePacket& packet) {
        const Vec2 camPos = scene.GetCamera().GetEffectivePosition();
        scene.GetWorld().Query<Transform2D, SpriteRenderer>()
            .Without<Disabled>()
            .Each([&](Entity e, Transform2D& t, SpriteRenderer& s) {
                // parallax adjust, frustum cull, push to packet.Sprites
            });
    });
```

Systems execute in registration order. The engine registers all built-in
systems first; game code registers its systems in `OnStart` and they run after.

---

## Built-in System Execution Order

### Update phase (fixed timestep)

| Order | System | Reads | Writes | Status |
|-------|--------|-------|--------|--------|
| 1 | `LifetimeSystem` | `Lifetime` | destroys entity (unregisters its collider) | ✓ Done |
| 2 | `InputMovementSystem` | `InputMovement`, InputManager | `MoveIntent` | ✓ Done (22H) |
| 3 | `PushOutSystem` | free movers' `Collider2D`, entered-overlap record | `Transform2D.Position` (out of colliders that moved into them) | ✓ Done (22I) |
| — | `AIMovementSystem` | `AIMovement` | `MoveIntent` | Deferred (Phase 30) |
| 4 | `FreeMovementSystem` | `FreeMovement`, `MoveIntent`? | `FreeMovement.Velocity` / `External`, `PendingMove` | ✓ Done |
| 5 | `SmoothTileMoveSystem` | `SmoothTileMovement`, `MoveIntent`?, `Collider2D`?, TileMap tile size | `Transform2D.Position` (plan-then-commit via `QueryTileBlocked`) | ✓ Done (22C) |
| 6 | `SnapTileMoveSystem` | `SnapTileMovement`, `MoveIntent`, `Collider2D`?, TileMap tile size | `Transform2D.Position` (plan-then-commit) | ✓ Done (22H) |
| 7 | `CollisionResolutionSystem` | `PendingMove`, `Collider2D`?, `Pushable`? | `Transform2D.Position` (swept clamp + slide, shoves Pushables) | ✓ Done (22B, 22I) |
| 8 | `ContactEventsSystem` | contacts from the movers + trigger overlaps | `TriggerCallback` / `CollisionCallback` calls | ✓ Done (22F) |
| 9 | `CameraFollowSystem` | `CameraTarget`, `Transform2D` | `Scene.Camera2D` | ✓ Done |
| 10 | `AnimatorStateMachineSystem` | `AnimatorStateMachine` | `Animator.Clip` | ✓ Done (Phase 17) |
| 11 | `AnimatorSystem` | `Animator`, `SpriteSheet` | `SpriteRenderer.UV` | ✓ Done (Phase 17) |
| 12 | `AudioSystem` | `AudioSource`, `Transform2D` | spatial sounds, listener | ✓ Done (Phase 18) |

`?` marks an optional component: the system reads it when present.

### Render-collect phase (variable rate, before SubmitFrame)

| Order | System | Reads | Writes | Status |
|-------|--------|-------|--------|--------|
| 1 | `SpriteRenderSystem` | `Transform2D`, `SpriteRenderer`, `Parallax`? | `FramePacket.Sprites` | ✓ Done |
| 2 | `LightRenderSystem` | `Transform2D`, `LightEmitter` | `FramePacket.Lights` | ✓ Done |

---

## Query API

### Basic query

Iterates all archetypes whose mask is a superset of the requested types. The
query object is stack-allocated and performs no heap allocation.

```cpp
world.Query<Transform2D, SpriteRenderer>()
     .Each([](Entity e, Transform2D& t, SpriteRenderer& s) { ... });
```

### Read-only access

Passing `const T` in the template signals read-only intent; the implementation
provides a `const T&` reference and may skip write-back in future SIMD paths.

```cpp
world.Query<const Transform2D, SpriteRenderer>()
     .Each([](const Transform2D& t, SpriteRenderer& s) { ... });
```

### Exclusion filter

```cpp
world.Query<Transform2D, FreeMovement>()
     .Without<Disabled>()
     .Each(...);
```

### Tag filter

A convenience wrapper that adds a `Tag.Value == tagString` predicate on top of
the component query. This is a linear scan over the Tag column of each matching
archetype; use it for low-frequency queries (setup, events) rather than
per-frame inner loops.

```cpp
world.Query<Transform2D>()
     .WithTag("Enemy")
     .Each([](Entity e, Transform2D& t) { ... });
```

For high-frequency filtering (e.g. "all active enemies each tick"), prefer a
dedicated marker component (`struct EnemyMarker {}`) over the string tag. Marker
components create a distinct archetype that the query can match directly via
bitmask with zero per-entity branching.

---

## Integration with Application

`Application::Run()` drives two phases per loop iteration:

```
[fixed tick]
  Application::OnUpdate(dt)
  SceneManager::Update(dt)        — fades, and a pending swap
  Scene::Update(dt)               — the active scene
    → Scene::OnUpdate(dt), the camera, then the World's update systems in order
  UIManager::Update(dt)

[variable tick]
  FramePacket packet = ...
  Application::OnRender(packet)
  Scene::CollectRenderData(packet)
    → Scene::OnRender(packet), then the World's render systems in order
  scene fade quad, UI
  RenderThread::SubmitFrame(packet)
```

The app-level `OnUpdate(dt)` and `OnRender(packet)` hooks run **before** the
scene, for global work (input actions, debug overlays); per-scene logic goes in
the scene's own hooks. While a game-blocking UI screen is up (the splash), the
game and scene steps are skipped. Only the top scene of the stack is updated
and drawn.

---

## Scene Files

Scenes are saved as `.arcscene` JSON: entities keyed by a file id, with each
component inline under its registered name. The format, merge-safety rules and
the component registry are in [scenes.md](scenes.md#scene-file-format-arcscene).
The file id is an editor and serialization concern only: the runtime ECS uses
generational `Entity` handles, and `FileScene` keeps an id -> `Entity` table
(`FindEntity`) for scripts and entry points.

---

## File Layout

```
engine/include/arcbit/ecs/
    Entity.h      — Entity handle
    World.h       — World, archetype internals, ComponentTypeID, OnRemove hooks
    Query.h       — Query<Ts...> template, WithTag(), Without<T>()
    System.h      — SystemRegistry, RegisterSystem / RegisterRenderSystem
    Components.h  — Transform2D, SpriteRenderer, LightEmitter, Parallax,
                    CameraTarget, Disabled, Lifetime, Name, Tag
    Movement.h    — MoveIntent, InputMovement, AIMovement, FreeMovement,
                    SmoothTileMovement, SnapTileMovement, PendingMove

engine/include/arcbit/scene/
    Scene.h          — Scene class (owns World, Camera2D, WorldConfig, TileMap, PhysicsWorld)
    WorldConfig.h    — WorldConfig struct
    SceneManager.h   — scene stack, switching, fades, loading ahead (scenes.md)
    ScenePreloader.h — what OnPrepare names for background decoding; load progress
    FileScene.h      — a scene built from an .arcscene file
    SceneFile.h      — .arcscene load / save
    SceneRegistry.h  — component name <-> type <-> JSON, named systems
    SceneAssets.h    — asset path <-> handle for scene files

engine/src/
    World.cpp     — World implementation (archetype management, entity table)
    Systems.cpp   — built-in system implementations (all systems listed above)
    Movement.cpp  — ResolveInputDirection (input → direction, tie-break)
    Scene.cpp     — Scene::Update, Scene::CollectRenderData
    SceneManager.cpp, ScenePreloader.cpp, FileScene.cpp, SceneFile.cpp,
    SceneRegistry.cpp, SceneComponents.cpp (built-in component serializers)
```

---

## Migration: Current Demo → ECS

The current `main.cpp` demo manually fills `_sprites` and `_lights` vectors and
culls them by hand in `OnRender`. After this phase it migrates to:

1. ✓ Create entities with `Transform2D + SpriteRenderer` for each sprite.
2. ✓ Create entities with `Transform2D + LightEmitter` for each light source.
3. ✓ WASD movement goes through `InputMovement` + `MoveIntent` (Phase 22H).
4. ✓ Delete `_sprites`, `_lights`, and the manual cull loops — `SpriteRenderSystem`
   and `LightRenderSystem` handle everything.
5. ✓ The `Camera2D` moves from being a game-owned member to `scene.GetCamera()`.

The demo can migrate incrementally: manual sprites and ECS sprites both feed
into `FramePacket.Sprites` and are rendered identically.

---

## Entity Templates

An **Entity Template** (also called a Component Group in editor parlance) is a
named, serialized description of an entity's initial component set and default
values. It is the ECS equivalent of a prefab — a reusable starting point for
creating entities with a known configuration.

### Why "Entity Template" not "Component Group"

"Component group" risks confusion with ECS query filters (`.Query<A, B, C>()` is
already a kind of component group). "Entity Template" is unambiguous: it is a
template for an entity, not a filter over components.

### Two uses

**Editor** — the "New Entity" workflow shows a template picker. Selecting
`"Guard NPC"` creates an entity pre-populated with `SpriteRenderer`, `Animator`,
`AIBehavior`, and `ScriptComponent(guard.lua)` rather than requiring the designer
to add and configure each component manually.

**Runtime** — `World::SpawnFromTemplate(name, position)` lets game code spawn
entities without hard-coding component setup inline. Enemy wave spawners,
projectile factories, and loot drops all benefit from this.

### What a template is not

Applying a template is a **one-time stamp** — there is no live link back to the
template definition afterward. Changing the template does not retroactively
update existing entities. This is simpler than Unity's prefab override system and
is the right tradeoff for Phase 19: the editor can batch-update entities by query
if needed, and a live-link system can be added later without breaking the format.

### Definition format

Templates are stored in a `templates.json` file in the project directory
(referenced by `project.arcbit`). Each template lists component types and their
serialized initial values:

```json
{
    "version": "1.0",
    "templates": [
        {
            "name": "Guard NPC",
            "tag": "NPC",
            "components": [
                {
                    "type": "SpriteRenderer",
                    "texture": "assets/textures/guard.png",
                    "layer": 0
                },
                {
                    "type": "Animator",
                    "sheet": "assets/spritesheets/guard.json",
                    "default_clip": "idle_down"
                },
                {
                    "type": "AIBehavior",
                    "behavior": "patrol",
                    "alert_radius": 128.0
                },
                {
                    "type": "ScriptComponent",
                    "script": "scripts/guard.lua"
                }
            ]
        },
        {
            "name": "Chest",
            "tag": "Interactive",
            "components": [
                {
                    "type": "SpriteRenderer",
                    "texture": "assets/textures/chest.png",
                    "layer": 0
                },
                {
                    "type": "Interactable",
                    "on_interact": "scripts/open_chest.lua"
                }
            ]
        }
    ]
}
```

### Runtime API

```cpp
// Spawn an entity from a template at the given world position.
// Returns Entity::Invalid() if the template name is not found.
Entity World::SpawnFromTemplate(std::string_view templateName, Vec2 position);

// Register templates at startup (called by Application after OnStart).
void World::LoadTemplates(std::string_view path);
```

`SpawnFromTemplate` creates the entity, sets `Transform2D.Position`, and applies
each component listed in the template with its default values. Game code then
modifies the entity normally — the template has no further influence.

```cpp
// Spawn a guard at a patrol waypoint:
Entity guard = world.SpawnFromTemplate("Guard NPC", waypointPos);
world.GetComponent<AIBehavior>(guard)->PatrolPath = myPath;
```

### Editor workflow

1. Open **Templates** panel — lists all templates in `templates.json`.
2. Drag a template onto the scene viewport — snaps to tile grid, creates entity
   at that position with all listed components.
3. Select an existing entity — "Save as Template" button extracts its current
   component set into a new template entry (useful for capturing a configured NPC
   you want to reuse).
4. Edit a template's default values — changes affect new spawns only (no
   retroactive update, by design).

### Template inheritance (deferred)

A future extension can add `"extends": "Base NPC"` so templates compose rather
than copy. Deferred until Phase 26+ when the scripting layer makes cross-template
component sharing practical.

---

## Out of Scope for This Phase

- Lua scripting of entities (Phase 26)
- Physics integration — `Rigidbody2D`, `Collider2D` (Phase 22)
- Animation system — `Animator` component, clip playback (Phase 17)
- `AudioSource` component (Phase 18)
- Editor integration — entity inspector, UUID generation, scene file authoring (Phase 40)
