# Arcbit — Project Blueprint

Checkbox list of every major milestone. Check off items as they are completed.

---

## Phase 0: Foundation
- [x] CMake project structure (root, core, engine, game, backends/vulkan)
- [x] vcpkg manifest + CMakePresets (SDL3, spdlog, VMA)
- [x] **Unit test project** — `tests/` → `arcbit-tests` (Catch2 v3 via vcpkg), registered with CTest; headless (no window / render device). The debug-only startup `*SelfTest()` functions (World, SpatialHash, PhysicsWorld, Narrowphase, Sweep, DirectionArc, ContactTracker) were migrated into test cases and removed from the engine. New code gets tests alongside it — starting with 22H's movement controllers
- [x] vcpkg overlay triplet (`cmake/triplets/x64-windows.cmake`) pins ports to the project's v143 toolset — prevents newer-Visual-Studio-built ports from failing to link

---

## Phase 1: Core Utilities
- [x] Type aliases (`i8` / `u8` / `f32` / `usize` / etc.)
- [x] Logger (spdlog, per-channel named loggers, DLL-safe SHARED library)
- [x] Assertions (`ARCBIT_ASSERT`, `ARCBIT_VERIFY`)
- [x] Profiler stub (`ScopedTimer`, Tracy-ready macros)

---

## Phase 2: Render API Design
- [x] `RenderHandle<Tag>` — generational indices, typed aliases
- [x] `RenderTypes` — enums, descriptors, flags system
- [x] `RenderDevice` public API (full surface, no Vulkan exposed)
- [x] `ARCBIT_RENDER_API` dllexport/dllimport macro

---

## Phase 3: Vulkan Backend — Initialisation
- [x] `VulkanContext` + `HandlePool<T>` slot-map
- [x] Instance creation + optional validation layers
- [x] Physical device selection (Vulkan 1.3 + dynamic rendering + sync2 required)
- [x] Logical device (dynamic rendering + synchronization2 features enabled)
- [x] VMA allocator
- [x] Command pool (RESET_COMMAND_BUFFER_BIT)

---

## Phase 4: Buffers & Shaders
- [x] `CreateBuffer` / `UpdateBuffer` / `DestroyBuffer`
- [x] `CreateShader` / `DestroyShader`

---

## Phase 5: Swapchain & Frame Loop
- [x] `CreateSwapchain` / `DestroySwapchain` / `ResizeSwapchain`
- [x] `AcquireNextImage` + per-frame sync (fences + semaphores)
- [x] `BeginCommandList` / `EndCommandList`
- [x] `BeginRendering` / `EndRendering` (image layout transitions via sync2)
- [x] `Submit` + `Present`
- [x] Clear-color frame rendered to the window in `main.cpp`

---

## Phase 6: Pipeline & Triangle
- [x] CMake glslc compile step (`.vert` / `.frag` → `.spv` at build time)
- [x] Minimal vertex + fragment shaders (hardcoded triangle, solid color)
- [x] `CreatePipeline` / `DestroyPipeline` (graphics pipeline)
- [x] `BindPipeline` / `SetViewport` / `SetScissor` / `Draw`
- [x] Triangle rendered to screen

---

## Phase 7: Textures & Samplers
- [x] `CreateTexture` / `UploadTexture` / `DestroyTexture`
- [x] `CreateSampler` / `DestroySampler`
- [x] Descriptor pool + descriptor set management
- [x] Textured quad demo

---

## Phase 8: Render Thread & Window
- [x] Dedicated render thread with double-buffered frame state
- [x] Window resize → swapchain recreation (out-of-date handling)
- [x] SDL events polled on the main thread and held until a fixed tick consumes them (`InputManager::ProcessEdges`), so no press is lost between ticks
- [x] `Window` class — RAII SDL_Window wrapper; hides SDL from game/engine headers

---

## Phase 9: Input System
- [x] `Action` registry — map named actions (`"Move_Left"`, `"Interact"`) to one or more bindings
- [x] Binding types: keyboard key, mouse button, gamepad button, gamepad axis (with deadzone; whole axis or one half via `AxisHalf`, Phase 22H)
- [x] `InputManager::IsPressed(Action)` / `JustPressed` / `JustReleased` / `AxisValue`
- [x] Keyboard + mouse support via SDL3 event stream
- [x] Gamepad / controller support via SDL3 `SDL_Gamepad` API (hot-plug add/remove)
- [x] Multiple bindings per action (e.g. `"Interact"` → E key OR gamepad A button)
- [x] Runtime rebind API (used by the settings screen)

---

## Phase 10: Settings System
- [x] Settings file (JSON, loaded on startup / saved on shutdown or explicit flush)
- [x] Graphics settings: VSync, target resolution, fullscreen mode
- [x] Audio settings: master volume, music volume, SFX volume
- [x] Input settings: persist action→binding map from Phase 9; apply on load
- [x] `Settings::Get<T>` / `Set<T>` typed accessors with dirty-flag auto-save
- [x] **Bindings across game updates** - today `SaveInputBindings` writes every action's bindings and `LoadInputBindings` replaces the code's defaults with them, with no record of what the player chose. Done - `MergeBindings`, the notice (`Defaults.BindingNotice`), `Settings::ResetBindingOnUpdate`; see `docs/input.md`. The rules, keeping what the player is used to:
  - Existing actions keep their current binding - rebound or not - even if the game's default for them changed
  - A new action gets its default if the key is free. If an action that kept its old key holds it, and that action's new default is free, the new action takes that one: an update moving Interact E -> F and giving Melee E leaves Interact on E and puts Melee on F. Otherwise the new action stays unbound
  - A one-time notice after the update lists what's new and where it landed ("New: Melee is F (E is your Interact)", "Melee has no key yet - set it in Controls")
  - The developer can reset a chosen action to its new default in an update (an old default that was broken), through the same clash rules and notice
  - The settings file records each action's bindings with the defaults they were based on, so a load can tell new, changed and kept actions apart
  - A per-game settings path, so the demos stop sharing (and inheriting) each other's bindings

---

## Phase 11: Application & Game Loop
- [x] `Application` class — owns Window, RenderThread, InputManager, SettingsSystem
- [x] `GameLoop` — fixed-timestep update tick separate from variable render tick
- [x] Virtual `OnStart` / `OnUpdate(f32 dt)` / `OnRender(FramePacket&)` / `OnShutdown` hooks
- [x] `main.cpp` reduced to constructing the Application subclass and calling `Run()`

---

## Phase 12: Asset System
- [x] `stb_image` integration (PNG / JPG decode to raw RGBA pixels)
- [x] `TextureManager` — load texture from file, cache by path, return `TextureHandle`
- [x] `SpriteSheet` — load atlas texture + JSON metadata; supports named frames, uniform tile grids, and animation clips in one unified format (see `docs/sprite-format.md`)
- [x] Asset hot-reload stub (watch file mtime, re-upload on change)
- [ ] Carrot engine format import — explicit import only (right-click in editor or `arcbit-pack --import-carrot`); validates Carrot structure before converting, fails clearly if source does not conform

---

## Phase 13: 2D Forward+ Lighting
- [x] `UseNormalTexture` + `UseStorageBuffer` flags on `PipelineDesc`
- [x] `BindStorageBuffer` — bind SSBO to descriptor set (no fixed array limit)
- [x] `GetCurrentFrameIndex` — expose per-frame slot index for SSBO cycling
- [x] `forward.vert` / `forward.frag` — single-pass lit sprite shaders
- [x] Default flat-normal texture in `RenderThread` (fallback for unlit sprites)
- [x] Per-frame light SSBO cycling in `RenderThread`
- [x] `PointLight` struct + `Lights` / `AmbientColor` fields on `FramePacket`
- [x] Point light demo scene

---

## Phase 14: Sprite Batch Renderer
- [x] Per-frame CPU-side sprite buffer (position, UV rect, tint, layer)
- [x] Sort / group by texture to minimize descriptor switches
- [x] Single instance buffer upload + one `Draw` call per texture group
- [x] World-space coordinate system (pixels or tiles); push constant projects world → NDC
- [x] Frustum culling — `Camera2D::IsVisible()` filters sprites before packet submission; uses AABB of rotated viewport so culling is correct at any camera rotation angle

---

## Phase 15: Camera System
- [x] `Camera2D` — world position, zoom level, rotation
- [x] Smooth follow (lerp toward target entity, configurable lag)
- [x] Viewport clamping (prevent camera from showing outside world bounds)
- [x] Screen shake (trauma-based decay; add trauma on hit, explosion, etc.)
- [x] World-to-screen and screen-to-world conversion helpers
- [x] Camera push constant integrated into Sprite Batch renderer
- [x] Camera push constant integrated into Tilemap renderer (deferred to Phase 19)

---

## Phase 16: Custom ECS
- [x] Archetype storage (component pools grouped by component set)
- [x] Entity create / destroy
- [x] Query API (iterate entities matching a component mask)
- [x] System registration + execution order
- [x] Starter components: `Transform2D`, `SpriteRenderer`, `LightEmitter`, `Parallax`, `CameraTarget`, `Disabled`, `Lifetime`, `Tag`, `Name`

### Movement Components (mix-and-match design)
Controller components drive *where* an entity wants to move; style components
define *how* it gets there. Any controller pairs with any style.

**Controller components** (who decides the destination):
- [x] `InputMovement` — reads input bindings each tick; converts pressed directions into a `MoveIntent` (Phase 22H)
- [ ] `AIMovement` — driven by pathfinding / behavior system; writes the same `MoveIntent` programmatically (struct declared, no system yet — Phase 30)

**Style components** (how the entity actually moves):
- [x] `SmoothTileMovement` — lerps from tile center to tile center; plays a walk animation mid-transit; next input queued but not consumed until landing
- [x] `SnapTileMovement` — jumps instantly to the destination tile; no interpolation; a held direction repeats every `StepInterval`; useful for puzzle / grid games (Phase 22H)
- [x] `FreeMovement` — pixel-perfect world-space velocity; not tile-aligned; intended for live-combat sections where precise sub-tile positioning matters

**Usage examples:**
- Overworld player → `InputMovement` + `SmoothTileMovement`
- Puzzle NPC      → `AIMovement`    + `SnapTileMovement`
- Combat enemy    → `AIMovement`    + `FreeMovement`
- Combat player   → `InputMovement` + `FreeMovement`

---

## Phase 17: Animation System
- [x] `AnimationClip` — ordered list of named frames with per-frame duration; defined in the sprite format JSON (`docs/sprite-format.md`) and loaded via `SpriteSheet`
- [x] `Animator` component — current clip, elapsed time, frame index, playing flag; `AnimatorSystem` advances frames and writes `SpriteRenderer.UV` + `SpriteRenderer.Pivot` each tick
- [x] `SpriteRenderer.Pivot` applied by `SpriteRenderSystem` — offsets the rendered quad so the logical anchor aligns with `Transform2D.Position`
- [x] State machine: `AnimatorStateMachine` component — named states, float/bool/trigger parameters, per-transition condition lists, optional exit-time gate; `AnimatorStateMachineSystem` evaluates transitions and updates the paired `Animator` clip automatically
- [ ] Blend between clips (cross-fade — not applicable to pixel-art sprite sheets; deferred indefinitely)
- [x] Events on specific frames — `"events": ["FootStep"]` in sprite JSON; `Animator::OnEvent` callback fired by `AnimatorSystem` when the frame becomes active

---

## Phase 18: Audio
- [x] Audio library integration (miniaudio via vcpkg — single-header, no runtime deps)
- [x] `AudioManager` — initialize device, master volume control
- [x] Sound effect playback (fire-and-forget one-shots)
- [x] Music streaming (looping background track, crossfade)
- [x] `AudioSource` ECS component (spatial attenuation for point sounds)
- [x] Wire volume levels to Settings System (Phase 10)
- [x] Volume settings are perceived loudness, not amplitude - `AudioManager::SliderToGain` (gain = slider^(1/0.6), Stevens' power law): 100 is the authored mix, 50 sounds half as loud (-10 dB), 10 a tenth (-33 dB); a linear slider left 10% at only -20 dB, which still sounded loud
- [x] `PlayOneShot` honors its `volume` (it used `ma_engine_play_sound`, which has none, so every one-shot played at full gain)
- [ ] Loudness guidance for game assets (e.g. music around -24 to -26 LUFS, SFX peaks below -3 dBFS), checked by `arcbit-pack` at import (Phase 36)

**Revisit: audio categories** - beyond the fixed master / music / SFX
- [x] Game-defined audio categories (buses): a game declares its own - menu music, game music, combat music, ambience, voice, UI - each with its own player-facing volume slider, persisted in settings like the built-in three. Lets a player keep menu music at 70 but in-game music at 40
- [x] Categories nest under a parent (combat music under music under master), so one slider can still turn a whole group down; the settings screen lists whatever categories the game defines
- [x] Every sound names its category: `PlayOneShot`, `AudioSource`, `Scene::SetMusic` / the `.arcscene` `"music"` key, UI skin sounds; a crossfade between tracks in different categories fades each at its own volume (one track plays at a time; see `docs/audio.md`)
- [ ] Optional per-asset gain (or measured loudness stored with the asset at pack time, Phase 36) so tracks mastered at different levels play at a consistent level without editing the files

---

## Phase 19: Tilemap System
- [x] Tile definition (ID, solid flag, light-blocking, flat, uv_scroll, flip-book animation)
- [x] Chunk-based map storage (16×16 tile chunks)
- [x] Tile atlas texture + UV lookup
- [x] Tilemap renderer (batched instanced quads via Phase 14 sprite batcher)
- [x] Camera / viewport culling (only submit visible chunks)
- [x] Multiple layers (ground, objects, overlay; each layer rendered in order with Y-sort and flat-object support)
- [ ] **Two point light layers** — add `LightLayer` enum to `PointLight`: `World` (affected by tile occlusion) and `Overhead` (bypasses occlusion entirely); overhead lights are used for sunlight, moonlight, magical sky effects, and UI indicators
- [ ] **Tile occlusion grid** — pack `light-blocking` flags for the visible map area into a GPU texture; updated incrementally when tiles change; uploaded to the sprite shader as a new descriptor binding. Only needed if shadows move to the GPU: today's are CPU-raycast polar shadow maps (below). Old design notes: `docs/plans/tilemap-future.md`
- [x] **Shadow casting** — CPU DDA raycaster builds a 1D polar shadow map per light; sampled in the sprite fragment shader with 3-tap PCF; `BlocksLight` tile flag drives occlusion
- [ ] Soft shadow quality option: configurable penumbra sample count in `project.arcbit` (default: hard shadows; higher counts add a blur fringe for performance/quality tradeoff)
- [x] **Entities Y-sort with Objects tiles on their own** - today an entity's `SpriteRenderer.Layer` is fixed, and a character walking among Objects tiles has to set it to its feet's Y each tick (the World demo does); a flag or component (`YSort`) that the sprite system applies would make it automatic for every NPC and scene-file entity. Done: `SpriteRenderer::YSort`, on by default (`Defaults.YSortSprites`), `"y_sort": false` for flat things

---

## Phase 20: Font Rendering
- [x] Font rasterizer integration (`stb_truetype`)
- [x] Bitmap font atlas generation at startup (bake glyphs into a `TextureHandle`)
- [x] `DrawText` helper — lays out a string into sprite-batch quads with correct UVs; supports `\n`, `\t`, and `TextAlign` (Left / Center / Right)
- [x] SDF font variant — dedicated screen-space pipeline; engine ships Roboto-Regular for the built-in debug overlay
- [x] Debug overlay — FPS, sprite/batch counts, draw calls, lights, chunk culling stats; toggled via `Settings::Graphics.ShowDebugInfo`

**Revisit: text paths** - today `FontMode` ties how text is rasterized to where it's drawn: `Bitmap` always goes through the lit world sprite pipeline, `SDF` always through the screen-space UI one. Split the two: the font decides rasterization, the caller decides the destination.
- [ ] Font kind decides rasterization: **pixel** fonts bake as bitmaps at their design size - hard edges (no antialiasing), nearest sampling, whole-number scales, outline / shadow as one-pixel offset copies; **smooth** fonts are SDF (any scale, 21F's effects)
- [ ] World text, lit (signs, banners in the scene): both kinds; smooth fonts need an SDF mode in the sprite shader so they stay sharp under camera zoom (Valve's "Improved Alpha-Tested Magnification", SIGGRAPH 2007)
- [ ] World-anchored text, unlit (damage numbers, "+5 gold", name tags): follows a world point or an entity each frame but draws with the UI text renderer - above the world, unlit, with text styles and effects; optional scale with camera zoom; rise / fade helpers. Needed by Phase 31
- [ ] Pixel fonts on screen too (UI, dialog box) for pixel-art games
- [ ] A default pixel font shipped with the engine - candidates Pixel Operator (CC0), monogram (CC0), with Fusion Pixel (OFL) as a Japanese fallback; check each license file before shipping
- [ ] Decide `FontMode::Bitmap`'s future once the above lands (it may become just "pixel")

---

## Phase 21: GUI / UI System ✓
- [x] Retained-mode widget tree (`Panel`, `Scrim`, `Label`, `Button`, `Image`, `ProgressBar`, `NineSlice`, `NineSliceButton`, `NineSliceProgressBar`, `ScrollPanel`)
- [x] Input widgets: `TextInput`, `Slider`, `Dropdown`, `Checkbox`, `RadioGroup`, `Switch`
- [x] Anchoring + relative layout (anchor/pivot/offset/size/size-percent per widget)
- [x] Input routing — `BlocksInput` screens consume input before the game; tab/arrow/confirm/back navigation
- [x] Theming: `UISkin` JSON with full color palette + sound key fields; per-widget `SkinOverride`; `.arcui` `"skin"` block
- [x] `Screen` abstraction — push/pop with fade transitions; `BlocksInput` and `BlocksGame` flags
- [x] `.arcui` JSON layout format + `UILoader`; `text_key` localization; `UILoader::RegisterType` for custom widgets
- [x] HUD layer (`HudScreen`): FPS label, controlled by `Settings::Graphics.ShowFps`
- [x] Pause menu (`PauseMenuScreen`): resume, controls, audio, graphics, quit
- [x] Input rebinding UI (`InputRebindScreen`): multi-binding chip list, key/mouse/gamepad columns
- [x] Audio settings (`AudioSettingsScreen`): master/music/SFX sliders
- [x] Graphics settings (`GraphicsSettingsScreen`): VSync, Show FPS, Show Debug Info via `Switch` widgets
- [x] Splash screen (`SplashScreen`): ordered image sequence, per-entry fade-in/hold/fade-out, skip on confirm, `BlocksGame`
- [x] Main menu (`MainMenuScreen`): data-driven `.arcui`, hideable buttons, C++ fallback
- [x] Engine splash: `ArcbitA_withbackground.png` pushed automatically in `Application::Run` before game content
- [x] Skin sound keys wired: `SoundFocusMove` on nav, `SoundActivate` on button, `SoundToggle` on switch/checkbox, `SoundSliderTick` on slider step, `SoundBack` on back-press
- [x] Localization: `Loc::Get(key)`, `en.json` engine strings, `text_key` in `.arcui`
- [x] Engine asset pipeline: `.arcui`, fonts, skins, locale deployed under `assets/engine/` via CMake
- [ ] Editor integration: GUI layouts designed and previewed in the AvaloniaUI editor (Phase 40)
- [x] Dialog screen (Phase 27)
- [ ] Inventory screen (Phase 29)
- [x] Save / load slot screens, a yes / no confirm screen, the saving indicator (Phase 25)

### Phase 21F - Text styling & animation
*Built together with Phase 27 (dialog), after Phase 28 (localization), so the dialog box gets styled, animated text from the start. Fonts, skins and `Label` grow beyond one plain face. The SDF font path does most of the work: outlines, glows and faux bold are distance thresholds in the shader.*

- [x] Font styles: bold, italic and bold italic from a font's style faces (`FontAtlas::SetStyleFace`; Roboto's three ship with the engine), faux bold (an SDF threshold) and faux italic (a shear) for fonts without them
- [ ] Weights from thin to black, each its own face or an SDF threshold, chosen per text style
- [x] Text decorations: outline (color, width), drop shadow (offset, color, softness - with no offset, a glow), underline and strikethrough
- [x] Text styles in `UISkin` and per widget (`SkinOverride`, `"text_style"` in `.arcui`), so a skin sets the look of every label at once
- [x] Inline rich text markup (`<b>`, `<i>`, `<u>`, `<s>`, `<color=#f00>`, `<size>`, `<outline>`, `<shadow>`, `<wave>`, `<shake>`, `<rainbow>`, `<pause>`, `<speed>`), in localized strings too; labels opt in with `"markup": true`; laid out into a styled glyph list that wraps across runs
- [x] Animated text: `TextReveal` typewriter (characters per second, `<pause>` and `<speed>`, a voice sound per character, skip, done callback; `Label::StartReveal`) and per-character effects - wave, shake, rainbow - plus a fade-in for the character being revealed
- [ ] Editor: font, style and effect pickers with a live preview (Phase 40)

---

## Phase 22: 2D Collision
*Custom kinematic-first collision system — no rigid-body dynamics. Full design
in `docs/physics.md`. Covers tile collision, sub-tile entity colliders,
directional arc gating, optional rotation, triggers, and per-style movement
integration. Arcbit uses its own collision rather than Box2D - why is in
`docs/physics.md`.*

### Phase 22A — Core types & static collision ✓
- [x] `BodyKind`, `Collider2D` (Box / Circle, `Rotation` for OBB later, `Layer` / `Mask`, `IsTrigger`) headers + ECS registration
- [x] `SpatialHash` (uniform grid keyed on tile size) with insert / remove / query
- [x] `PhysicsWorld` skeleton — owns the hash and the tilemap pointer; tick stub
- [x] AABB↔AABB and AABB↔Circle narrowphase (rotation==0 fast path)
- [x] Tile-synthesized colliders from `TileDef.Solid`, with **greedy-mesh** rectangle merging per chunk; rebuild on tile mutation
- [x] Engine default collision layers (`Default`, `Player`, `NPC`, `Enemy`, `Wall`, `Prop`, `Pickup`, `Projectile`, `Trigger`); user layers extensible from bit 9 upward
- [x] Debug draw — runtime `PhysicsDebugDraw` flag (kinematic green / static red); toggled from a dev key binding in demo, from the editor IPC channel in Phase 40. **Not** a player-facing setting.

### Phase 22B — `FreeMovement` collision ✓
- [x] `PendingMove` component + `FreeMovementIntegrateSystem` writes desired delta into it
- [x] `CollisionResolutionSystem` — swept resolution, slide along contact normal, one re-sweep
- [x] `Layer` / `Mask` filtering applied in narrowphase
- [x] Demo: player can't walk through trees / rocks; slides cleanly along walls

### Phase 22C — Tile movement collision ✓
- [x] `PhysicsWorld::QueryTileBlocked(entity, targetTile)` for the plan-then-commit flow
- [x] `TileMovementPlanSystem` integrates `SmoothTileMovement` and `SnapTileMovement` (22H) with the query before a tile transition is committed
- [x] Demo: tile-style player can't enter solid tiles; queued-input-during-transit semantics preserved

### Phase 22D — Directional collision (arcs) ✓
- [x] `DirectionArc` type (`{ centerDeg, halfWidthDeg }`) + preset library (`AllDirections`, `Vertical`, `Horizontal`, `NorthOnly`, ...)
- [x] Narrowphase emits a contact direction; arc check filters blocking contacts. Arcs evaluated in the collider's **local frame** so they rotate with the body
- [x] `TileDef.BlockedFrom` field (vector of arcs); tile-loader / atlas-spec support
- [x] Demos: `ArcDemo` (free movement — bush `Horizontal()`, one-way platform `SouthOnly()`, tree as offset stump collider) and `TileMoveArcDemo` (tile movement — canopy/trunk tree pair, bush, `NorthOnly()` cliff strip)

### Phase 22E — Rotated colliders (OBB) ✓
- [x] OBB↔OBB narrowphase (SAT) and OBB↔Circle (transform-into-local)
- [x] OBB sweep — substepping fallback for Phase 22 (split into N small AABB-of-OBB-bounds steps); promote to true OBB sweep only if profiling demands. Rotation is rare.
- [x] Demo (`ObbDemo`): diagonal wall slide, rotated chest, thin plank, spinning gate whose directional arc rotates with it

### Phase 22F — Triggers & events ✓
- [x] `IsTrigger` flag on `Collider2D`
- [x] `TriggerCallback` and `CollisionCallback` ECS components
- [x] Per-pair persistent set diffed each tick → `OnEnter` / `OnExit` (and `OnStay` for triggers only)
- [x] Demo (`TriggerDemo`): warp door teleports on enter, hazard zone Enter/Stay/Exit, NPC bump `CollisionCallback` in tile and free movement

### Phase 22G — Polish & integration
*Parked: the event-bus item was settled in Phase 24; the editor item waits on Phase 40. Execution order after 22F is **22H**, then the partial-move item here.*
- [ ] Optional `SmoothTileMovement.AllowPartialMove` — sub-tile stop policy for the rare scenes that want it
- [x] Event Bus publishing settled in Phase 24: collisions and triggers stay per-entity callbacks (one bus event per contact would flood it); a game publishes the contacts that matter from its callback in one line
- [ ] Editor integration (Phase 40) — collider authoring panel, live preview, directional-arc arrows on the canvas

### Phase 22H — Movement controllers ✓
*Replaces the hand-rolled WASD blocks copy-pasted across the demos with the controller → style pipeline designed in Phase 16 (`docs/ecs.md` "Movement"). Lands after 22F; does not wait on 22G.*
- [x] `InputMovementSystem` — reads `InputMovement` actions into a `MoveIntent`; style systems turn it into `FreeMovement.Velocity` (diagonals normalized) or `SmoothTileMovement.QueuedDir` (cardinal-only). Reads all actions as released while a `BlocksInput` screen is up
- [x] Defined two-key tie-break for tile styles: the most recently pressed key wins everywhere — across axes (X when both go down together) and between opposite keys on one axis, for free movers too
- [x] Resolve `InputMovement.Speed` unit ambiguity — removed; style components own their speed (`FreeMovement.Speed` px/s, `SmoothTileMovement.Speed` tiles/s, `SnapTileMovement.StepInterval` s)
- [x] Expose the resolved move direction on the entity — `MoveIntent.Direction` plus `MoveIntent.Facing` (last nonzero direction)
- [x] `SnapTileMoveSystem` — plan-then-commit via `PhysicsWorld::QueryTileBlocked`, instant `Transform2D` write + broadphase refresh; a fresh press steps at once, a held one repeats every `StepInterval`
- [x] Intent path shared so `AIMovementSystem` (Phase 30) plugs in without touching the style systems — it writes `MoveIntent` like `InputMovementSystem` does
- [x] Migrate `PhysicsDemo`, `ArcDemo`, `ObbDemo`, `TileMoveDemo`, `TileMoveArcDemo`, `TriggerDemo`, `WorldDemo` to `InputMovement`; M toggles smooth / snap in `TileMoveDemo`
- [x] Update `docs/ecs.md` movement section and built-in system table
- [x] Half-axis gamepad bindings — an axis bound to both `Move_Left` and `Move_Right` (as `WorldDemo` did with `LeftX`) pressed both whenever the stick moved, so they cancelled. `Binding::Half` (`AxisHalf::Full / Positive / Negative`) lets each action take one half; saved to settings as `"half"`


- [x] **Tap to turn** for tile movers: standing still, a quick tap of a direction other than the one faced turns in place, and only walks if held past `TurnDelay` (~0.1 s); the faced direction walks at once. On by default - per mover (`"turn_delay"`, 0 = off) and per project

### Phase 22I - Physics responses
*Movement that comes from the world rather than the mover's own intent. Lands before Phase 32 (Live Combat), whose knockback and dash build on it.*
- [x] External velocity on `FreeMovement` - an impulse channel added on top of the intent / authored velocity and decayed by `Friction`. Since 22H an intent-driven mover's `Velocity` is rewritten every tick, so an impulse written to `Velocity` would vanish next tick. Knockback, dash bursts, conveyors and bumpers all use it
- [x] Push-out (depenetration) for obstacle-caused overlaps - a free mover overlapping a collider it didn't walk into (a rotating gate turning into it, a prop spawned or teleported onto it) is pushed out along the contact normal. Overlaps the mover made itself (entering a directional collider through a pass-through face) are left alone, so one-way colliders still work
- [x] Pushable objects - a `Pushable` component; a free mover walking into one shoves it along (swept against the world, so it stops at walls) at a configurable resistance
- [x] Tile-mover pushing (Sokoban-style crates) - a tile step into a `Pushable` moves it one tile if the cell beyond is clear; a smooth mover carries the crate one tile ahead in lockstep so a held push never stalls (crates in TileMoveDemo)
- [x] Demos - a bumper and a pushable crate in PhysicsDemo; ObbDemo's gate rotates through the player instead of stalling

---

## Phase 23: Scene Management
*`Scene` already exists from Phase 16 (ECS world, camera, `WorldConfig`, `TileMap`, lazily-created `PhysicsWorld`; lights are entities), but `Application` owns exactly one for the whole run. This phase makes scenes swappable, then uses that for a demo switcher and a proper boot flow.*

**Foundations**
- [x] Entity destroy cleanup — `World::OnRemove<T>` hooks run whenever a component leaves an entity (`RemoveComponent` or `DestroyEntity`); `Scene` hooks `Collider2D` to drop the broadphase record, so no removal path leaves an invisible wall. `LifetimeSystem`'s manual unregister is gone
- [x] Mid-run GPU resource safety — `RenderThread::WaitIdle()` parks the render thread and drains the GPU so the main thread can create / destroy resources between frames (scene switches); texture hot-reload now goes through it instead of racing the render thread
- [x] Scene lifecycle — scene types with `OnLoad` / `OnUnload` / `OnUpdate` / `OnRender` hooks (plus `OnPrepare` for background loading and `OnSuspend` / `OnResume` for the stack), so game setup that lives in `Application::OnStart` today moves onto the scene it builds. The hooks are engine extension points: a data-driven scene (below) is one generic `FileScene` class whose `OnLoad` reads an `.arcscene`, so editor-made games need no C++ scene subclasses

**SceneManager**
- [x] `SceneManager` — load / unload / switch the active scene by name (`Register(name, factory)`, queued `Switch(name)`); `Application` drives the active one instead of owning a single `Scene`
- [x] Scene-owned resources — textures acquired through `Scene::LoadTexture` are reference-counted (`TextureManager::Acquire` / `Release`) and released automatically on unload; a data-driven scene can't hand-write cleanup
- [x] Scene-owned sprite sheets — `Scene::LoadSpriteSheet` caches sheets per scene and loads their textures through `LoadTexture`, so they're released on unload (`Animator.sheet` in scene files)
- [x] Scene-owned music — `Scene::SetMusic` (`"music"` in scene files): the active scene's track crossfades in at each swap (`SceneManager::MusicFadeTime`), a shared track carries on, a departing owner's track fades out, and a scene suspended under a push resumes its track where it was. Scenes that never set music leave app-level music alone. `AudioManager` music gained crossfades (two slots), seek and position; `MusicPlayer` is the seam that makes it testable headless
- [x] Entry point on switch — `Switch(name, entry)` so a door or script can say where the player arrives (e.g. a named spawn marker)
- [x] Fade-to-black transitions — every scene change fades the outgoing scene out *and the incoming one in* (full-window quad under the UI); the first scene fades in from black
- [ ] Crossfade transition (needs the outgoing scene rendered to an offscreen target — Phase 34's render-target work)
- [x] Async streaming: load the next scene in the background while the current one keeps running - `Scene::OnPrepare` runs on a loader thread (file reads, image decodes through `ScenePreloader`) while the fade-out plays; the main-thread `OnLoad` then only uploads and builds entities, and the swap holds at black until the prepare is done. `SceneManager::Preload(name)` starts it early with the current scene still running. `FileScene` and the demos prepare themselves
- [x] Two ways to cover a load, per change (`SceneChange`): fade first (default, for doors - holds at black if the load outlasts the fade) or `WaitForLoad` (the current scene keeps running and playable until the next is ready, then fades - menu jumps, keep-playing loads)
- [x] Loading screen over a long hold at black - the engine's `LoadingScreen` ("Loading" + animated dots + progress bar) after a short grace period, fading out as the scene fades in; `Application::SetLoadingScreen` replaces it with any `UIScreen`, `SceneChange::ShowLoadingScreen = false` keeps plain black
- [x] Loading progress - `SceneManager::GetLoadProgress()` across three stages (prepare / decode / build). `OnPrepare` only names images and the loader decodes them afterwards, so the total is known up front and the bar is weighted by file size; scenes doing heavy work of their own claim a share (`SetShares`, `ReportPrepareProgress`). The default `LoadingScreen` draws a bar
- [x] Measured load data in scene files - an optional `"load"` block (stage shares + the texture list they were measured with) replaces the default split; a stale texture list logs a warning to re-save
- [x] Scene stack — `Push` suspends the active scene (`OnSuspend`; not updated or drawn, sounds stopped, state kept) and loads one on top; `Pop` unloads it and resumes the one below (`OnResume`); `Switch` replaces the whole stack. B in the demo app pushes a battle arena over any demo. UI overlays like inventory stay on the UI screen stack
- [ ] Draw a suspended scene under the one above (e.g. a battle over a dimmed overworld) - needs the outgoing scene rendered to an offscreen target (Phase 34), like crossfade
- [ ] Entry-point scene configured in `project.arcbit` (Phase 38)

**Demo switcher**
- [x] Each demo becomes a scene registered in one `DemoApp`; F1 opens a demo-select menu, PgUp / PgDn cycle, and the last demo is remembered in settings. Replaces the `#define ARCBIT_DEMO_*` selection in `game/src/main.cpp`. `arcbit-game --cycle <seconds>` switches demos on a timer as a soak test for load / unload (no keyboard needed)
- [x] Scene teardown releases outside resources — `Scene`'s destructor destroys its entities so `OnRemove` hooks run; an `AudioSource` hook stops its sound (a looping sound used to outlive its scene)

**Scene files** — the data-driven path the editor (Phase 40) and runtime host (Phase 38) build on
- [x] One `.arcscene` format — reconciled into `docs/scenes.md`: id-keyed entities with inline components (sorted on save for merge safety), a referenced `.arcmap`, only non-default fields written; prefabs as `"prefab"` with inline fields
- [x] Component registry — `SceneRegistry` maps component name ↔ C++ type ↔ JSON read / write; the engine registers its 20 built-ins, games add their own
- [x] `.arcscene` loader and saver — `SceneFile::Load` / `Save` (headless, through a `SceneAssets` interface) and `FileScene` on top; file id → `Entity` table (`FindEntity`); exact load → save round trip tested
- [x] Named systems — game code registers installers by name; a scene file lists the ones it uses
- [x] At least one demo converted to an `.arcscene` — "Physics File" (`game/assets/scenes/physics.arcscene` + `maps/physics.arcmap`) runs with no C++ scene class
- [x] **Prefabs**: an entity starting from a shared template with field overrides. Done as `.arcprefab` files (`"extends"`), `"prefab"` on a scene entity with its fields inline, a live link at load, `Scene::Spawn` / `Entity.Spawn` / console `spawn` (`docs/scenes.md#prefabs`)
- [x] Texture drawn with two samplers in one frame — the Vulkan backend kept one descriptor set per texture and rewrote it when the sampler changed, which invalidated the frame's command buffer; now one set per (texture, sampler) pair, written once (found by the scene-file demo sharing the engine's white texture with the UI)

**Boot flow** — engine splash → game splash(es) → title screen → gameplay
- [x] First scene loads under the splash — scene swaps run while a game-blocking screen covers loading (`UIScreen::CoversLoading`; the splash only while its image is fully shown), instead of after the splash. The scene's files load on the loader thread while the splash animates (`OnPrepare`); only the short `OnLoad` runs under the logo
- [x] Hand-off into the first scene fades in — fades hold while the scene is hidden, so it fades in from black as the splash ends (skipping too)
- [x] Engine splash is engine-owned: its content and timing are defined inside the engine (`ApplicationBoot.cpp`), not exposed to games. Games append their own splash entries after it (`BootConfig::SplashImages`) but can't replace, reorder, or remove it (hardened in packed builds by the Phase 36 asset protection)
- [ ] Engine splash entry can be a video once Phase 35 video playback lands (animated intro)
- [x] Title screen as the default first stop after the splashes — `BootConfig` (`FirstScene`, `ShowTitle`, `TitleScene`, `CreditsPath`) drives splash → title → game; the engine wires the title's buttons to its own settings / credits screens
- [x] Credits screen — `CreditsScreen` scrolls a JSON file (title, sections of heading + names, speed), reachable from the title menu; `Application::ShowCredits` can roll it at the end of a game too
- [x] Credits logos — image entries in the credits roll: `"image"` / `"images"` (a row), `"height"`, `"uv"` crop

**Docs**
- [x] `docs/scenes.md` covers the whole phase - lifecycle (hook order and threads), stack, swap rules, loading ahead and progress, boot flow, the `.arcscene` format with its `"load"` block, registry, named systems, asset ownership; `docs/ecs.md`'s Scene, removal-hook, Application-integration and file-layout sections brought up to date

---

## Phase 24: Event Bus
*Plan: `docs/plans/24-event-bus.md`. Reference: `docs/events.md`.*

- [x] Named pub/sub bus: `EventBus` (`Application::GetEvents()`) - `Subscribe` / `Publish` by `EventId` (a name hashed with the shared `HashName`, like input actions) or by name; `Register` / `Describe` keep names for logs, Lua and the editor
- [x] Payload: `EventPayload` - named fields of a closed type set (bool, integer, decimal, text, `Entity`, `Vec2`) instead of `std::any`, so Lua, the editor monitor and saves can read any event; `Get<T>(name, fallback)` converts between number types
- [x] Deferred dispatch: `Publish` queues and the bus delivers once per fixed tick, after the scene update - handlers never run mid-update. Events published during delivery go out in the same pass, capped (`MaxPerFlush`) against handlers publishing to each other forever; `PublishNow` for the exceptions
- [x] Owned subscriptions: `Subscription` unsubscribes when destroyed; safe to subscribe or unsubscribe during delivery. `Scene::Subscribe` ends with the scene's unload (a suspended scene keeps listening)
- [x] Naming: lower-case dot paths; `engine.` is reserved - game code subscribes to engine events but can't publish them (`PublishEngine` is the engine's)
- [x] Engine events (`EngineEvents`): scene loaded / unloaded / suspended / resumed, `engine.input.device_changed` (new: `InputManager::GetLastDevice`), window focus / resize, `engine.settings.changed` (screens report their section; `Settings::Set` reports `game` + key)
- [x] Opt-in entity events: `EntityEvents` (code or `.arcscene`) announces spawned / destroyed for chosen entities only, each renameable per entity (`"boss.defeated"`); scene unloads and component removal aren't deaths. New `World::OnAdd<T>` / `OnDestroy` hooks underneath
- [x] Bus vs callbacks settled: per-entity callbacks (triggers, collisions, animation events) stay; a game publishes from them in one line when it wants a broadcast
- [x] Debugging: `SubscribeAll` (the editor monitor's hook), every event logged on the `EventBus` channel; demo app F3 event overlay and coin pickups in the Triggers demo
- [x] Lua integration: `Event.Subscribe("name", handler)`, `Event.Unsubscribe`, `Event.Publish("name", payload)` - Phase 26
- [ ] Editor integration: live event monitor (name, payload, source) - Phase 40
- [ ] Progression events (`quest.started`, `player.leveled_up` and friends) - when a quest / progression system is planned; until then games define them as their own events
- [ ] Day / night events - with a day / night cycle, when one is planned

The rest of the original event list moved to the phases that build those systems, renamed to the dotted `engine.` style: saves (25), dialog (27), items (29), combat and death (30 - 32), weather (34), achievements (39).

---

## Phase 25: Save / Load System
*See [docs/saves.md](docs/saves.md).*

- [x] Save files: one versioned JSON document per slot (`SaveGame`) - format and game versions, metadata, `SaveData` key / values, participant sections; crash-safe writes with a `.bak` fallback
- [x] Sealed saves: zstd + XChaCha20-Poly1305 (libsodium) in release builds, so a save can't be edited by hand; plain JSON in debug; a per-game key (`SetSealKey`) until Phase 36's project keys
- [x] Per-user data folder (`%APPDATA%\<Studio>\<Game>\`): saves and `settings.json` (moved from beside the executable)
- [x] Save the changes, not the world: `Persistent` entities (and scene-level `persist_all` with per-entity opt-out) record field-level changes against the authored scene, destroyed and spawned entities, and tile changes; each scene's changes outlive it
- [x] Captured state: the scene stack with entry points, every visited scene's changes, game data, participants, playtime
- [x] Slots: 10 manual, 3 rotating autosaves, 1 quicksave by default, set per game; metadata (name, scene, date, playtime) and a thumbnail of the world without UI
- [x] `Application::SaveTo` / `LoadFrom` / `AutoSave` / `ResetGame`; refused mid scene change; loading replaces the stack in one fade
- [x] Auto-save API, with an opt-in autosave after scene changes; a saving indicator on every save and a saving notice splash card (first launch by default)
- [x] Save / load slot screens; pause menu Save / Load, title Continue / Load Game
- [x] Migration / version-upgrade path so old saves stay compatible after content updates
- [x] Publishes `engine.game.saved` and `engine.game.loaded` via the Event Bus (Phase 24)
- [x] Lua API exposure so scripts can read and write custom save keys (Phase 26)

---

## Phase 26: Lua Scripting
*See [docs/scripting.md](docs/scripting.md).*

- [x] Lua 5.4 integration (sol2 via vcpkg), kept out of engine headers; `ScriptEngine` owns the state
- [x] Sandboxed execution: no filesystem, OS, debug or bytecode access; `require` only from the script root; per-module environments; instruction and memory limits; every call protected
- [x] Opt-in `Script` component (module, properties, saved state) and scene scripts (`.arcscene` `"script"`)
- [x] Lifecycle hooks: `OnLoad`, `OnStart` (after saved state is applied), `OnUpdate(dt)`, `OnDestroy`, `OnUnload`
- [x] Entity hooks: `OnInteract` (`Interactor` / `Interactable`, `engine.entity.interacted`), `OnTriggerEnter/Stay/Exit`, `OnCollisionEnter/Exit` alongside C++ callbacks
- [x] `OnEvent(eventName, payload)` for `Event.Subscribe(name)` without a function
- [x] Engine API: entities (find, create, destroy, any registered component by name, stale-safe handles), Scene, Tilemap, Input, Audio, Save, Event (`Subscribe` / `Unsubscribe` / `Publish`)
- [x] Script state in saves (`self.Saved`)
- [x] Tasks and timers: `self:Run`, `Wait`, `WaitUntil`, `Timer.After / Every`
- [x] Lua UI: `.arcui` `"script"`, `"on_click"` / `"on_change"`, `UI.Open`
- [x] Debug console: commands (C++ and Lua), Lua, history, predictive completion, `ConsoleAccess`, cheats flag `engine.cheats_used`
- [x] Hot reload (debug builds) keeping instance state; script errors in the console and on screen
- [x] The Scripted demo, built entirely in Lua

---

## Phase 27: Dialog / Conversation System ✓
*Built together with Phase 21F (text styling and animation), after Phase 28. See [docs/dialog.md](docs/dialog.md). The default implementation - a game can run conversations its own way (`DialogRunner`, its own screen) or in Lua.*

- [x] `.arcdialog` files: speakers (name, portrait, voice) and named nodes of steps - lines (`say`, or a bare string), choices, branches, actions - each with an optional `if` and `goto` (a node or `"end"`); `"world": "run"` / `"pause"`
- [x] Checked as they load: gotos to missing nodes, unknown speakers, duplicate text keys, bad comparisons, empty steps; misspelled fields and unreachable nodes warned
- [x] English lives in the dialog file; each line, option and speaker name gets a Loc key from its place (or an `id` / `key`), and the English goes into Loc (`Loc::SetStrings`) so translations go in the locale files
- [x] Conditions on `SaveData` (equality, comparisons, `any`, `not`) and Lua expressions; actions `set`, `add`, `event`, `lua` and `call` (a method on the NPC's script); `{placeholders}` from save data
- [x] `DialogRunner` - the headless conversation (lines, choices, branches, actions), `DialogLibrary` - every dialog in a folder, with hot reload
- [x] `DialogScreen`: nine-slice text box, name plate, portrait, typewriter in the speaker's voice, bobbing next arrow, choice box (keys, mouse, gamepad); stock placeholder art; layout and art replaceable per game
- [x] Game settings: Text speed (slow / normal / fast / instant) and Auto-advance
- [x] Starting one: `Interactable` `"dialog"` (no script needed), Lua `Dialog.Start / IsActive / Stop`, `Application::StartDialog`, console `dialog`; one started during another waits in a queue
- [x] While it runs: the player and the NPC are held (`Held`; tile movers finish their step), the NPC turns to the player, autosaves wait; `entity:Move` / `entity:IsHeld` for scripts
- [x] Lua hook `OnDialogEnd(id, choice)` on the NPC's script and the scene script; events `engine.dialog.started`, `.line`, `.choice`, `.ended`
- [x] `FacingAnimation` component: four-way idle / walk clips from facing and movement (so a held NPC visibly turns)
- [x] `arcbit-loc` reads dialog files (`--dialogs`): their English in coverage and templates, their problems reported (errors fail `check`), and translations English no longer has flagged - for any key, not just dialogs
- [x] Demo: Elder Maren in the Scripted demo - a greeting, choices, a gold check, a gift (`call` into her script), a map to buy, remembered on the next talk - and the guard, who knows whether you've met her; English, French and Japanese
- [x] Dialog placeholders read an unsaved key as 0, as conditions do ("You have {gold} gold" before any gold)
- [x] **Back out of a conversation** with Esc / gamepad East: on a choice, picks the option marked `"cancel": true` (its actions and goto run); on a line, ends the conversation (`OnDialogEnd` with choice `""`). Cancelable by default - `"cancelable": false` per dialog (story scenes) and per project
- [ ] Real dialog art from an artist (replacing the placeholder frame, plate, arrow, cursor and blip)

---

## Phase 28: Localization ✓
*English is required. All other languages are optional. The system is always active — it is the canonical way to define any player-visible text. See [docs/localization.md](docs/localization.md).*

- [x] String key convention: player-visible text is authored as a key (`"ui.pause.title"`); the engine and every demo string are keys
- [x] Locale files: one JSON per language (`en.json`, `fr.json`, `ja.json`, `pt-BR.json`) in the engine's and the game's folders, the game's winning; nested keys; a `$locale` block (name, fonts)
- [x] `Loc` — per-language tables, switched live (`Application::SetLanguage`); `Loc::Get` with the fallback chain language → base language → English → key
- [x] UTF-8 text everywhere: drawing, measuring, wrapping (CJK, kinsoku), text input
- [x] Fonts: a wider default glyph set, glyphs baked on demand, fallback faces per language, OpenType (CFF) fonts in SDF text, a visible missing-glyph box
- [x] Plural forms by CLDR category (`zero` / `one` / `two` / `few` / `many` / `other`) with built-in rules for European, Slavic, CJK and Arabic languages
- [x] Named placeholders (`{index}`), escaped braces; engine screens' glued strings moved to keys
- [x] Widgets keep their keys (`text_key`, `label_key`, `placeholder_key`, `item_keys`) and follow the language; `engine.locale.changed`
- [x] Language: the OS's on first launch (English if the game lacks it), then the player's choice, saved; a dropdown in the Game settings screen
- [x] Lua: `Loc.Get / Format / Interpolate / Has / Language / Languages / SetLanguage`, widget `TextKey`
- [x] Tools: `arcbit-loc` (check / coverage / template), the `qps` pseudo-locale, console `lang` and `loc.missing`, hot reload of locale files
- [x] French and Japanese for the engine and the demos (Noto Sans JP)
- [x] Numbers and dates in each language's style: separators and grouping, date / time patterns, month names (`loc.*` keys); `{n:n2}`, `{saved:datetime}`, `{year:raw}`
- [ ] Right-to-left text and complex shaping: Phase 42
- [ ] Editor integration (Phase 40): inline key picker, missing-key warnings, locale coverage report (the editor runs `LocCheck`)

---

## Phase 29: Inventory & Items
- [x] Item definition (ID, name, description, icon, stat block, type flags: consumable / equipment / key); all player-visible strings use localization keys (Phase 28) - `.arcitems` files, `extends`, English in the file (`docs/items.md`)
- [ ] Inventory kinds a game picks per inventory, without building one: **grid** (Minecraft: slots, optional hotbar section), **pockets** (Pokemon: a list per category), **weight** (Skyrim: category-filtered list, carry weight), **survival** (grid + carry weight); the project picks the party bag's default
- [ ] Grid items can take more than one cell (`"size"`) and rotate by default; every kind sorts and filters; max stack per item, defaults per type and project
- [x] Inventories are game state (the party's bag, a stash), saved as their own section; scene entities that hold items (chests) have a `Container`
- [ ] Changing a bag (bigger grid, more weight, another kind) moves the contents across; when the new one holds less, the player discards first, `junk`-tagged items picked for them
- [ ] Carry weight over the limit refused by default; a project can allow it (encumbered: slower)
- [ ] Quick slots for any kind (item references bound to keys, a HUD bar)
- [x] Currencies: `SaveData` counters by default (`gold`), or an item stack per currency; one API for both
- [ ] A party (brought forward from Phase 31): several members, a party of one by default; each with stats and equipment; the scene's player entity shows the leader
- [ ] `Stats` (brought forward from Phase 31): an engine default set (HP, MaxHP, MP, MaxMP, ATK, DEF, MAG, RES, SPD, LUK) a game's stats file extends or replaces
- [ ] Containers open into a transfer screen by default (take all per project / per container)
- [ ] Dialog conditions and actions on items (`has`, `give`, `take`); console `give`, `items`, `party`
- [ ] Pick-up / drop / equip / unequip logic; equip slots (weapon, armor, accessory, etc.)
- [ ] **Grid view**: icon-based inventory panel (like classic RPGs); drag-and-drop reorder; drag and drop and tooltips added to the GUI for it
- [ ] **List view**: name + quantity rows (Pokémon-style); keyboard/gamepad navigable
- [ ] Both views built on the GUI system (Phase 21); a grid inventory can switch to a list view at runtime
- [ ] Default look is game art, not the flat UI skin: a textured panel with slot sprites (Minecraft-style), nine-slice where it stretches, shipped as stock engine art and replaceable per game
- [ ] Equipment stat application: `EquipmentSystem` aggregates equipped item bonuses onto a `Stats` component
- [ ] Loot tables: weighted random drops defined in JSON
- [x] Save / load integration (Phase 25)
- [x] Lua API: add/remove/check items from scripts
- [ ] Publishes `engine.item.picked_up` and `engine.item.dropped` (item id, entity) via the Event Bus (Phase 24)

---

## Phase 30: AI & Pathfinding
- [ ] `NavGrid` — walkability grid derived from tilemap solid flags; rebuilt on map mutation
- [ ] A\* pathfinding with diagonal movement option; path smoothing (string-pulling)
- [ ] Steering behaviors: `Follow` (track target), `Wander` (random walk), `Flee` (move away from threat)
- [ ] `AIController` ECS component — configurable behavior tree or state machine
- [ ] Line-of-sight check (raytrace against solid tiles; used for perception radius)
- [ ] Per-entity sighting event through `EntityEvents` (Phase 24): an opted-in NPC announces when it sees the player ("on see"), renameable per NPC
- [ ] Patrol paths: series of waypoints defined per NPC in the editor
- [ ] Awareness states: `Idle → Alert → Chase → Combat` transitions
- [ ] Turn-based AI: action selection (attack / use item / move) driven by heuristics or Lua script
- [ ] Real-time AI: target acquisition, attack range check, cooldown management (used in Phase 32)
- [ ] Debug overlay: draw paths, awareness radius, LOS rays
- [ ] Dialog respects: AI leaves entities held in a dialog (Phase 27) alone - hostile AI doesn't target or attack the player mid-conversation - unless the game opts in

---

## Phase 31: Turn-Based Combat
- [ ] Battle state machine (idle → select action → resolve → end check)
- [ ] Action queue (Pokémon-style turn order by speed stat)
- [ ] Move / stat system (HP, MP, ATK, DEF, SPD, type affinities)
- [ ] Status effects (poison, stun, burn, etc.) applied and ticked per turn
- [ ] Combat UI (health/MP bars, action menu, move list, enemy info panel); all text via localization keys (Phase 28)
- [ ] Transition in/out of battle (trigger from overworld collision or script)
- [ ] Basic combat animations (Phase 17 clip playback) for attack and hurt reactions
- [ ] Rewards: XP, gold, item drops via loot tables (Phase 29)
- [ ] Lua hooks: `OnBattleStart`, `OnTurnStart`, `OnActionResolved`, `OnBattleEnd`; entity hooks `OnCombatStart` / `OnCombatEnd` (moved from Phase 26)
- [ ] Publishes `engine.combat.started` and `engine.combat.ended` via the Event Bus (Phase 24)

---

## Phase 32: Live Combat
*Alternative to turn-based — toggled per encounter or globally by the game dev.*

- [ ] Real-time hitbox system: attack hitboxes active for specific animation frames (Phase 17 frame events)
- [ ] Melee combat: swing arc collider, hit detection, knockback impulse
- [ ] Ranged combat: projectile entity spawned at fire point, moves each tick, destroyed on hit or range
- [ ] Dodge / dash: brief i-frame window, directional impulse
- [ ] Enemy AI in real-time (Phase 30): target acquisition, attack wind-up telegraphing, retreat at low HP
- [ ] Combo system: input sequence maps to chained attacks (optional; data-driven per weapon type)
- [ ] Screen effects: hit-stop (brief time-scale reduction), screen flash, camera shake (Phase 15)
- [ ] Boss encounter flag: phase transitions at HP thresholds, scripted via Lua
- [ ] Lua entity hooks `OnCombatStart` / `OnCombatEnd` when an entity enters or leaves a fight (moved from Phase 26)
- [ ] Publishes `engine.combat.started`, `engine.combat.ended`, `engine.player.died` and `engine.player.respawned` via the Event Bus (Phase 24); per-entity death through `EntityEvents` (a renameable "died" event, opt-in per enemy)

---

## Phase 33: Particle System
- [ ] CPU-side particle pool: position, velocity, lifetime, color/alpha, scale — fixed-size ring buffer
- [ ] Emitter component: shape (point, line, circle, cone), burst vs. continuous mode, rate
- [ ] Per-particle modifiers: gravity, drag, color-over-lifetime, scale-over-lifetime
- [ ] Render: batched into sprite batcher as textured quads; sorted by layer
- [ ] Preset library: spark, smoke, blood splash, magic aura, footstep dust, rain, snow
- [ ] Lua API: spawn named emitter at world position, stop/pause emitter
- [ ] Editor integration: preview emitters in the editor canvas (Phase 40)

---

## Phase 34: Visual Effects
*Renderer changes required: offscreen render target, multi-pass `RenderFrame`, per-sprite shader variant selection, line renderer vertex path.*

- [ ] **Post-processing pipeline**: scene rendered to an offscreen color target; fullscreen passes applied before presenting; each pass individually toggleable and configurable in `project.arcbit`
- [ ] Built-in post-processing effects: bloom (bright pixel extraction + Gaussian blur + additive composite), vignette, chromatic aberration, color grading via a 1D LUT texture
- [ ] **Light animation**: `LightAnimator` component — curve-driven intensity, color, and radius over time; built-in presets: flicker (fire / broken lamp), pulse (magic aura), candle, strobe; custom curves authored in the editor (Phase 40)
- [ ] **Sprite material effects**: per-sprite `MaterialEffect` field on the `Sprite` struct; built-in variants:
  - `Dissolve` — noise-based alpha cutoff controlled by a threshold parameter; used for death / spawn transitions
  - `Outline` — renders a colored rim around the sprite boundary; used to show silhouettes of entities occluded behind walls
  - `HitFlash` — RGBA tint pulse driven by a timer; wired to the combat hurt reaction
- [ ] **Line renderer**: `DrawLine(start, end, width, color, style)` for lasers, lightning, and ropes; rendered as a screen-aligned quad with SDF-based anti-aliasing; style variants: solid, dashed, glow
- [ ] **Weather overlay**: `WeatherSystem` drives a configurable overlay layer — rain (particle shower + screen-space streak pass), snow (particle drift), fog (animated Perlin noise fullscreen overlay with density and tint); weather type changes publish `engine.weather.changed` via the Event Bus (Phase 24)

---

## Phase 35: Cutscene & Event Sequence System
- [ ] `EventSequence` — ordered list of timed commands (timeline-style)
- [ ] Command types: `MoveCameraTo`, `PanCamera`, `PlayAnimation`, `PlaySound`, `PlayVideo`, `ShowDialog`, `FadeScreen`, `WaitSeconds`, `WaitForInput`, `SetFlag`, `RunLua`, `PublishEvent`
- [ ] `EventTrigger` tile/entity property — starts a named sequence when entered or interacted with
- [ ] Sequence authoring in JSON; imported and previewed in editor (Phase 40)
- [ ] Skippable flag per sequence (skip jumps to end, fires all `SetFlag` / `RunLua` / `PublishEvent` commands immediately)
- [ ] Supports both in-engine cutscenes and background story events (no camera override needed)

**Video playback** — pre-rendered cutscenes, an animated engine splash intro (Phase 23 boot flow), looping title backgrounds
- [ ] Choose codec + container with licensing in mind: royalty-free formats (e.g. VP9 / AV1 in WebM, or Theora) over H.264 / H.265; prefer a permissively licensed decoder over linking all of FFmpeg
- [ ] `VideoPlayer` — decode on a worker thread, upload frames into a streaming texture, route the audio track through `AudioManager`, keep video synced to the audio clock
- [ ] Fullscreen `VideoScreen` (skippable, `BlocksGame`) and a `Video` UI widget for in-layout playback
- [ ] `PlayVideo` sequence command (above) and a video entry type for the splash sequence

---

## Phase 36: Content Tools
*A shared `arcbit-content` DLL consumed by both the engine and the AvaloniaUI editor, ensuring identical read/write logic for all binary asset formats.*

- [ ] `.arcasset` binary container format: magic bytes, version, table of contents, compressed payload sections
- [ ] Supported asset types in v1: `Texture` (raw pixels + metadata), `SpriteSheet` (texture + UV table), `AnimationClip` (frame list + events), `TileAtlas` (texture + tile property table), `AudioClip` (decoded PCM + loop points), `FontAtlas` (glyph texture + metrics), `UILayout` (compiled `.arcui` data blob)
- [ ] **Texture atlas packer**: bin individual sprites and spritesheets into power-of-two atlas pages grouped by sampler type (nearest / linear); output UV mapping table in `.arcasset` so game code still refers to sprites by name; `arcbit-pack` supports `--atlas` mode for batch packing
- [ ] Import pipeline: source file (PNG, JSON, WAV, TTF, `.arcui`, etc.) → validated → packed into `.arcasset`
- [ ] Export pipeline: `.arcasset` → engine `TextureHandle` / `AudioClip` / `FontAtlas` / etc. at runtime
- [ ] **Migration — font registry**: `UILoader::FontRegistry` (currently populated by manual `UILoader::RegisterFont()` calls at startup) should be replaced by asset-driven loading — `FontAtlas` assets are loaded through the content pipeline and auto-registered under their asset key so `.arcui` skin blocks can reference them by name without any hand-wiring in game code
- [ ] **Migration — UI texture references**: `NineSliceButton`, `NineSliceProgressBar`, and `Image` widgets currently require texture/sampler handles to be assigned in code (no asset registry exists yet); once the content pipeline is in place, `.arcui` files should support a `"texture": "ui/button_normal"` key that resolves to a `TextureHandle` through the asset registry at load time
- [ ] **Everything through arcbit-pack**: `.arcui` layout files, tilemap atlases, fonts, and all other engine-consumed data must be packed and loaded exclusively via the `arcbit-pack` / `.arcasset` pipeline — no raw file reads at runtime outside of the content tools themselves
- [ ] **Two-key asset protection system**:
  - *Dev key*: generated per project, stored in the dev's `project.arcbit` (never shipped with the game); baked into the game binary by the packaging step at build time; required to open `.arcasset` files in full editor mode
  - *Mod key*: a separate per-project key included in the shipped `project.arcbit` only when `mod_support = true`; used to sign assets produced by end users in mod editor mode; engine distinguishes mod assets from dev assets by key
  - When `mod_support = false`, shipped builds contain no key material; `.arcasset` files are opaque to end users (obfuscation, not hard encryption — raises the bar for casual extraction)
- [ ] **Save keys**: replace `SaveManager::SetSealKey`'s per-game secret with the project keys - saves sealed with the dev key, or the mod key when `mod_support = true` (Phase 25)
- [ ] **Mod editor mode** (activated when the editor opens a shipped `project.arcbit` with `mod_support = true`):
  - Open and preview original dev assets (decrypted in memory via the mod key where possible, or by asking the game runtime)
  - Extract audio and image assets to standard formats (WAV, PNG, etc.) for use as mod source material
  - "Save" is disabled on original assets — only "Save as Mod" is available, which always writes to the project's `mods/` directory signed with the mod key; original `.arcasset` files are never overwritten
- [ ] Hot-reload: engine watches `.arcasset` mtime and re-imports on change (extends Phase 12 stub)
- [ ] **Scene load data at pack time**: `arcbit-pack` rewrites each scene's `"load"` block (Phase 23) from the packed assets - exact sizes, never stale in a shipped build; the packed scene format puts it in a real header so it's read without parsing the whole scene, and its texture list lets the loader start decoding before discovery
- [ ] CLI tool (`arcbit-pack`) for batch import from a source assets directory

---

## Phase 37: Mod Support
*Optional — enabled per project in `project.arcbit`.*

- [ ] Mod discovery: scan a `mods/` folder for subdirectories, each containing a `mod.json` manifest
- [ ] `mod.json`: mod ID, display name, version, load order, list of Lua entry scripts
- [ ] Load order resolution: mods declare soft dependencies; engine topologically sorts them
- [ ] Lua mod API extensions (on top of Phase 26): `Entity.Spawn`, `Tilemap.SetTile`, `ItemRegistry.Register`, `DialogRegistry.Register`, `Event.Subscribe`
- [ ] Asset override: mod can shadow engine assets by placing an `.arcasset` at the same relative path; mod assets must be signed with the mod key (produced by the mod editor) — the engine rejects any file signed with the dev key that did not ship with the game
- [ ] Conflict detection: warn when two mods override the same asset or define the same item ID
- [ ] Mod can be disabled at runtime; save system tags which mods were active (warn on load if missing)
- [ ] Steam Workshop integration: wires into mod discovery when `steam_workshop = true` in `project.arcbit` (requires Phase 39)

---

## Phase 38: Data-Driven Runtime
- [ ] `project.arcbit` JSON project file format (title, resolution, entry scene, asset/script paths, mod support flag)
- [ ] `runtime/` CMake project — generic, pre-compiled Application host with no game-specific code
- [ ] Project file loader replaces hardcoded `ApplicationConfig` in the runtime path
- [ ] Lua hook wiring: `OnStart` loads entry scene, `OnUpdate(dt)` dispatches to Lua `OnUpdate`, etc.
- [ ] Defined export folder layout (`runtime.exe`, `project.arcbit`, `assets/`, `scenes/`, `scripts/`, `shaders/`, `mods/`)
- [ ] C++ subclass path (`game/`) remains fully supported alongside the data-driven path

---

## Phase 39: Steam / Platform Integration
*Optional — enabled per project. Requires a Steamworks SDK license (free for Steam distribution).*

- [ ] Steamworks SDK integration (dynamic DLL load; gracefully no-ops when Steam is not running)
- [ ] Steam initialization: `SteamAPI_Init` on startup, `SteamAPI_RunCallbacks` each tick, `SteamAPI_Shutdown` on exit
- [ ] Achievements: `AchievementManager` with named achievements defined in `project.arcbit`; unlock via `AchievementManager::Unlock("ACH_FIRST_BATTLE")`; synced to Steam backend; publishes `engine.achievement.unlocked` via Event Bus (Phase 24)
- [ ] Stats: integer / float stats (playtime, enemies defeated, etc.) submitted to Steam leaderboards
- [ ] Cloud saves: `SteamRemoteStorage` mirrors the save slot files from Phase 25; conflict resolution (newest wins or user prompt)
- [ ] Rich presence: set activity string shown in Steam friends list (e.g. "Exploring the Forest")
- [ ] Steam overlay: ensure `SDL_Window` is correctly set up for the overlay to render (Vulkan surface compatible)
- [ ] DLC flag system: `SteamApps::BIsDlcInstalled(appId)` gates content; checked at asset load time
- [ ] Steam Workshop: full discovery + download pipeline (extends the stub in Phase 37)
- [ ] Build pipeline: `steam_appid.txt` present in output directory; packaging script generates depot manifests

---

## Phase 40: C# AvaloniaUI Editor
- [ ] Separate solution / CMake-independent project
- [ ] Tilemap canvas (render tiles, layer management)
- [ ] Tile palette + placement / erase tools
- [ ] Tile Objects: multi-cell, multi-layer stamps (a tree, a house) defined in the atlas and written out as plain tiles; per-cell property overrides. Design notes: `docs/plans/tilemap-future.md`
- [ ] Tile property editor panel (solid, light-blocking, interactable, custom properties)
- [ ] NPC placement + script file assignment
- [ ] Dialog graph editor (node canvas for Phase 27 conversation trees)
- [ ] Event sequence editor (timeline view for Phase 35 cutscene commands)
- [ ] GUI layout designer (drag-and-drop widget canvas for Phase 21 screens)
- [ ] **Font registry panel** (part of asset browser): register loaded `FontAtlas` instances under string keys so `.arcui` skin blocks can reference fonts by name (`"Font": "roboto"`); call `UILoader::RegisterFont(key, atlas)` at startup from game code or project config; editor needs UI to add/remove/rename entries and preview each registered font
- [ ] Particle emitter preview panel (live preview of Phase 33 emitter configs)
- [ ] Visual effects panel: light animator curve editor, weather overlay preview, material effect preview (Phase 34)
- [ ] Localization tools: key picker, `en.json` export, per-locale coverage report (Phase 28)
- [ ] **Run button**: builds the game and launches it in its own window; optionally opens directly to a selected scene (useful when iterating on a specific area)
- [ ] **Scene launch selector**: choose any registered scene as the start point before hitting Run
- [ ] **Measure scene loads on save**: when saving a scene, time a real load (prepare / decode / build) and write the shares and texture list into its `"load"` block (Phase 23), so the loading bar is even; flag scenes whose load data is stale
- [ ] **Engine log panel**: the running game pipes its spdlog output back to the editor over a named pipe or local socket; displayed in a filterable log panel in the editor
- [ ] **Event monitor panel**: live stream of Event Bus events from the running game (Phase 24); filterable by event name; shows payload and source entity
- [ ] Content export to Phase 38 folder layout (`project.arcbit` + `.arcasset` files via Phase 36 content tools)
- [ ] Asset importer UI: drag source files in, editor calls `arcbit-pack` and shows result
- [ ] **Save settings**: per-project slot counts, the saving indicator icon / tint / duration, the saving notice (never / first launch / every launch) and autosave on scene changes (`SavingConfig`, Phase 25)
- [ ] **Save editing in mod mode**: open, edit and reseal a player's `.arcsave` with the mod key (Phase 25, Phase 36)
- [ ] **Persistence in the scene editor**: `Persistent` and `persist_all` as checkboxes, with the component list picker (Phase 25)
- [ ] **Art studio - pixel art for non-artists** (`docs/plans/40-editor.md`, 40I): a project palette with hue-shifted shading ramps everything draws in; a pixel editor (layers, tile-wrap preview, animation, export to the sprite format) and Aseprite import; tileable pattern generators (wood, stone, brick, grass, water...) quantized to the palette; photo to texture (seamless, downscaled, reduced to the palette); autotile transition sets from one texture; sketch cleanup - draw a scribble, get a sprite (strokes straightened and snapped to clean pixel slopes, symmetry, then shading by material: metal shine, glass highlights, wood grain; selective outline); your own parts saved into kits you can export, reuse and share - the engine bundles no art, so games don't share a look; items and paper-doll characters built from your kits, with equipment overlays (Phase 29); optional sketch recognition and AI plugins
- [ ] **Editor mode detection**: on open, inspect the loaded `project.arcbit` to determine mode — full dev mode (dev key present) or restricted mod mode (mod key only, `mod_support = true`); mod mode disables save-over-original and shows a clear "Mod Editor" banner so end users know what they are working with

---

## Phase 41: C# Scripting
*Optional, alongside Lua (Phase 26). C# is for a game's own trusted code: .NET has no sandbox, so mods (Phase 37) stay Lua-only. Comes after the editor (Phase 40), which brings .NET into the toolchain and can share the bindings.*

- [ ] Host .NET from the engine with `nethost` / `hostfxr`; load the game's assembly and call `[UnmanagedCallersOnly]` entry points through function pointers
- [ ] A language-neutral script runtime seam under `SceneScripts`: create an instance, call a hook, end an owner; Lua becomes one runtime, C# another
- [ ] A flat C API exported by the engine (entities, components by registered name, events, scenes, input, audio, saves, UI, console), JSON-based first, with typed fast paths (`Transform`, `Position`) where profiling asks for them
- [ ] `Arcbit.Scripting` C# library over that API: `Entity`, `Scene`, `Event`, `Input`, `Save`, and a script base class with `OnStart` / `OnUpdate` / `OnDestroy` / contact and interact hooks; async tasks in place of Lua's `self:Run` / `Wait`
- [ ] `Script` component names a C# class (`"module": "MyGame.Chest"`) as well as a Lua module; `self.Saved`-style saved state through a `[Saved]` field attribute
- [ ] Hot reload in debug builds through a collectible `AssemblyLoadContext`, carrying `[Saved]` fields across the reload
- [ ] Shipping: self-contained .NET runtime next to the game, or the game's C# compiled with NativeAOT into a native library (no runtime to ship; needed for consoles)
- [ ] Console commands and the error handler / toasts work the same as for Lua

---

## Phase 42: Right-to-Left Text & Complex Scripts
*Low priority - for when a game targets Arabic, Hebrew, Persian, Thai or the Indic languages. Builds on Phase 28's UTF-8 text and fonts.*

- [ ] Shaping with HarfBuzz (vcpkg): Arabic joining forms, ligatures, Indic reordering, Thai; the font atlas caches glyphs by face and glyph id rather than codepoint, and layout places shaped runs
- [ ] Bidirectional text (Unicode bidi algorithm - FriBidi or ICU): mixed right-to-left and left-to-right runs, numbers and names inside RTL sentences, mirrored brackets; wrapping that respects runs
- [ ] Text input in RTL and mixed text: cursor movement, selection and deletion by visual and logical order
- [ ] Mirrored UI for RTL languages: a locale's `"direction": "rtl"` flips anchors, alignment and list order on engine screens (and `.arcui` layouts that opt in)
- [ ] An RTL locale in the demos (Arabic or Hebrew) with a font that covers it, and `qps`-style pseudo-RTL for testing layouts
