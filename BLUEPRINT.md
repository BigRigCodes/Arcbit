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
- [x] Raw SDL events forwarded from SDL thread to game thread via a lock-free queue
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
- [ ] Persist only rebound actions — `SaveInputBindings` writes every action's bindings on shutdown, and `LoadInputBindings` replaces the code's defaults with them. So a default changed in code never reaches an existing settings file (hit in 22H: WorldDemo's broken stick bindings had to be deleted from `settings.json` by hand), and demos sharing one file inherit each other's bindings (TileMoveDemo picks up WorldDemo's arrow keys and stick). Snapshot the defaults at startup, save only actions that differ, and give actions with no saved entry their code defaults. A per-game settings path would also keep the demos apart

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

---

## Phase 19: Tilemap System
- [x] Tile definition (ID, solid flag, light-blocking, flat, uv_scroll, flip-book animation)
- [x] Chunk-based map storage (16×16 tile chunks)
- [x] Tile atlas texture + UV lookup
- [x] Tilemap renderer (batched instanced quads via Phase 14 sprite batcher)
- [x] Camera / viewport culling (only submit visible chunks)
- [x] Multiple layers (ground, objects, overlay; each layer rendered in order with Y-sort and flat-object support)
- [ ] **Two point light layers** — add `LightLayer` enum to `PointLight`: `World` (affected by tile occlusion) and `Overhead` (bypasses occlusion entirely); overhead lights are used for sunlight, moonlight, magical sky effects, and UI indicators
- [ ] **Tile occlusion grid** — pack `light-blocking` flags for the visible map area into a GPU texture; updated incrementally when tiles change; uploaded to the sprite shader as a new descriptor binding
- [x] **Shadow casting** — CPU DDA raycaster builds a 1D polar shadow map per light; sampled in the sprite fragment shader with 3-tap PCF; `BlocksLight` tile flag drives occlusion
- [ ] Soft shadow quality option: configurable penumbra sample count in `project.arcbit` (default: hard shadows; higher counts add a blur fringe for performance/quality tradeoff)

---

## Phase 20: Font Rendering
- [x] Font rasterizer integration (`stb_truetype`)
- [x] Bitmap font atlas generation at startup (bake glyphs into a `TextureHandle`)
- [x] `DrawText` helper — lays out a string into sprite-batch quads with correct UVs; supports `\n`, `\t`, and `TextAlign` (Left / Center / Right)
- [x] SDF font variant — dedicated screen-space pipeline; engine ships Roboto-Regular for the built-in debug overlay
- [x] Debug overlay — FPS, sprite/batch counts, draw calls, lights, chunk culling stats; toggled via `Settings::Graphics.ShowDebugInfo`

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
- [ ] Dialog screen (Phase 27), Inventory screen (Phase 25)

---

## Phase 22: 2D Collision
*Custom kinematic-first collision system — no rigid-body dynamics. Full design
in `docs/physics.md`. Covers tile collision, sub-tile entity colliders,
directional arc gating, optional rotation, triggers, and per-style movement
integration. Box2D was reconsidered and rejected — see `docs/physics.md`
"Library Choice" for rationale.*

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
*Parked: two of three items wait on Phase 24 (Event Bus) and Phase 40 (editor). Execution order after 22F is **22H**, then the partial-move item here; the rest closes out when its dependency lands.*
- [ ] Optional `SmoothTileMovement.AllowPartialMove` — sub-tile stop policy for the rare scenes that want it
- [ ] Event Bus publishing (`Collision.Started`, `Collision.Ended`, `Trigger.Entered`, `Trigger.Exited`) — wired when Phase 24 lands; callback API is the integration surface until then
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
- [x] One `.arcscene` format — reconciled into `docs/scenes.md`: id-keyed entities with inline components (sorted on save for merge safety), a referenced `.arcmap`, only non-default fields written; templates / overrides reserved for prefabs
- [x] Component registry — `SceneRegistry` maps component name ↔ C++ type ↔ JSON read / write; the engine registers its 20 built-ins, games add their own
- [x] `.arcscene` loader and saver — `SceneFile::Load` / `Save` (headless, through a `SceneAssets` interface) and `FileScene` on top; file id → `Entity` table (`FindEntity`); exact load → save round trip tested
- [x] Named systems — game code registers installers by name; a scene file lists the ones it uses
- [x] At least one demo converted to an `.arcscene` — "Physics File" (`game/assets/scenes/physics.arcscene` + `maps/physics.arcmap`) runs with no C++ scene class
- [x] Texture drawn with two samplers in one frame — the Vulkan backend kept one descriptor set per texture and rewrote it when the sampler changed, which invalidated the frame's command buffer; now one set per (texture, sampler) pair, written once (found by the scene-file demo sharing the engine's white texture with the UI)

**Boot flow** — engine splash → game splash(es) → title screen → gameplay
- [x] First scene loads under the splash — scene swaps run while a game-blocking screen covers loading (`UIScreen::CoversLoading`; the splash only while its image is fully shown), instead of after the splash. The scene's files load on the loader thread while the splash animates (`OnPrepare`); only the short `OnLoad` runs under the logo
- [x] Hand-off into the first scene fades in — fades hold while the scene is hidden, so it fades in from black as the splash ends (skipping too)
- [x] Engine splash is engine-owned: its content and timing are defined inside the engine (`ApplicationBoot.cpp`), not exposed to games. Games append their own splash entries after it (`BootConfig::SplashImages`) but can't replace, reorder, or remove it (hardened in packed builds by the Phase 36 asset protection)
- [ ] Engine splash entry can be a video once Phase 35 video playback lands (animated intro)
- [x] Title screen as the default first stop after the splashes — `BootConfig` (`FirstScene`, `ShowTitle`, `TitleScene`, `CreditsPath`) drives splash → title → game; the engine wires the title's buttons to its own settings / credits screens
- [x] Credits screen — `CreditsScreen` scrolls a JSON file (title, sections of heading + names, speed), reachable from the title menu; `Application::ShowCredits` can roll it at the end of a game too
- [ ] Credits logos — image entries in the credits roll

**Docs**
- [x] `docs/scenes.md` covers the whole phase - lifecycle (hook order and threads), stack, swap rules, loading ahead and progress, boot flow, the `.arcscene` format with its `"load"` block, registry, named systems, asset ownership; `docs/ecs.md`'s Scene, removal-hook, Application-integration and file-layout sections brought up to date

---

## Phase 24: Event Bus
- [ ] Named pub/sub message bus: `EventBus::Subscribe(eventName, callback)` / `Unsubscribe` / `Publish(eventName, payload)`
- [ ] Typed payload: `EventPayload` — flexible container for arbitrary per-event data (discriminated union or `std::any`)
- [ ] Deferred dispatch mode: events queued during the update tick and flushed at the start of the next tick to prevent re-entrant callback issues
- [ ] Predefined engine events:
  - *Scene:* `Scene.Loaded`, `Scene.Unloaded`
  - *Persistence:* `Game.Saved`, `Game.Loaded`
  - *Entities:* `Entity.Spawned(entityId)`, `Entity.Destroyed(entityId)`
  - *World:* `DayNight.Changed(normalizedTime)`, `Weather.Changed(type)`
  - *Dialog:* `Dialog.Started(graphId)`, `Dialog.Ended(graphId, lastChoice)`
  - *Combat:* `Combat.Started`, `Combat.Ended(result)`, `Player.Died`, `Player.Respawned`
  - *Input:* `Input.DeviceChanged(deviceType)` — e.g. keyboard → gamepad, used to swap UI hint icons
  - *Items:* `Item.PickedUp(itemId, entityId)`, `Item.Dropped(itemId, entityId)`
  - *Progression:* `Quest.Started(questId)`, `Quest.Completed(questId)`, `Player.LeveledUp(newLevel)`, `Achievement.Unlocked(achievementId)`
- [ ] User-definable events: any string key outside the reserved `engine.*` namespace is valid; game code and Lua scripts may publish and subscribe freely
- [ ] Lua integration: `Event.Subscribe("name", handler)`, `Event.Unsubscribe`, `Event.Publish("name", payload)` — exposed in Phase 26 Lua scripting
- [ ] Editor integration: live event monitor panel in the running game showing event name, payload, and source (Phase 40)

---

## Phase 25: Save / Load System
- [ ] `SaveSlot` — versioned binary or JSON snapshot of game state (separate from settings)
- [ ] Captured state: player position + scene, inventory, quest flags, NPC states, world mutations
- [ ] Multiple save slots with metadata (timestamp, playtime, thumbnail screenshot)
- [ ] Auto-save trigger API (on scene transition, on rest, on checkpoint)
- [ ] Migration / version-upgrade path so old saves stay compatible after content updates
- [ ] Lua API exposure so scripts can read and write custom save keys
- [ ] Publishes `Game.Saved` and `Game.Loaded` via the Event Bus (Phase 24) on completion

---

## Phase 26: Lua Scripting
- [ ] Lua integration (sol2 via vcpkg)
- [ ] Script component (path to `.lua` file, loaded via asset system)
- [ ] Core lifecycle hooks: `OnStart`, `OnUpdate(dt)`, `OnDestroy`
- [ ] Entity hooks: `OnInteract`, `OnCombatStart`, `OnCombatEnd`
- [ ] `OnEvent(eventName, payload)` — called when a subscribed event fires; scripts call `Event.Subscribe` in `OnStart` to opt in
- [ ] Engine API surface: ECS queries, entity create/destroy, component read/write
- [ ] Tilemap API: read/write tile properties, trigger map mutations
- [ ] Scene API: load scene, change entry point, spawn entities
- [ ] Save API: read/write custom save keys (Phase 25)
- [ ] Input API: query action state from scripts
- [ ] Audio API: play sound/music from scripts
- [ ] Event API: `Event.Subscribe`, `Event.Unsubscribe`, `Event.Publish` (Phase 24)
- [ ] Sandboxed execution: scripts cannot access filesystem or network directly

---

## Phase 27: Dialog / Conversation System
*Default implementation — can be replaced by a Lua script if the game dev prefers a custom system.*

- [ ] `DialogGraph` — node-based conversation tree (speak, choice, condition, branch, action)
- [ ] Node types: `SpeakNode` (portrait, speaker name, text), `ChoiceNode` (up to N options), `ConditionNode` (check quest flag / inventory / stat), `ActionNode` (run Lua callback)
- [ ] `DialogManager` — queue and advance dialogs; integrates with GUI system (Phase 21) for the dialog box UI
- [ ] Interactable tile and NPC trigger: `OnInteract` starts a named dialog graph
- [ ] All displayed text referenced by localization key (Phase 28); raw strings are never embedded
- [ ] Variable substitution in text (`{player_name}`, `{gold_count}`, etc.) resolved via Localization interpolation
- [ ] Typewriter reveal effect with configurable speed; skip on confirm input
- [ ] Portraits / speaker icons per node (optional; falls back to name-only if absent)
- [ ] JSON authoring format; importable from editor (Phase 40)
- [ ] Lua hook: `OnDialogEnd(graphId, lastChoice)` for scripted reactions
- [ ] Publishes `Dialog.Started` and `Dialog.Ended` events via the Event Bus (Phase 24)

---

## Phase 28: Localization
*English is required. All other languages are optional. The system is always active — it is the canonical way to define any player-visible text.*

- [ ] String key convention: all player-visible text in dialog, GUI, items, and combat is authored as a key (e.g. `"npc.elder.greeting"`) rather than a raw string
- [ ] Language resource files: one JSON file per locale (`en.json`, `ja.json`, etc.); keys map to translated strings
- [ ] `LocaleManager` — loads a locale file at startup (default: `en`); hot-swappable at runtime for in-game language selection
- [ ] `L("key")` / `Localize("key")` helper resolves a key to the active locale string; falls back to English if the key is missing in the target locale
- [ ] Fallback chain: requested locale → English → key string itself (so missing translations never crash or show blank)
- [ ] Plural forms: keys can define `_one` / `_other` variants (e.g. `"item.gold_one"` = "1 Gold", `"item.gold_other"` = "{count} Gold")
- [ ] String interpolation: `{player_name}`, `{count}`, `{item_name}` placeholders resolved at call site
- [ ] **Export pipeline**: editor scans all dialog graphs, GUI layouts, item definitions, and combat text; emits a canonical `en.json` with every key and its English value; translator-ready
- [ ] Language selection UI in settings screen (Phase 21); persisted in settings file (Phase 10)
- [ ] Editor integration (Phase 40): inline key picker, missing-key warnings, locale coverage report (% of keys translated per locale)

---

## Phase 29: Inventory & Items
- [ ] Item definition (ID, name, description, icon, stat block, type flags: consumable / equipment / key); all player-visible strings use localization keys (Phase 28)
- [ ] `Inventory` component — item slots with quantities; max-stack configurable per item type
- [ ] Pick-up / drop / equip / unequip logic; equip slots (weapon, armor, accessory, etc.)
- [ ] **Grid view**: icon-based inventory panel (like classic RPGs); drag-and-drop reorder
- [ ] **List view**: name + quantity rows (Pokémon-style); keyboard/gamepad navigable
- [ ] Both views built on the GUI system (Phase 21) and switchable at runtime
- [ ] Equipment stat application: `EquipmentSystem` aggregates equipped item bonuses onto a `Stats` component
- [ ] Loot tables: weighted random drops defined in JSON
- [ ] Save / load integration (Phase 25)
- [ ] Lua API: add/remove/check items from scripts
- [ ] Publishes `Item.PickedUp` and `Item.Dropped` events via the Event Bus (Phase 24)

---

## Phase 30: AI & Pathfinding
- [ ] `NavGrid` — walkability grid derived from tilemap solid flags; rebuilt on map mutation
- [ ] A\* pathfinding with diagonal movement option; path smoothing (string-pulling)
- [ ] Steering behaviors: `Follow` (track target), `Wander` (random walk), `Flee` (move away from threat)
- [ ] `AIController` ECS component — configurable behavior tree or state machine
- [ ] Line-of-sight check (raytrace against solid tiles; used for perception radius)
- [ ] Patrol paths: series of waypoints defined per NPC in the editor
- [ ] Awareness states: `Idle → Alert → Chase → Combat` transitions
- [ ] Turn-based AI: action selection (attack / use item / move) driven by heuristics or Lua script
- [ ] Real-time AI: target acquisition, attack range check, cooldown management (used in Phase 32)
- [ ] Debug overlay: draw paths, awareness radius, LOS rays

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
- [ ] Lua hooks: `OnBattleStart`, `OnTurnStart`, `OnActionResolved`, `OnBattleEnd`
- [ ] Publishes `Combat.Started` and `Combat.Ended` events via the Event Bus (Phase 24)

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
- [ ] Publishes `Combat.Started`, `Combat.Ended`, `Player.Died`, and `Player.Respawned` events via the Event Bus (Phase 24)

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
- [ ] **Weather overlay**: `WeatherSystem` drives a configurable overlay layer — rain (particle shower + screen-space streak pass), snow (particle drift), fog (animated Perlin noise fullscreen overlay with density and tint); weather type changes publish `Weather.Changed` via the Event Bus (Phase 24)

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
- [ ] Achievements: `AchievementManager` with named achievements defined in `project.arcbit`; unlock via `AchievementManager::Unlock("ACH_FIRST_BATTLE")`; synced to Steam backend; publishes `Achievement.Unlocked` via Event Bus (Phase 24)
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
- [ ] **Editor mode detection**: on open, inspect the loaded `project.arcbit` to determine mode — full dev mode (dev key present) or restricted mod mode (mod key only, `mod_support = true`); mod mode disables save-over-original and shows a clear "Mod Editor" banner so end users know what they are working with
