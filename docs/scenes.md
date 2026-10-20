# Scenes

How scenes are built, switched, and stored. A scene is either a C++ `Scene`
subclass or a data-driven `FileScene` loaded from an `.arcscene` file; the
`SceneManager` treats both the same. Games made with the editor (Phase 40)
are all scene files.

- [Lifecycle and switching](#lifecycle-and-switching)
  - [Scene stack](#scene-stack), [Swap rules](#swap-rules),
    [Loading ahead](#loading-ahead), [Load progress](#load-progress),
    [Music](#music)
- [Boot flow](#boot-flow)
- [Scene file format (`.arcscene`)](#scene-file-format-arcscene)
- [Component registry](#component-registry)
- [Named systems](#named-systems)
- [Assets and resource ownership](#assets-and-resource-ownership)

---

## Lifecycle and switching

```cpp
class MyScene : public Scene {
    void OnPrepare(ScenePreloader& preload) override; // loader thread: read files, decode images
    void OnLoad() override;     // build entities, LoadTexture, register systems
    void OnUnload() override;   // destroy what OnLoad made directly on the device
    void OnUpdate(f32 dt) override;
    void OnRender(FramePacket& packet) override;
};

app.GetScenes().Register("Village", [] { return std::make_unique<MyScene>(); });
app.GetScenes().Register("Cave", [] { return std::make_unique<FileScene>("assets/scenes/cave.arcscene"); });
app.GetScenes().Switch("Village", "north_gate"); // queued; fade out, swap, fade in
```

A scene's life, in order:

| Hook | When | Thread |
|------|------|--------|
| factory | the switch / push / preload is requested | main |
| `OnPrepare` | right after - read files, name images to decode | loader |
| `OnLoad` | at the swap - build entities, `LoadTexture`, systems | main (render thread idle) |
| `OnUpdate` / `OnRender` | every tick / frame while it's the top scene | main |
| `OnSuspend` / `OnResume` | another scene is pushed over it / popped off | main |
| `OnUnload` | at the swap that removes it - free what `OnLoad` made directly | main (render thread idle) |

A scene listens on the event bus with `Scene::Subscribe(event, handler)`; the
subscription ends with its unload, and a suspended scene keeps listening (see
[events.md](events.md#subscribing)). Scene changes are themselves announced
as `engine.scene.*` events.

Every hook is optional. A scene is never moved (systems capture it), and
nothing outside `OnPrepare` runs off the main thread.

### Scene stack

```cpp
app.GetScenes().Push("Battle", "arena"); // suspend the active scene, load Battle on top
app.GetScenes().Pop();                   // unload Battle, resume the scene below
```

`Push` suspends the active scene rather than unloading it: `OnSuspend` runs,
it stops being updated or drawn, and its sounds stop (they restart on resume),
but its entities, resources and state stay as they are. `Pop` unloads the top
scene and calls `OnResume` on the one below. Use it for a scene you come back
from with the world as you left it, e.g. a battle over the overworld.
`Switch` replaces the whole stack, top first. All three fade the same way,
and a request made while another is queued replaces it.

Only the top scene is drawn. A frame has one camera, so letting a scene show
through the one above it needs offscreen render targets (Phase 34). Overlays
that keep the world visible (inventory, pause menu) are UI screens.

The demo app shows it: B pushes `assets/scenes/battle.arcscene` over any demo
and B again pops back to the demo exactly as it was left.

### Swap rules

The swap runs while the render thread is idle, so `OnLoad` / `OnUnload` may
create and destroy GPU resources. Order: old `OnUnload`, new `OnLoad`, old
scene's resources released, old scene destroyed. Scene teardown destroys every
entity, so component removal hooks release outside resources (colliders,
sounds).

### Loading ahead

A switch or push creates the next scene at once and runs its `OnPrepare` on a
loader thread. `OnPrepare` reads and parses files into the scene's own members and names the
assets `OnLoad` will use, so their images decode there:

```cpp
void OnPrepare(ScenePreloader& preload) override
{
    preload.AddTileMap("assets/maps/village.arcmap");      // every atlas image
    preload.AddSpriteSheet("assets/spritesheets/player.json");
    preload.AddTexture("assets/textures/chest.png");
}
```

Just before `OnLoad`, the decoded images are handed to the `TextureManager`,
so `OnLoad`'s `LoadTexture` / `LoadSpriteSheet` / `TileMap::LoadMap` calls
only upload them. Images already on the GPU are skipped. `OnPrepare` must
touch nothing outside its scene object: no World, `GetApp()`, GPU, UI or
audio - those stay in `OnLoad`, which runs on the main thread.

The swap waits until `OnPrepare` is done (`SceneManager::IsLoading`). What
the player sees meanwhile is up to the change (`SceneChange`):

```cpp
scenes.Switch("House", "front_door");                          // fade first (default)
scenes.Switch("Cave", SceneChange{ .WaitForLoad = true });     // keep playing until it's ready
scenes.Switch("Shop", SceneChange{ .ShowLoadingScreen = false }); // fade first, plain black
```

- **Fade first** (the default) - for doors. The fade-out starts at once and
  the load runs under it. If the load outlasts the fade, the screen holds at
  black, and after a short grace period (so quick loads don't flash it)
  Application shows its loading screen, which fades out as the new scene
  fades in. The engine's `LoadingScreen` shows "Loading" with animated dots
  and a progress bar; `Application::SetLoadingScreen(factory)` swaps in any
  `UIScreen` (leave `BlocksGame` off; read `GetScenes().GetLoadProgress()`
  for a bar), or null for plain black.
- **`WaitForLoad`** - for menu jumps, or a game that keeps playing while it
  loads where you're going. The current scene stays on screen, updating and
  playable (the player is held still only during the fades,
  `SceneManager::IsFading`), and the fade-out starts once the next scene is
  ready.

Either way the splash and fades never freeze. `FileScene` prepares itself: it
parses the `.arcscene` and decodes the tilemap's atlases and every `texture` /
`sheet` component field.

`Preload(name)` starts that work early, with the current scene still running
(e.g. as the player nears a door), and the later `Switch` / `Push` to that
name finds it done. One scene is prepared at a time; preloading or switching
to another name discards it.

The demo app shows all three: PgUp / PgDn switch with `WaitForLoad`, the F1
menu fades first (with the loading screen on a long load), and the battle is
preloaded while a demo runs, so B is instant. `arcbit-game --load-delay
<seconds>` (`SceneManager::PrepareDelay`) slows every prepare down so the
difference is visible.

### Load progress

`SceneManager::GetLoadProgress()` (0..1) is how far the queued scene's load
is. A load runs in three stages, each a share of the bar:

| Stage     | What runs                                    | Default share |
|-----------|----------------------------------------------|---------------|
| `prepare` | `OnPrepare` itself (loader thread)           | 0%            |
| `decode`  | the images it named, weighted by file size   | 95%           |
| `build`   | `OnLoad` (main thread)                       | 5%            |

`OnPrepare` only *names* images; the loader decodes them after it returns,
so the total is known before the first decode, and a big texture moves the
bar more than a small one. Nothing redraws while `OnLoad` runs, so the bar
waits at the start of the build share until the scene is in.

A scene doing heavy work of its own in `OnPrepare` (generating a world)
claims a share and reports through it:

```cpp
void OnPrepare(ScenePreloader& preload) override
{
    preload.SetShares(0.4f, 0.5f, 0.1f); // prepare, decode, build (normalized)
    for (int i = 0; i < chunks; ++i) {
        GenerateChunk(i);
        preload.ReportPrepareProgress(f32(i + 1) / chunks);
    }
    preload.AddTileMap("assets/maps/generated_base.arcmap");
}
```

Scene files can carry measured shares in a `"load"` block (below), so the bar
also covers what file sizes can't: `OnLoad`'s build time and per-scene
overhead.

### Music

A scene can own its music. `SetMusic` (usually in `OnLoad`; `"music"` in a
scene file) names the track that plays whenever the scene is active:

```cpp
void OnLoad() override
{
    SetMusic("assets/music/village.ogg");    // loops by default
}
void OnBossAppears() { SetMusic("assets/music/boss.ogg"); } // active: changes at once
```

At each swap the music follows the new active scene, crossfading over
`SceneManager::MusicFadeTime` (0.6 s, from the swap):

- A scene with its own track plays it. If that's the track already playing
  (two areas of one town), it carries on without restarting.
- `SetMusic("")` owns silence: the music fades out.
- A scene that never calls `SetMusic` leaves the music alone, so a game with
  one global soundtrack (`AudioManager::PlayMusic` from app code) keeps it -
  except that when a scene owning music goes away, its track fades out with
  it rather than playing on into a scene that didn't ask for it.
- Pushed over: a pushed scene with its own track takes over; when it's popped,
  the scene below picks its track up where it was suspended (the overworld
  theme resumes mid-phrase after a battle). A pushed scene without music
  leaves the one below playing.

In the demo app the World demo and "Physics File" share a theme and the
battle has its own: B crossfades to the battle music and back.

---

## Boot flow

Engine splash, then the game's splashes, then the title menu or the first
scene. A game describes it with `BootConfig` (Phase 38's `project.arcbit` will
fill the same struct from data):

```cpp
MyGame() : Application({ ... })
{
    _boot.SplashImages = { "assets/ui/studio_logo.png" };
    _boot.FirstScene   = "Village";
    _boot.ShowTitle    = true;                 // title menu after the splashes
    _boot.TitleScene   = "TitleBackdrop";      // optional; empty = title over FirstScene
    _boot.CreditsPath  = "assets/credits.json";
}
```

- **The engine splash comes first and is engine-owned**: its image and timing
  are fixed in the engine (`ApplicationBoot.cpp`); a game adds splashes after
  it but has no way to change or remove it.
- **The first scene loads under the splash.** The main loop runs a queued
  scene swap while a game-blocking screen *covers loading*
  (`UIScreen::CoversLoading`); the splash covers it only while its image is
  fully shown, so the load never stalls a fade. Fades hold while the scene is
  hidden, so the scene fades in from black once the splash ends instead of
  appearing at full brightness. The scene's `OnPrepare` starts as soon as
  it's queued, so its files load on the loader thread while the splash
  animates; only the short `OnLoad` runs under the logo.
- **The title menu** (`MainMenuScreen`) opens when the splashes end, with
  Controls / Audio / Graphics / Credits wired to the engine's screens. With
  no `TitleScene` it sits over `FirstScene`, already running behind it, and
  New Game just closes it; with a `TitleScene`, New Game switches to
  `FirstScene`.
- **Credits** (`CreditsScreen`) scroll a JSON file: `title`, `sections` of
  `heading` + `names`, `scroll_speed`. A name can be a logo instead:

  ```json
  { "heading": "credits.engine", "names": [
      { "image": "assets/engine/ui/ArcbitA_withbackground.png", "height": 180, "uv": [0.2, 0.04, 0.8, 0.9] },
      { "images": ["assets/logos/sdl.png", "assets/logos/vulkan.png"], "height": 64 },
      "BigRigCodes" ] }
  ```

  `image` is one logo, `images` a row of them side by side; each is drawn
  `height` pixels tall (default 96) at its own aspect, cropped to `uv` if
  given. A logo that doesn't load is left out (logged). Application loads
  them through its texture manager.

The splash is on the `System` UI layer: screens pushed while it's up (a HUD
the first scene pushes in `OnLoad`) go beneath it, so it keeps ticking and
finishes (see [gui.md](gui.md#screen-management)).

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
| `music` | `path`, `loop` (default true), `category` (default `"music"`, the [audio category](audio.md#music) it plays at): the scene's [music](#music). An empty `path` is silence; no key leaves the music alone. |
| `load` | Measured [load data](#load-data-load) for the loading bar. Optional, written by tools. |
| `script` | The scene's own Lua script, a module name (`"demo.scene"`). See [scripting.md](scripting.md). |
| `persist_all` | Make every entity `Persistent` (saved changes), each under its file id; an entity opts out with `"Persistent": { "enabled": false }`. See [saves.md](saves.md). |
| `entities` | Entity id → components. |

### Entities

- **Ids** are strings unique within the file - readable names by hand (the
  editor, Phase 40, will generate them). The runtime uses fast
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
  state, not a save game ([saves.md](saves.md)).
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

### Load data (`"load"`)

Optional. Measured stage shares for the loading bar, and the textures they
were measured with:

```json
"load": {
    "shares":   { "prepare": 0.05, "decode": 0.7, "build": 0.25 },
    "textures": [ "assets/textures/GRASS+.png", "assets/textures/Water+.png" ]
}
```

- `shares` replace the default 0 / 95 / 5 split (any positive scale;
  normalized). They're proportions rather than times, so they hold up across
  fast and slow machines.
- `textures` detects stale data: if the textures the scene names no longer
  match the list, the load logs a warning to re-save the scene. The shares
  are still used - they're only a hint, and a wrong hint just makes the bar
  less even.
- The block is written by tools, not by hand: the editor measures a real load
  when it saves the scene, and `arcbit-pack` rewrites it at pack time (both
  later phases). `FileScene::Save` keeps an existing block as-is.

### Not in v1

- **Prefabs (templates and overrides)**: an entity that starts from a shared
  definition and overrides some fields (see the blueprint). The format
  reserves `"template"` and `"overrides"` keys on an entity for this.
- **Entity references between entities**: e.g. a door that names its target
  by id. Ids are in place for it; no component needs it yet.
- **Callbacks** (`TriggerCallback`, `CollisionCallback`): they hold C++
  functions. A scene file's entities react through their Lua scripts
  (`OnTriggerEnter`, `OnCollisionEnter`...) or a named system instead.

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
`AudioSource`, `Animator`, `FacingAnimation`, `Collider2D`, `MoveIntent`, `InputMovement`,
`AIMovement`, `FreeMovement`, `SmoothTileMovement`, `SnapTileMovement`,
`Pushable`, `PendingMove`, `EntityEvents`, `Persistent`, `Script`, `Interactor`, `Interactable`.
`AnimatorStateMachine` and the callback components
aren't serializable yet; `Held` is never saved (a conversation's, while it runs).

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
name is a load error. Lua scripts ([scripting.md](scripting.md)) are the
other way to give a scene file behavior - no C++ build needed.

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
- **Audio**: the file path - `AudioSource` sounds and the scene's `music`.

Textures and sprite sheets a scene loads are owned by that scene and
reference-counted: they are released when it unloads, and a texture another
scene still uses stays loaded. `.arcmap` atlas textures are still loaded
app-wide (not yet scene-owned). C++ scenes should load through
`Scene::LoadTexture` / `LoadSpriteSheet` too: `TextureManager::Load` pins a
texture for the app's lifetime. A scene's music and its `AudioSource` sounds
stop with it (see [Music](#music)).

Loading and saving go through the `SceneAssets` interface (path ↔ handle),
which `FileScene` implements on top of the application. Tests implement it
without a GPU, so the format is round-trip tested headless.
