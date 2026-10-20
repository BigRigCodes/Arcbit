# Entities, Components & Systems

Every game object in Arcbit is an entity in a scene's `World`: a handle with
components (plain data) that systems (functions run each tick or frame)
query. This covers the data model, the built-in components, the system order
and the query API.

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
This caps a program at **64 distinct component types** - the engine uses
about 30, leaving room for a game's own. Widening the mask is a contained
change if a game ever needs more.

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

**Add and destroy hooks.** `World::OnAdd<T>(callback)` runs just after a `T`
is added; `World::OnDestroy(callback)` runs when an entity is destroyed (not
when a component is merely removed) - what `EntityEvents` is built on. Like
removal hooks, they must not add, remove or destroy anything while running.

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
    f32  TileSize = 32.0f;           // world pixels per tile (used by tile movement systems)
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

An entity from a scene file is already identified by its file id
(`FileScene::FindEntity`, Lua `Entity.Find`). `Name` is for entities with
game-design identity that scripts or other code look up by name: the player,
named NPCs, quest objects.

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

Add only when the entity needs a stable human-readable identity.

### SpriteRenderer *(optional)*

Visual representation without duplicating position (Transform2D owns that).

```cpp
struct SpriteRenderer {
    TextureHandle Texture;
    SamplerHandle Sampler;
    UVRect        UV    = { 0.0f, 0.0f, 1.0f, 1.0f };
    Vec2          Pivot = { 0.5f, 0.5f }; // normalized origin; the Animator writes it from frame data
    Color         Tint  = Color::White();
    i32           Layer = 0;
    bool          FlipX = false;
};
```

`SpriteRenderSystem` reads `(Transform2D, SpriteRenderer)` and pushes a `Sprite`
into `FramePacket.Sprites`. Frustum culling is applied at this point. If the
entity also has a `Parallax` component the position is adjusted before submission
(see `Parallax` below).

UV rectangles come from `SpriteSheet`, which loads the Arcbit sprite format
(`docs/sprite-format.md`): named frames, tile grids and animation clips in one
file. An `Animator` writes `UV` and `Pivot` from its clip; without one, set
them from `SpriteSheet::GetTile` / `GetSprite`.

### LightEmitter *(optional)*

Light properties without position (Transform2D owns that).

```cpp
struct LightEmitter {
    f32   Radius       = 200.0f;
    f32   Intensity    = 1.0f;
    Color LightColor   = Color::NaturalLight();
    bool  CastsShadows = false; // solid tiles with BlocksLight shadow it (a 1D shadow map per light)
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

### EntityEvents *(optional)*

Opts the entity into announcing itself on the event bus: spawned and
destroyed, each renameable per entity (`"boss.defeated"`). Entities announce
nothing without it. See [events.md](events.md#entity-events).

```cpp
struct EntityEvents {
    bool        Spawned   = true;
    bool        Destroyed = true;
    std::string SpawnedEvent;   // empty = engine.entity.spawned
    std::string DestroyedEvent; // empty = engine.entity.destroyed
};
```

### Animator, AnimatorStateMachine, FacingAnimation *(optional)*

`Animator` plays a clip from a `SpriteSheet`: `AnimatorSystem` advances its
frames, writes `SpriteRenderer.UV` and `Pivot`, and calls `OnEvent` for frame
events (`"events": ["FootStep"]` in the sheet). Which clip plays is up to game
code, or:

- `AnimatorStateMachine` - named states, float / bool / trigger parameters and
  conditional transitions, for real animation logic (attacks, hurt, death).
- `FacingAnimation` - a four-way character's idle / walk clip from its
  `MoveIntent` facing and whether it's moving (`"walk_{facing}"` plays
  `walk_left`), optionally drawing left as right flipped. Most NPCs and players
  need nothing more.

```cpp
struct FacingAnimation {
    std::string Idle       = "idle_{facing}";
    std::string Walk       = "walk_{facing}";
    bool        MirrorLeft = false; // left plays the right clip, flipped
};
```

### AudioSource *(optional)*

A looping or one-shot sound at the entity, attenuated by its distance from the
listener (the camera). `AudioSystem` starts, positions and stops it; setting
`Playing = false` releases it.

### Collider2D and its callbacks *(optional)*

Collision shape, layer / mask, directional arcs and the trigger flag, plus
`TriggerCallback` / `CollisionCallback` for contact events and `Pushable` for
things that get shoved: see [physics.md](physics.md).

### Interactor / Interactable *(optional)*

The player's `Interactor` uses the `Interactable` in front of it when its
action is pressed: an `Interactable` with a `Dialog` starts that conversation,
the target's script hears `OnInteract`, and `engine.entity.interacted` is
published. See [scripting.md](scripting.md) and [dialog.md](dialog.md).

### Script, Persistent *(optional)*

`Script` gives an entity Lua behavior ([scripting.md](scripting.md));
`Persistent` saves its changes with the game ([saves.md](saves.md)).

### Held *(runtime only)*

Something has the entity's attention - a conversation holds the player and the
NPC they talk to. `HoldSystem` clears a held entity's `MoveIntent` direction
after every controller and script has written it, so it stands still (a tile
mover finishes the step it's on); its facing is kept. Holds nest (`Hold` /
`Release` in `Movement.h`); scripts check `entity:IsHeld()`. Never saved.

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
polling input. Code and cutscenes can add one and call `Set` to drive any
style without a controller; scripts use `entity:Move(direction)`. Style systems reduce a non-cardinal intent to its
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

Reserved for the pathfinding system (Phase 30), which will turn
`TargetPosition` into `MoveIntent` the way `InputMovement` does, so the style
layer is shared. Nothing reads it yet.

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

`External` is movement from the world: knockback, a bumper, a
conveyor. `AddImpulse` adds to it; it moves the entity on top of `Velocity`
and always decays with `Friction`, so it works on intent-driven movers too
(writing an impulse to `Velocity` would be overwritten next tick). A burst of
speed `v` travels about `v / Friction` pixels.

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
    f32  TurnDelay = -1.0f;   // tap to turn (below); < 0 = the project default, 0 = off
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
    f32 TurnDelay    = -1.0f; // tap to turn (below); < 0 = the project default, 0 = off
};
```

#### Tap to turn

Both tile styles: a mover standing still, pressed a direction it isn't
facing, turns at once (`MoveIntent.Facing`, so `FacingAnimation` shows it) and
only walks if the direction is still held after `TurnDelay` seconds - a quick
tap just turns, like facing an NPC to talk. The faced direction walks at once,
and so does any direction while already walking. It's on by default:
`ApplicationConfig::Defaults.TileTurnDelay` (0.1 s, `ProjectDefaults.h`) is
copied into every scene's `WorldConfig::TileTurnDelay`; a mover's own
`TurnDelay` (`"turn_delay"`) overrides it, 0 turning it off. Scenes made
without an Application (tests, tools) default to 0.

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
// Update system - runs every fixed tick
scene.GetWorld().RegisterSystem("Spin", [](Scene& scene, f32 dt) {
    scene.GetWorld().Query<Transform2D, const Spinner>()
        .Without<Disabled>()
        .ForEach([dt](Transform2D& t, const Spinner& s) { t.Rotation += s.Speed * dt; });
});

// Render system - runs every frame, adds to the FramePacket
scene.GetWorld().RegisterRenderSystem("Markers", [](Scene& scene, FramePacket& packet) {
    scene.GetWorld().Query<const Transform2D, const Marker>()
        .ForEach([&](Entity e, const Transform2D& t, const Marker& m) { /* push sprites */ });
});
```

Systems run in registration order. The engine registers its built-in systems
when a scene is created; a scene registers its own in `OnLoad` (or a scene file
lists named systems, [scenes.md](scenes.md#named-systems)), and they run after.

---

## Built-in System Execution Order

### Update phase (fixed timestep)

| # | System | Reads | Writes |
|---|--------|-------|--------|
| 1 | `Lifetime` | `Lifetime` | destroys the entity |
| 2 | `InputMovement` | `InputMovement`, InputManager | `MoveIntent` |
| 3 | `Hold` | `Held` | `MoveIntent.Direction` (cleared) |
| 4 | `PushOut` | free movers' `Collider2D`, entered-overlap record | `Transform2D.Position` (out of colliders that moved into them) |
| 5 | `FreeMovement` | `FreeMovement`, `MoveIntent`? | `FreeMovement.Velocity` / `External`, `PendingMove` |
| 6 | `SmoothTileMove` | `SmoothTileMovement`, `MoveIntent`?, `Collider2D`?, TileMap tile size | `Transform2D.Position` (plan-then-commit via `QueryTileBlocked`) |
| 7 | `SnapTileMove` | `SnapTileMovement`, `MoveIntent`, `Collider2D`?, TileMap tile size | `Transform2D.Position` (plan-then-commit) |
| 8 | `CollisionResolution` | `PendingMove`, `Collider2D`?, `Pushable`? | `Transform2D.Position` (swept clamp + slide, shoves Pushables) |
| 9 | `ContactEvents` | contacts from the movers + trigger overlaps | `TriggerCallback` / `CollisionCallback` calls |
| 10 | `CameraFollow` | `CameraTarget`, `Transform2D` | the scene's `Camera2D` |
| 11 | `FacingAnimation` | `FacingAnimation`, `MoveIntent`, tile step / velocity | `Animator.Clip`, `SpriteRenderer.FlipX` |
| 12 | `AnimatorStateMachine` | `AnimatorStateMachine` | `Animator.Clip` |
| 13 | `Animator` | `Animator`, `SpriteSheet` | `SpriteRenderer.UV` / `Pivot`, frame events |
| 14 | `Audio` | `AudioSource`, `Transform2D` | spatial sounds, the listener |

`?` marks an optional component: the system reads it when present. An
`AIMovement` system joins after `InputMovement` in Phase 30.

### Render-collect phase (every frame, before SubmitFrame)

| # | System | Reads | Writes |
|---|--------|-------|--------|
| 1 | `TilemapRender` | the scene's `TileMap`, camera | `FramePacket.Sprites` (visible chunks) |
| 2 | `SpriteRender` | `Transform2D`, `SpriteRenderer`, `Parallax`? | `FramePacket.Sprites` |
| 3 | `LightRender` | `Transform2D`, `LightEmitter` | `FramePacket.Lights`, shadow maps |

---

## Query API

### Basic query

Iterates all archetypes whose mask is a superset of the requested types. The
query object is stack-allocated and performs no heap allocation.

```cpp
world.Query<Transform2D, SpriteRenderer>()
     .ForEach([](Entity e, Transform2D& t, SpriteRenderer& s) { ... });
```

The callback takes the components in order, optionally after the `Entity`.
Inside it, don't create or destroy entities or add / remove components (it
asserts): collect them and change the world after `ForEach` returns. Reading
and writing component values, `GetComponent` and nested queries are fine.

### Read-only access

Passing `const T` hands the callback a `const T&`:

```cpp
world.Query<const Transform2D, SpriteRenderer>()
     .ForEach([](const Transform2D& t, SpriteRenderer& s) { ... });
```

### Exclusion filter

```cpp
world.Query<Transform2D, FreeMovement>()
     .Without<Disabled>()
     .ForEach(...);
```

### Tag filter

A convenience wrapper that adds a `Tag.Value == tagString` predicate on top of
the component query. This is a linear scan over the Tag column of each matching
archetype; use it for low-frequency queries (setup, events) rather than
per-frame inner loops.

```cpp
world.Query<Transform2D>()
     .WithTag("Enemy")
     .ForEach([](Entity e, Transform2D& t) { ... });
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
  input edges, a waiting conversation starts (docs/dialog.md)
  Application::OnUpdate(dt)
  SceneManager::Update(dt)        - fades, and a pending swap
  Scene::Update(dt)               - the active scene:
    Scene::OnUpdate, interaction (Interactor), scripts' OnUpdate,
    the camera, then the World's update systems in order
  EventBus::Flush                 - this tick's events delivered
  UIManager::Update(dt)

[every frame]
  FramePacket packet = ...
  Application::OnRender(packet)
  Scene::CollectRenderData(packet)
    -> Scene::OnRender(packet), then the World's render systems in order
  scene fade quad, UI
  RenderThread::SubmitFrame(packet)
```

The app-level `OnUpdate(dt)` and `OnRender(packet)` hooks run **before** the
scene, for global work (input actions, debug overlays); per-scene logic goes in
the scene's own hooks. While a game-blocking UI screen is up (the splash), the
game and scene steps are skipped; a conversation with `"world": "pause"` skips
the scene update. Only the top scene of the stack is updated and drawn.

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
    Entity.h               - the Entity handle
    World.h                - World, queries (QueryView: ForEach, Without, WithTag),
                             systems (RegisterSystem / RegisterRenderSystem),
                             OnAdd / OnRemove / OnDestroy hooks
    Components.h           - Transform2D, Tag, Name, SpriteRenderer, LightEmitter,
                             Parallax, CameraTarget, Disabled, Lifetime, Animator,
                             FacingAnimation, AudioSource, EntityEvents
    Movement.h             - MoveIntent, InputMovement, AIMovement, FreeMovement,
                             SmoothTileMovement, SnapTileMovement, Pushable,
                             PendingMove, Held
    AnimatorStateMachine.h - AnimatorStateMachine
    Interaction.h          - Interactor, Interactable, FindInteractTarget

engine/include/arcbit/scene/
    Scene.h          - Scene (owns World, Camera2D, WorldConfig, TileMap, PhysicsWorld)
    WorldConfig.h    - WorldConfig
    SceneManager.h   - scene stack, switching, fades, loading ahead (scenes.md)
    ScenePreloader.h - what OnPrepare names for background decoding; load progress
    FileScene.h      - a scene built from an .arcscene file
    SceneFile.h      - .arcscene load / save
    SceneRegistry.h  - component name <-> type <-> JSON, named systems
    SceneAssets.h    - asset path <-> handle for scene files

engine/src/
    World.cpp      - archetypes, the entity table
    Systems.cpp    - the built-in systems listed above
    Movement.cpp   - ResolveInputDirection (input -> direction, tie-break), Hold / Release
    Interaction.cpp, AnimatorStateMachine.cpp
    Scene.cpp      - Scene::Update, Scene::CollectRenderData
    SceneManager.cpp, ScenePreloader.cpp, FileScene.cpp, SceneFile.cpp,
    SceneRegistry.cpp, SceneComponents.cpp (built-in component serializers)
```
