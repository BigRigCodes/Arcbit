# Architecture

How Arcbit is put together: the libraries it builds, the threads it runs, how
a frame gets drawn, and why the big choices went the way they did. The other
docs cover each system; this is the map.

---

## Targets

```
core/              arcbit-core     (shared)  types, logging (spdlog), asserts, hashing, profiler stub
backends/vulkan/   arcbit-vulkan   (shared)  the RenderDevice implementation - all Vulkan lives here
engine/            arcbit-engine   (static)  everything else: app loop, ECS, scenes, UI, audio, ...
game/              arcbit-game     (exe)     the demo app - one Application subclass, many demo scenes
tests/             arcbit-tests    (exe)     Catch2 unit tests, headless
tools/loc/         arcbit-loc      (exe)     locale checking and templates (docs/localization.md)
```

A game is an `Application` subclass in its own executable linking
`arcbit-engine`; `main` constructs it and calls `Run()`. Engine assets (UI
layouts, fonts, the default skin, locale files, dialog art) are copied beside
the executable under `assets/engine/`; a game's go under `assets/`.

### The render boundary

`RenderDevice` (`engine/include/arcbit/render/RenderDevice.h`) is the only way
engine and game code reach the GPU: buffers, textures, samplers, shaders,
pipelines, command recording, swapchains, all through typed generational
handles (`TextureHandle`, `BufferHandle`, ...). No Vulkan type crosses it - the
implementation, `VulkanContext` and everything under it, is inside the
`arcbit-vulkan` library, which creates the device (`Arcbit_CreateDevice`).
Another backend (OpenGL, DX12) would be another library implementing the same
class; none exists yet.

Vulkan 1.3 is required, with dynamic rendering and synchronization2 (no render
pass objects), memory through VMA.

---

## Threads

| Thread | Does |
|--------|------|
| main | window and input events (SDL), the fixed-tick game loop - ECS, scripts, UI, audio commands - and building each frame's `FramePacket` |
| render | `RenderThread`: turns a `FramePacket` into command buffers, submits and presents |
| scene loader | a scene's `OnPrepare` - file reads and image decodes - while the current scene keeps running (docs/scenes.md) |
| audio | miniaudio's own mixing thread |

Lua, the ECS, the event bus and the UI are main-thread only.

**Main and render** hand frames over through `RenderThread::SubmitFrame`: the
main thread builds frame N+1 while the render thread draws frame N, and
`SubmitFrame` blocks if N isn't done - so the game is never more than one frame
ahead of the GPU (bounded latency and memory). Creating or destroying GPU
resources mid-run - a scene swap, a texture hot reload - parks the render
thread first (`RenderThread::WaitIdle`).

---

## The loop

`Application::Run` (details in `docs/ecs.md`, "Integration with Application"):

- **Fixed tick** (default 60 Hz, accumulated from real time, capped so a stall
  can't spiral): input edges, the app's `OnUpdate`, scene transitions, the
  active scene's update (its hooks, interaction, scripts, then its systems),
  the event bus delivers, the UI updates.
- **Every frame**: the app's `OnRender`, the scene's render systems (tiles,
  sprites, lights) and the UI fill a `FramePacket`, which goes to the render
  thread.
- **Once a second** (debug builds): hot reload of changed textures, scripts,
  locale and dialog files.

---

## Drawing

Everything is a quad. `FramePacket` carries world sprites, lights and shadow
maps, SDF text and UI quads; the render thread sorts sprites by layer, groups
them by texture and draws each group instanced, in one forward pass:

- **World sprites** (`sprite.vert` / `.frag`): camera transform from push
  constants; each fragment samples its albedo and an optional normal map (a
  flat default when there's none), then loops over every light in a storage
  buffer - no fixed light count, no shader recompile for more. Lights that cast
  shadows add a row to a shadow storage buffer (`docs/tilemap.md`, "Shadows").
- **UI and text** (`ui.vert` / `.frag`): screen space, no lighting; plain
  textures and SDF glyphs in one pass, with outline, weight, glow and slant per
  quad (`docs/gui.md`).

Lights are culled to the view on the CPU before upload, so the per-fragment
loop stays short. Arcbit is forward-rendered, not deferred: alpha-blended
sprites (characters overlapping, particles, UI) need no extra passes, there's
one set of shaders and no G-buffer, and normal maps still work per sprite.
Tile-binned light culling on the GPU can come later if profiling asks.

Shading happens in linear space. Image files are color art, so every texture
`TextureManager` loads is uploaded as sRGB and sampling decodes it to linear;
every `Color` (tints, UI skins, text, lights, ambient, clear colors) is sRGB
too, like a color picker, and the render thread converts it with
`Color::ToLinear` before upload. The sRGB swapchain encodes the result once on
output. So a skin color of `[0.1, 0.1, 0.12]` shows as about 10% gray, as an
artist expects, and a white sprite under ambient `A` shows as `A`. Data
textures stay `UNorm`: the font atlas (a distance field) and normal maps.

Post-processing, render targets and the effects that need them are Phase 34.

---

## Choices, and why

- **Vulkan, from the start.** It's built for recording commands on a render
  thread; OpenGL's thread-bound context fights that. DX12 would close off
  Linux. An OpenGL fallback can be another backend behind `RenderDevice`.
- **SDL3** for the window, input and gamepads: better controller support than
  GLFW, which an RPG needs, and a straightforward Vulkan surface.
- **A custom archetype ECS** rather than EnTT: entities with the same
  components share contiguous arrays, which suits an RPG's mostly stable
  compositions; components are registered by name, so scene files, saves, Lua
  and the editor read any component without per-type code (`docs/ecs.md`,
  `docs/scenes.md`).
- **Custom kinematic collision** rather than Box2D: world pixels throughout,
  a tile-grid broadphase, plan-then-commit tile steps and directional arcs,
  none of a rigid-body solver's weight (`docs/physics.md`).
- **Lua 5.4 through sol2**, sandboxed, for game logic that shouldn't need a
  C++ build (`docs/scripting.md`); C# for a game's own trusted code is planned
  (Phase 41).
- **JSON** for every authored format (scenes, maps, atlases, sprites, UI,
  dialogs, locale) - diffable and hand-editable; `arcbit-pack` (Phase 36) will
  pack them into binary `.arcasset` files for shipping.
- **CMake + vcpkg** in manifest mode, with a pinned baseline and an overlay
  triplet for the toolset (README).
- **A C# AvaloniaUI editor** (Phase 40): XAML like WPF but cross-platform, with
  real tree views, docking and property grids that an in-engine ImGui editor
  would struggle with. Its design notes are in `docs/plans/`.
