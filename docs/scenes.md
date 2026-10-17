# Scenes

How scenes are built, switched, and stored. A scene is either a C++ `Scene`
subclass or a data-driven `FileScene` loaded from an `.arcscene` file; the
`SceneManager` treats both the same. Games made with the editor (Phase 40)
are all scene files.

- [Lifecycle and switching](#lifecycle-and-switching)
- [Scene file format (`.arcscene`)](#scene-file-format-arcscene)
- [Component registry](#component-registry)
- [Named systems](#named-systems)
- [Assets and resource ownership](#assets-and-resource-ownership)

---

## Lifecycle and switching

```cpp
class MyScene : public Scene {
    void OnLoad() override;     // build entities, LoadTexture, register systems
    void OnUnload() override;   // destroy what OnLoad made directly on the device
    void OnUpdate(f32 dt) override;
    void OnRender(FramePacket& packet) override;
};

app.GetScenes().Register("Village", [] { return std::make_unique<MyScene>(); });
app.GetScenes().Register("Cave", [] { return std::make_unique<FileScene>("assets/scenes/cave.arcscene"); });
app.GetScenes().Switch("Village", "north_gate"); // queued; fade out, swap, fade in
```

The swap runs while the render thread is idle, so `OnLoad` / `OnUnload` may
create and destroy GPU resources. Order: old `OnUnload`, new `OnLoad`, old
scene's resources released, old scene destroyed. Scene teardown destroys every
entity, so component removal hooks release outside resources (colliders,
sounds).

---

## Scene file format (`.arcscene`)

One JSON document per scene. It references the tilemap (an `.arcmap`, see
[tilemap.md](tilemap.md#map-file-arcmap)) and embeds the entities.

```json
{
    "version": 1,
    "config":  { "tile_size": 64 },
    "camera":  { "zoom": 1.5 },
    "render":  { "ambient": [1, 1, 1, 1] },
    "physics": { "debug_draw": true },
    "tilemap": "assets/maps/physics.arcmap",
    "tile_overrides": [ { "id": 409, "solid": true } ],
    "systems": [ "DemoBumpers" ],
    "entities": {
        "player": {
            "Tag":            { "value": "Player" },
            "Transform2D":    { "position": [320, 0], "scale": [44.8, 44.8] },
            "SpriteRenderer": { "texture": "builtin:white", "sampler": "nearest", "tint": [0.3, 0.5, 1, 1] },
            "Collider2D":     { "shape": "circle", "radius": 22.4, "layer": "Player" },
            "FreeMovement":   { "speed": 240 },
            "PendingMove":    {},
            "InputMovement":  { "move_left": "Move_Left", "move_right": "Move_Right",
                                "move_up": "Move_Up", "move_down": "Move_Down" },
            "CameraTarget":   { "lag": 0.05 }
        }
    }
}
```

| Key | Meaning |
|-----|---------|
| `version` | Format version; currently `1`. Newer files are rejected. |
| `config.tile_size` | `WorldConfig.TileSize` and the tilemap's tile size. Set before anything else, because the physics broadphase is sized from it. |
| `camera` | `zoom`, `position` [x, y]. |
| `render.ambient` | Ambient light color (RGBA), applied every frame. |
| `physics.debug_draw` | Start with collider outlines on. |
| `tilemap` | Path of an `.arcmap`. Optional. |
| `tile_overrides` | TileDefs applied after the map loads, one per tile id, using the `.tileatlas.json` tile keys (`solid`, `blocks_light`, `flat`, `layer`, `blocked_from`, `uv_scroll`). Flip-book `animation` belongs in the atlas file. |
| `systems` | [Named systems](#named-systems) the scene needs, installed in order at load. |
| `entities` | Entity id → components. |

### Entities

- **Ids** are strings unique within the file. The editor generates UUIDs;
  hand-written files can use readable names. The runtime uses fast
  generational `Entity` handles; ids exist only in the file and in the
  `FileScene`'s id table (`FindEntity("player")`), which scripts and entry
  points use.
- **Components** are keyed by their registered name (the C++ type name), one
  of each type per entity, as in the ECS. Field keys are snake_case, like the
  other engine formats.
- **Only non-default fields are written.** A missing field keeps the
  component's default, so files stay short and adding a field to a component
  doesn't rewrite every scene. An empty object (`"PendingMove": {}`) adds the
  component with all defaults.
- **Runtime state isn't stored**: tile-step progress, input tie-break state,
  sound handles, external velocity, callbacks. A scene file is a starting
  state, not a save game (Phase 25).
- Entities with a `Collider2D` are registered with the scene's physics world
  after all their components are in place.
- **Unknown component names are skipped** with a warning and the rest of the
  entity loads, so a file from a newer build still opens. Anything else
  malformed (a wrong type, an unknown layer name, an unregistered system, a
  newer `version`) fails the load.

**Merge safety.** The saver writes entity ids in sorted order. Two people
adding entities on different branches insert keys at unrelated positions, so
git merges them cleanly; a conflict means both edited the *same* entity's
values, which is a real conflict.

### Not in v1

- **Prefabs (templates and overrides)**: an entity that starts from a shared
  definition and overrides some fields - the Entity Templates design in
  [ecs.md](ecs.md#entity-templates). The format reserves `"template"` and
  `"overrides"` keys on an entity for this.
- **Entity references between entities**: e.g. a door that names its target
  by id. Ids are in place for it; no component needs it yet.
- **Callbacks** (`TriggerCallback`, `CollisionCallback`): behavior comes from
  named systems now and Lua scripts in Phase 26.

---

## Component registry

`SceneRegistry` (`app.GetSceneRegistry()`) maps a component name to its C++
type and its JSON read and write functions. The engine registers its built-in
components; a game adds its own the same way, in `OnStart`:

```cpp
registry.RegisterComponent<Bumper>("Bumper",
    [](Bumper& c, const nlohmann::json& j, SceneAssets&) { c.Kick = j.value("kick", c.Kick); },
    [](const Bumper& c, nlohmann::json& j, const SceneAssets&) { j["kick"] = c.Kick; });
```

The writer always writes every field; the registry drops fields whose value
matches a default-constructed component, so writers stay simple.

Built-in components: `Tag`, `Name`, `Transform2D`, `SpriteRenderer`,
`LightEmitter`, `Parallax`, `CameraTarget`, `Disabled`, `Lifetime`,
`AudioSource`, `Animator`, `Collider2D`, `MoveIntent`, `InputMovement`,
`AIMovement`, `FreeMovement`, `SmoothTileMovement`, `SnapTileMovement`,
`Pushable`, `PendingMove`. `AnimatorStateMachine` and the callback components
aren't serializable yet.

Collision layers are written by name (`"layer": "Prop"`,
`"mask": ["Player", "Wall"]`, `"mask": "all"`); game-defined bits are written
as numbers.

---

## Named systems

A scene file can't hold C++ code, so behavior a game adds is registered under a
name and listed in the file:

```cpp
registry.RegisterSystem("DemoBumpers", [](Scene& scene) {
    scene.GetWorld().RegisterSystem("DemoBumpers", [](Scene& s, f32 dt) { /* ... */ });
});
```

The installer runs once when the scene loads, before its entities are created,
and may register update or render systems on the scene's world. An unknown
name is a load error. Phase 26 adds Lua scripts alongside named systems.

**Example**: `game/assets/scenes/physics.arcscene` is the Physics demo as a
scene file. Its bumper is a game component (`DemoBumper`) plus a named system
(`DemoBumpers`) that gives each bumper its knockback callback; F2 is another
named system (`DemoDebugKeys`). The demo menu lists it as "Physics File".

---

## Assets and resource ownership

Scene files reference assets by path:

- **Textures**: `"texture": "assets/textures/chest.png"`, or `"builtin:white"`
  for the engine's 1×1 white texture (tinted boxes, debug shapes).
- **Samplers**: named presets, `"nearest"` (point filtering, repeat) and
  `"linear"` (bilinear, clamp), owned by the application and shared by every
  scene.
- **Sprite sheets** (`Animator.sheet`): path of the sheet's `.json`.
- **Audio**: the file path, as `AudioSource` already stores it.

Textures and sprite sheets a scene loads are owned by that scene and
reference-counted: they are released when it unloads, and a texture another
scene still uses stays loaded. `.arcmap` atlas textures are still loaded
app-wide (not yet scene-owned).

Loading and saving go through the `SceneAssets` interface (path ↔ handle),
which `FileScene` implements on top of the application. Tests implement it
without a GPU, so the format is round-trip tested headless.
