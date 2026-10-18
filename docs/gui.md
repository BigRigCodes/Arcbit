# Arcbit — Phase 21: GUI / UI System

## Overview

The GUI system is a retained-mode, screen-space widget framework built on top of
the existing SDF text pipeline and sprite batcher. It handles all in-game UI:
HUD elements, menus, dialog boxes, inventory screens, and pause menus.

Guiding principles:
- **Screen-space only** — widgets render in pixel coordinates with no camera
  transform, matching the SDF pipeline convention.
- **No new GPU pipeline** — solid-color and textured quads reuse the existing UI
  sprite pipeline; a 1×1 white texture stands in for untextured quads.
- **Retained, not immediate** — widget trees are built once and mutated, not
  rebuilt every frame. Per-frame cost is proportional to what changed.
- **Data-driven layout** — widget trees are described in `.arcui` JSON files;
  behavior (callbacks, dynamic content) is wired in C++ after loading.

---

## Coordinate System

All UI positions and sizes are in **screen pixels**, origin at the **top-left**
corner of the window:

```
(0,0) ──────────────── (W, 0)
  │                       │
  │       screen          │
  │                       │
(0,H) ──────────────── (W, H)
```

Widget positions are expressed as an **anchor** on the parent (or screen) plus
a **pixel offset**. This lets the same layout adapt to different window sizes
without hardcoding pixel positions.

---

## Anchoring & Layout

Each widget has:

| Field | Type | Description |
|---|---|---|
| `Anchor` | `Vec2` | Normalized (0–1) point on the **parent** rect. `{0,0}` = top-left, `{0.5,0.5}` = center, `{1,1}` = bottom-right. |
| `Pivot` | `Vec2` | Normalized point on the **widget itself** that aligns to the anchor. Defaults to `{0,0}` (top-left). |
| `Offset` | `Vec2` | Pixel displacement added after anchor/pivot alignment. |
| `Size` | `Vec2` | Widget size in pixels. Can be overridden by `SizePercent`. |
| `SizePercent` | `Vec2` | If non-zero, overrides `Size` as a fraction of the parent's size (useful for bars, full-screen panels). |

---

## Widget Types

### `UIWidget` (base)

All widgets inherit from `UIWidget`. Core fields:

```cpp
std::string Name;           // optional; used by FindWidget<T>()
Vec2    Anchor      = {0, 0};
Vec2    Pivot       = {0, 0};
Vec2    Offset      = {0, 0};
Vec2    Size        = {100, 100};
Vec2    SizePercent = {0, 0};   // overrides Size per axis when > 0
f32     Opacity     = 1.0f;     // multiplied with children's opacity
bool    Visible     = true;
bool    Enabled     = true;     // disabled = not interactive, still drawn
i32     ZOrder      = 0;
bool    Focusable   = false;    // participates in keyboard/gamepad nav
```

### `Panel`

Solid-color background rect. Defaults to `skin.PanelBg`; override via the
per-widget `SkinOverride.PanelBg` (`background_color` in `.arcui`).

```cpp
bool DrawBorder = false;
```

### `Scrim`

Full-screen `Panel` (pre-set to `SizePercent={1,1}`, `Anchor={0,0}`) used as
a semi-transparent darkening overlay behind modal screens. Renders from
`skin.ScrimColor`; override via `SkinOverride.ScrimColor` (`background_color`
in `.arcui` is routed there).

### `NineSlice`

Scalable panel from a 9-patch texture. Source texture divided into 3×3 grid
by UV-space border fractions; corners are emitted at `Pixel*` sizes; center stretches.

```cpp
TextureHandle Texture; SamplerHandle Sampler;
Color Tint = {1,1,1,1};
f32 UVBorderLeft, UVBorderRight, UVBorderTop, UVBorderBottom; // 0–1 UV fractions
f32 PixelLeft, PixelRight, PixelTop, PixelBottom;             // screen pixels
```

### `Label`

Single- or multi-line SDF text. Color comes from `skin.TextLabel`; override via
`SkinOverride.TextLabel` (`text_color` in `.arcui`).

```cpp
std::string Text;
TextAlign   Align      = TextAlign::Left;
bool        WordWrap   = false;
bool        AutoCenter = false;
```

### `Button`

Clickable rect with label. Fires `OnClick` on mouse-up inside, or `OnActivate`
when confirmed via keyboard/gamepad. States: Normal → Hovered → Pressed →
Disabled, all resolved from the effective skin (`ButtonNormal` etc.).

```cpp
std::string            Text;
std::function<void()>  OnClick;
```

### `Image`

Textured quad scaled to the widget rect, turned `Rotation` radians
clockwise about its center (a spinner).

```cpp
TextureHandle Texture; SamplerHandle Sampler;
Color  Tint     = {1,1,1,1};
UVRect UV       = {0,0,1,1};
f32    Rotation = 0.0f;
```

### `ProgressBar`

Two-layer horizontal fill bar. Track from `skin.ProgressBg`, fill from
`skin.ProgressFill`; override either via `SkinOverride` (`fill_color` in
`.arcui` maps to `ProgressFill`).

```cpp
f32 Value = 0.5f;
```

### `ScrollPanel`

Clipped container with vertical scrolling and a drag scrollbar. Set
`ContentHeight` to the total logical height of all children.

```cpp
f32 ContentHeight  = 0.0f;
f32 ScrollOffset   = 0.0f;
f32 ScrollbarWidth = 6.0f;
```

Overrides `UpdateTree` and `CollectTree` to apply scissor clipping and scroll offset.

---

## .arcui File Format

`.arcui` files are JSON documents that describe a widget tree plus optional
metadata. They are loaded at runtime by `UILoader::Load`. The guiding principle
is **layout is data, behavior is code**: the file owns visual properties;
C++ wires `OnClick` and other callbacks after loading via `FindWidget<T>`.

### Top-level structure

```json
{
  "widgets": [ ... ],
  "meta":    { ... }
}
```

`widgets` is an array of root widget objects (equivalent to calling `Add<T>()` on
the screen). `meta` is a flat key-value object for style hints that C++ reads via
`GetMetaF32` / `GetMetaStr`.

### Widget object — base properties

Every widget object has a required `"type"` field and any subset of:

| Field          | Type         | Default      | Description                                                                                               |
|----------------|--------------|--------------|-----------------------------------------------------------------------------------------------------------|
| `type`         | string       | required     | One of: `"Panel"`, `"Scrim"`, `"Label"`, `"Button"`, `"Image"`, `"NineSlice"`, `"NineSliceButton"`, `"NineSliceProgressBar"`, `"ProgressBar"`, `"ScrollPanel"`, `"TextInput"`, `"Slider"`, `"Dropdown"`, `"Checkbox"`, `"RadioGroup"`, `"Switch"` |
| `name`         | string       | —            | Identifier for `FindWidget<T>(name)`                                                                      |
| `size`         | `[w, h]`     | `[100, 100]` | Pixel size                                                                                                |
| `size_percent` | `[x, y]`     | `[0, 0]`     | Overrides `size` as fraction of parent when > 0                                                           |
| `anchor`       | `[x, y]`     | `[0, 0]`     | Normalized point on parent rect                                                                           |
| `pivot`        | `[x, y]`     | `[0, 0]`     | Normalized point on this widget                                                                           |
| `offset`       | `[x, y]`     | `[0, 0]`     | Pixel offset after anchor/pivot alignment                                                                 |
| `zorder`       | int          | 0            | Sort key within screen layer                                                                              |
| `opacity`      | float        | 1.0          | Multiplied into children                                                                                  |
| `visible`      | bool         | true         |                                                                                                           |
| `enabled`      | bool         | true         |                                                                                                           |
| `focusable`    | bool         | false        | Keyboard/gamepad navigation                                                                               |
| `children`     | array        | —            | Nested widget objects                                                                                     |
| `tab_order`    | unsigned int | 0            | Focus order for keyboard/gamepad navigation. Equal values keep tree order.                                |
| `skin`         | object       | —            | Per-widget skin override block (see [Per-widget skin overrides](#per-widget-skin-overrides))              |

* Defaults listed are the base defaults. Specific widgets may override them — e.g., `focusable` defaults to `true` on `Button`, `Dropdown`, `Switch`, `Checkbox`, `Slider`, `TextInput`, and `RadioGroup`.
* All colors are `[r, g, b, a]` floats in 0–1 range.
* Setting any color/font field on a widget — either through a per-type property like `background_color` or via the `skin` override block — is an explicit override. To use the skin default, omit the field entirely; do **not** pass `[0,0,0,0]`.

### Per-type properties

**Panel**

| Field              | Type        | Description                                    |
|--------------------|-------------|------------------------------------------------|
| `background_color` | `[r,g,b,a]` | Fill color (overrides `skin.PanelBg`)          |
| `draw_border`      | bool        | Render the skin border                         |

**Scrim**

| Field              | Type        | Description                                    |
|--------------------|-------------|------------------------------------------------|
| `background_color` | `[r,g,b,a]` | Overlay color (overrides `skin.ScrimColor`)    |
| `draw_border`      | bool        | Render the skin border                         |

**Label**

| Field         | Type                              | Description                                      |
|---------------|-----------------------------------|--------------------------------------------------|
| `text`        | string                            | Literal display text                             |
| `text_key`    | string                            | Localization key — overrides `text` when present |
| `align`       | `"left"` / `"center"` / `"right"` | Horizontal alignment                             |
| `text_color`  | `[r,g,b,a]`                       | Overrides `skin.TextLabel`                       |
| `word_wrap`   | bool                              | Wrap at widget width                             |
| `auto_center` | bool                              | Center text vertically in widget                 |

**Button**

| Field        | Type        | Description                |
|--------------|-------------|----------------------------|
| `text`       | string      | Label text                 |
| `text_key`   | string      | Localization key           |
| `text_color` | `[r,g,b,a]` | Overrides `skin.TextLabel` |

**ProgressBar**

| Field        | Type        | Description                  |
|--------------|-------------|------------------------------|
| `value`      | float       | Fill fraction 0–1            |
| `fill_color` | `[r,g,b,a]` | Overrides `skin.ProgressFill`|

**ScrollPanel**

| Field             | Type  | Description                      |
|-------------------|-------|----------------------------------|
| `content_height`  | float | Total logical height of children |
| `scrollbar_width` | float | Scrollbar gutter width in pixels |

**NineSlice**

| Field                             | Type        | Description                            |
|-----------------------------------|-------------|----------------------------------------|
| `tint`                            | `[r,g,b,a]` | Color tint                             |
| `uv_border_left/right/top/bottom` | float       | UV-space border fractions (0–1)        |
| `pixel_left/right/top/bottom`     | float       | Rendered border sizes in screen pixels |

**NineSliceButton**

Inherits `NineSlice` border fields and adds button text + per-state tints.
Texture/sampler must currently be wired in code (Phase 36 will move this to the
asset registry).

| Field           | Type        | Description                                     |
|-----------------|-------------|-------------------------------------------------|
| `text`          | string      | Label text                                      |
| `text_key`      | string      | Localization key                                |
| `tint_normal`   | `[r,g,b,a]` | Tint applied in the idle state                  |
| `tint_hovered`  | `[r,g,b,a]` | Tint applied while hovered                      |
| `tint_pressed`  | `[r,g,b,a]` | Tint applied while pressed                      |
| `tint_disabled` | `[r,g,b,a]` | Tint applied when `enabled = false`             |

**NineSliceProgressBar**

Inherits `NineSlice` border fields. Texture/sampler wired in code.

| Field        | Type        | Description                                  |
|--------------|-------------|----------------------------------------------|
| `value`      | float       | Fill fraction 0–1                            |
| `track_tint` | `[r,g,b,a]` | Tint of the track texture                    |
| `fill_tint`  | `[r,g,b,a]` | Tint of the fill texture                     |

**Image**

| Field  | Type            | Description |
|--------|-----------------|-------------|
| `tint` | `[r,g,b,a]`     | Color tint  |
| `uv`   | `[u0,v0,u1,v1]` | UV rect     |

**TextInput**

| Field         | Type                              | Description                                      |
|---------------|-----------------------------------|--------------------------------------------------|
| `placeholder` | string                            | Greyed text shown when the field is empty        |
| `max_length`  | unsigned int                      | Maximum character count (0 = unlimited)          |
| `mode`        | `"text"` / `"numeric"` / `"regex"` | Input filter (`regex` uses `pattern`)            |
| `pattern`     | string                            | Regex used by `mode = "regex"`                   |

**Slider**

| Field   | Type  | Description                                 |
|---------|-------|---------------------------------------------|
| `value` | float | Current value, clamped to `[min, max]`      |
| `min`   | float | Minimum value                               |
| `max`   | float | Maximum value                               |
| `step`  | float | Quantization step (0 = continuous)          |

**Dropdown**

| Field      | Type            | Description                                  |
|------------|-----------------|----------------------------------------------|
| `selected` | int             | Index of the initially selected item         |
| `items`    | array of string | Selectable item labels                       |

**Checkbox**

| Field       | Type   | Description                                |
|-------------|--------|--------------------------------------------|
| `checked`   | bool   | Initial state                              |
| `label`     | string | Inline label text                          |
| `label_key` | string | Localization key — overrides `label`       |

**RadioGroup**

| Field         | Type            | Description                                  |
|---------------|-----------------|----------------------------------------------|
| `selected`    | int             | Index of the initially selected option       |
| `item_height` | float           | Height of each option row in pixels          |
| `items`       | array of string | Option labels                                |

**Switch**

| Field        | Type  | Description                                              |
|--------------|-------|----------------------------------------------------------|
| `on`         | bool  | Initial state                                            |
| `anim_speed` | float | Speed of the on/off thumb tween (units/sec)              |

### Per-widget skin overrides

Any widget may carry a `"skin"` block that overrides individual fields of the
active `UISkin` for just that widget (and only that widget — overrides do
**not** cascade to children). Keys match the field names in `UISkin` exactly:

```json
{
  "type": "Button", "name": "danger-btn", "text_key": "ui.delete",
  "skin": {
    "ButtonNormal":  [0.50, 0.15, 0.15, 1.0],
    "ButtonHovered": [0.65, 0.20, 0.20, 1.0],
    "TextLabel":     [1.0, 1.0, 1.0, 1.0]
  }
}
```

Supported keys: `Font` (string — looks up a font registered with
`UILoader::RegisterFont`), `FontScale` (float), and any of the color fields
listed in `UISkin` (`PanelBg`, `PanelBorder`, `ButtonNormal`, `ButtonHovered`,
`ButtonPressed`, `ButtonDisabled`, `TextNormal`, `TextDisabled`, `TextLabel`,
`ProgressBg`, `ProgressFill`, `ScrollTrack`, `ScrollThumb`, `ScrollThumbHovered`,
`AccentColor`, `ScrimColor`, `InputBg`, `InputBorder`, `InputFocusBorder`,
`InputCursor`, `InputPlaceholder`, `SliderThumb`, `SliderThumbHovered`,
`CheckboxBg`, `CheckboxHovered`, `CheckboxCheck`, `SwitchOn`, `SwitchOff`,
`SwitchThumb`).

Sound keys may also be overridden per widget for interaction events that are
*driven by the widget itself*: `SoundActivate` (button confirm / click),
`SoundToggle` (checkbox / switch), `SoundSliderTick` (slider value step).
`SoundFocusMove` and `SoundBack` are screen/global UX events handled by
`UIManager` from the active skin and cannot be overridden per widget.

### Meta section

The `meta` object holds arbitrary key-value pairs (`number` or `string`) that C++
reads via `GetMetaF32(key, default)` / `GetMetaStr(key, default)`. Use it to pass
style parameters to C++ that generates procedural content so the numbers live in
the file rather than in code:

```json
"meta": {
  "chip_h": 22,
  "chip_gap": 4,
  "key_col_x": 242,
  "ctrl_col_x": 454
}
```

### Localization

Labels and buttons support a `"text_key"` field. When present, `UILoader` passes
the key through `Loc::Get(key)` instead of using the literal `"text"` value.
This allows the same `.arcui` file to display in any language without modification.

```json
{ "type": "Button", "name": "resume-btn", "text_key": "ui.pause.resume" }
```

The fallback if a key has no translation is the key string itself, which makes
`.arcui` files readable as documentation even without a translation file loaded.

### Full example

```json
{
  "widgets": [
    { "type": "Scrim" },
    {
      "type": "Panel", "name": "bg",
      "size": [360, 280], "anchor": [0.5, 0.5], "pivot": [0.5, 0.5], "zorder": 1,
      "children": [
        {
          "type": "Label", "name": "title",
          "text_key": "ui.pause.title",
          "align": "center",
          "size": [360, 32], "anchor": [0.5, 0.0], "pivot": [0.5, 0.0],
          "offset": [0, 20], "zorder": 2
        },
        {
          "type": "Button", "name": "resume-btn",
          "text_key": "ui.pause.resume",
          "size": [240, 44], "anchor": [0.5, 0.0], "pivot": [0.5, 0.0],
          "offset": [0, 72], "focusable": true, "zorder": 2
        },
        {
          "type": "Button", "name": "quit-btn",
          "text_key": "ui.pause.quit",
          "text_color": [0.88, 0.30, 0.30, 1.0],
          "size": [240, 44], "anchor": [0.5, 0.0], "pivot": [0.5, 0.0],
          "offset": [0, 128], "focusable": true, "zorder": 2
        }
      ]
    }
  ],
  "meta": { "btn_gap": 56 }
}
```

### C++ integration pattern

```cpp
void PauseMenuScreen::OnEnter()
{
    // Load visual layout — clears existing roots and meta first
    if (!LoadLayout("assets/engine/ui/pause_menu.arcui"))
        BuildFallback(); // inline fallback if file absent

    // Wire behavior by name — the file owns layout, code owns logic
    if (auto* btn = FindWidget<Button>("resume-btn")) btn->OnClick = OnResume;
    if (auto* btn = FindWidget<Button>("quit-btn"))   btn->OnClick = OnQuit;

    // Read style hints for procedurally-generated content
    const f32 gap = GetMetaF32("btn_gap", 56.0f);
}
```

### Asset locations

Engine-provided `.arcui` files live in `engine/assets/ui/` and are copied to
`assets/engine/ui/` in the runtime output directory by the build system.

Game-specific layouts go in `game/assets/ui/`. To override an engine screen's
layout, set `LayoutPath` before pushing:

```cpp
pause->LayoutPath = "assets/ui/my_pause_menu.arcui";
GetUI().OpenMenu(std::move(pause));
```

### Custom widget types

Register game-side widget types before any `Load()` call that references them:

```cpp
UILoader::RegisterType("HealthOrb", [] { return std::make_unique<HealthOrb>(); });
```

Base properties (`size`, `anchor`, `name`, etc.) are applied automatically.
Type-specific properties must be handled by the widget's own deserialization
if needed (future: per-type `FromJson` hook).

---

### Custom UI with Lua scripting

A layout can name its script (`"script": "ui.shop"`) and its widgets' Lua
handlers (`"on_click": "OnBuy"` on buttons, `"on_change": "OnVolume"` on
sliders, switches, checkboxes, text inputs, dropdowns and radio groups).
`ScriptedScreen` (Lua: `UI.Open(path)`) loads it and runs the script;
`"meta": { "layer": "hud" }` makes it a HUD. Engine-owned screens keep their
C++ behavior and ignore both. See [scripting.md](scripting.md#lua-ui).

---

## Screen Management

`UIScreen` owns a widget tree and represents one logical UI layer.

Key methods:

```cpp
// Layout
bool      LoadLayout(std::string_view path);   // parse .arcui, populate tree
template<typename T>
T*        FindWidget(std::string_view name);   // find named widget, dynamic_cast
f32       GetMetaF32(std::string_view key, f32 def = 0.0f) const;
std::string GetMetaStr(std::string_view key, std::string_view def = "") const;

// Manual construction (when not using LoadLayout)
template<typename T> T* Add();
```

`UIManager` maintains a **stack** of active screens, drawn bottom to top.

**Layers.** Every screen has a `Layer`, bottom to top: `Hud` (health, coins),
`Menu` (the default: menus and their settings screens), `Overlay` (debug
overlays, the loading screen) and `System` (the splash). `Push` puts a screen
on top of its own layer, so a menu opened while an overlay is up goes beneath
it, and a HUD a scene pushes never covers a menu.

**Input** goes to the topmost screen with `TakesInput` (the default). HUDs and
display-only overlays (the demo's F3 event log, the loading screen) turn it
off, so the menu under them still gets the keys. `BlocksInput` is separate: it
holds the *game* still while the screen is up (`HasBlockingScreen`).

**Ticks.** Every screen on the stack gets `OnUpdate(dt)` each tick - animate
and refresh there. Only the screen input goes to also gets `OnTick(dt, input)`
and its widget updates - read input there. A display-only screen (the event
log, the loading screen's dots and bar) does all its work in `OnUpdate`.

**Menus swap each other out.** Open a top-level menu with `OpenMenu`: it
closes every open Menu-layer screen - another menu and whatever settings
screens it opened - then pushes the new one. Screens a menu opens on top of
itself (Controls, Audio) use `Push`.

```cpp
ui.OpenMenu(std::move(pause));          // replaces the demo switcher, if it's open
ui.Push(std::move(controls));           // on top of the pause menu
controls->OnBack = [&ui, screen] { ui.Close(screen); }; // close yourself, by pointer
```

**Closing.** A screen closes itself with `Close(this-screen)` (fading out if it
has a transition). `Pop` closes the topmost menu, but a screen closing itself
by pointer can't close the wrong one; `CloseMenus` closes them all; `Remove`
drops a screen at once (a scene's HUD as it unloads). `IsOpen(screen)` and
`HasOpenMenu()` tell what's up.

**Gamepad.** In menus the D-pad moves focus (like the arrow keys), South (A)
confirms and East (B) goes back. Held, either repeats: one step on the press,
then after 0.4 s a step every 0.08 s until released (`NavRepeat`). The left stick moves an on-screen cursor -
South then clicks what's under it - and the right stick scrolls lists. The
cursor appears when the stick moves while a screen takes input, and hides
again as soon as the mouse, keyboard or D-pad is used. A scroll panel keeps
its focused child in view, so D-pad navigation down a long list (the controls
screen) scrolls with it. These are the hidden `UI_` actions (`UI_Cursor*`,
`UI_Scroll*`), which rebinding a game action never takes away.

**Keys that open menus** work while another menu is open, swapping it - but
a key that is also the UI's back (`UIManager::IsBackPressed`: Escape, gamepad
East) should be read as back while a menu is open. The World demo's pause key
does this: gamepad Start swaps the demo switcher for the pause menu, Escape
just closes the switcher.

---

## Theming (UISkin)

`UISkin` is a plain data struct loaded from JSON (`LoadFromFile` / `SaveToFile`).
It defines defaults for every widget type; individual widgets override only what
they need.

Key fields:

```cpp
const FontAtlas* Font   = nullptr;
f32              FontScale = 1.0f;

Color PanelBg, PanelBorder;
Color ButtonNormal, ButtonHovered, ButtonPressed, ButtonDisabled;
Color TextNormal, TextDisabled, TextLabel;
Color ProgressBg, ProgressFill;
Color ScrollTrack, ScrollThumb, ScrollThumbHovered;
Color AccentColor;  // focused/listening states
Color ScrimColor;   // Scrim widget default
Color InputBg, InputBorder, InputFocusBorder, InputCursor, InputPlaceholder;
Color SliderThumb, SliderThumbHovered;
Color CheckboxBg, CheckboxHovered, CheckboxCheck; // shared with RadioGroup
Color SwitchOn, SwitchOff, SwitchThumb;

// Sound keys — empty string = no sound; resolved via AudioManager at interaction time
std::string SoundFocusMove;   // keyboard/gamepad focus navigation
std::string SoundActivate;    // button confirm / click
std::string SoundBack;        // cancel / close / pop screen
std::string SoundSliderTick;  // slider value step
std::string SoundToggle;      // checkbox / switch toggle

i32   ScreenLayerBase; // set per-screen by UIManager; ensures stack order
```

In the skin JSON (`engine/assets/skins/default.skin.json`) colors are `[r, g, b, a]`
arrays grouped by widget: `panel` (`background`, `border`), `button` (`normal`,
`hovered`, `pressed`, `disabled`), `text` (`normal`, `disabled`, `label`),
`progressBar` (`background`, `fill`), `scrollBar` (`track`, `thumb`, `thumbHovered`),
`input` (`background`, `border`, `focusBorder`, `cursor`, `placeholder`), `slider`
(`thumb`, `thumbHovered`), `checkbox` (`background`, `hovered`, `check`), `switch`
(`on`, `off`, `thumb`), plus top-level `accentColor`, `scrimColor` (legacy key
`overlayColor` is still read), `fontScale`, and a `sounds` object (`focusMove`,
`activate`, `back`, `sliderTick`, `toggle`). Any missing key keeps the `UISkin` default.

Sound keys are plain strings for now; Phase 36 replaces them with asset handle references from `arcbit-pack`. The default engine skin ships with all sound keys empty (no sound).

---

## Rendering

`UIManager::CollectRenderData` walks the screen stack bottom-to-top and emits
quads via the UI sprite pipeline.

**Layer isolation:** Each screen on the stack gets `ScreenLayerBase = stackIndex * 1000`,
ensuring widgets from higher screens always sort above lower ones regardless of ZOrder.

**Opacity:** Parent opacity multiplies into children before emitting quads.

**Clipping:** `ScrollPanel` registers scissor rects and clips child quads to its bounds.

---

## File Structure

```
engine/
  include/arcbit/ui/
    UIManager.h
    UIScreen.h
    UIWidget.h
    UILoader.h              — .arcui parser, type registry
    Widgets.h               — Panel, Scrim, Label, Button, Image,
                              NineSlice, NineSliceButton, NineSliceProgressBar,
                              ProgressBar, ScrollPanel
    InputWidgets.h          — TextInput, Slider, Dropdown, Checkbox,
                              RadioGroup, Switch
    UISkin.h
    HudScreen.h
    PauseMenuScreen.h
    InputRebindScreen.h
    AudioSettingsScreen.h
    GraphicsSettingsScreen.h
    SplashScreen.h
    MainMenuScreen.h
    CreditsScreen.h
    ConfirmScreen.h         — yes / no question (Application::Confirm)
    SaveSlotScreen.h        — save / load slot list (docs/saves.md)
    SavingIndicator.h       — the "Saving..." spinner
    ScriptedScreen.h        — a screen driven by Lua (docs/scripting.md)
    ConsoleScreen.h         — the debug console window, ErrorToasts
  src/
    UIManager.cpp
    UIScreen.cpp
    UIWidget.cpp
    UILoader.cpp
    Widgets.cpp
    InputWidgets.cpp
    UISkin.cpp
    HudScreen.cpp
    PauseMenuScreen.cpp
    InputRebindScreen.cpp
    AudioSettingsScreen.cpp
    GraphicsSettingsScreen.cpp
    SplashScreen.cpp
    MainMenuScreen.cpp
    CreditsScreen.cpp
    ConfirmScreen.cpp
    SaveSlotScreen.cpp
    SavingIndicator.cpp
  assets/ui/                — engine-provided .arcui defaults
    pause_menu.arcui
    audio_settings.arcui
    graphics_settings.arcui
    input_rebind.arcui
    main_menu.arcui
    save_slots.arcui
    saving.png              — the default saving icon
  assets/fonts/             — engine fonts (deployed to assets/engine/fonts/)
    Roboto-Regular.ttf
  assets/skins/             — engine default skin (deployed to assets/engine/skins/)
    default.skin.json
  assets/locale/            — engine locale strings
    en.json
```

Engine assets are copied under `assets/engine/` in the runtime output directory:
- `assets/engine/ui/` — arcui layouts
- `assets/engine/fonts/` — Roboto-Regular.ttf
- `assets/engine/skins/` — default.skin.json
- `assets/engine/locale/` — locale strings

Game assets go in `game/assets/`.

---

## Implementation Phases

### Phase 21A — Foundation ✓
- [x] `UIWidget` base class: anchor/pivot/offset/size layout, opacity, visibility, Z-order, `Name`
- [x] `UIScreen`: widget tree, `Update`, `Collect`, `LoadLayout`, `FindWidget<T>`, meta accessors
- [x] `UIManager`: screen stack, `Push`/`Pop`, render collection, `HasBlockingScreen()`
- [x] `Panel`, `Overlay`, `Label`, `Button`, `Image`, `ProgressBar` widgets
- [x] Mouse input routing (hover + click)
- [x] Wire `UIManager` into `Application`
- [x] `UISkin` JSON loader

### Phase 21B — Polish ✓
- [x] `NineSlice` widget
- [x] Keyboard/gamepad focus navigation (tab order, confirm input)
- [x] Word wrap in `Label`, `AutoCenter`
- [x] Animated transitions: fade in/out per screen
- [x] `UIScreen::BlocksInput` + screen layer isolation (`ScreenLayerBase`)
- [x] `ScrollPanel` with scissor-rect clip + mouse wheel scroll
- [x] Engine-provided `HudScreen`, `PauseMenuScreen`, `InputRebindScreen`
- [x] Chip-based `InputRebindScreen`: multiple bindings per action, key/mouse/gamepad columns

### Phase 21C — Data-driven UI ✓ (in progress)
- [x] `.arcui` JSON format for widget tree serialization
- [x] `UILoader`: type registry, full property deserialization for all built-in types
- [x] `UIWidget::Name` + `FindDescendant` for post-load callback wiring
- [x] `UIScreen::LoadLayout`, `FindWidget<T>`, meta accessors
- [x] `PauseMenuScreen` migrated to `.arcui` with C++ fallback
- [x] `AudioSettingsScreen`, `GraphicsSettingsScreen` migrated to `.arcui` with `WireVolumeRow`/`FindWidget` callbacks
- [x] Engine asset deployment: `engine/assets/ui/` → `assets/engine/ui/` via CMake
- [x] Localization foundation: `Loc::Get(key)`, `text_key` field in UILoader, `en.json` engine locale
- [x] Migrate `AudioSettingsScreen`, `GraphicsSettingsScreen`, `InputRebindScreen` chrome to `.arcui`
- [ ] Editor integration: load/save `.arcui` from AvaloniaUI editor

### Phase 21D — Engine Screens ✓
- [x] `SplashScreen`: ordered image sequence; per-entry fade-in/hold/fade-out; skip on confirm; `BlocksGame` suppresses game scene and UI below while active
- [x] `MainMenuScreen`: data-driven via `main_menu.arcui`; `ShowContinue` / `ShowControls` / `ShowAudioSettings` / `ShowGraphicsSettings` / `ShowCredits` flags; C++ fallback
- [x] `CreditsScreen`: scrolling credits from a JSON file (`title`, `sections` of `heading` + `names`, `scroll_speed`); closes when the roll ends or on back
- [x] Screen layers (`Hud` / `Menu` / `Overlay` / `System`) replace `StaysOnTop`: a screen is pushed on top of its layer; `TakesInput = false` lets input through to the menu beneath; `OpenMenu` swaps menus out, `Close(screen)` closes by pointer and `Pop()` only touches menus; a screen removing itself during `UIManager::Update` is deferred until the update returns
- [ ] `DialogScreen`: portrait + speaker name + scrolling text + choice buttons (deferred to Phase 27 dialog system)
- [ ] `InventoryScreen` stub (deferred to Phase 29 item system)
- [x] `GraphicsSettingsScreen` extended: VSync, Show FPS, and Show Debug Info all use `Switch` widgets; `Settings::Graphics.ShowFps` and `ShowDebugInfo` drive live HUD and debug overlay
- [x] `UIScreen::BlocksGame` flag + `UIManager::HasGameBlockingScreen()` — suppresses `OnUpdate`, `OnRender`, scene render, and debug overlay; `CollectRenderData` skips screens below the lowest active blocking screen

### Phase 25 — Save screens ✓
- [x] `SaveSlotScreen`: save into a manual slot or load any save; rows with thumbnail, name, scene, playtime and date; frame from `save_slots.arcui`
- [x] `ConfirmScreen`: yes / no over the current menu, No focused first; `Application::Confirm`
- [x] `SavingIndicator`: spinning icon and "Saving..." in the bottom-right corner on every save (Overlay layer)
- [x] `SplashEntry` text cards: centered white text on black with an optional spinning icon - the saving notice
- [x] `PauseMenuScreen` Save Game / Load Game and `MainMenuScreen` Load Game buttons; `UIScreen::StackButtons` closes the gaps hidden buttons leave
- [x] `Image::Rotation`; UI sprites honor `Sprite::Rotation` (the gamepad cursor's diamond)

### Phase 26 — Lua UI and the console ✓
- [x] `ScriptedScreen`: `.arcui` `"script"`, `"on_click"` / `"on_change"`; Screen / Widget handles from Lua
- [x] `ConsoleScreen` (the debug console) and `ErrorToasts` (debug builds' script errors)
- [x] `TextInput::SetText` (cursor to the end) and `Suggestion` (dimmed predicted text); the cursor stays in range when `Text` changes under it
- [x] UI draws over screen-space SDF text (the debug overlay); typing into a field rests the game's keys (`InputManager::SetTyping`)

### Phase 21E — Skin Sounds ✓
- [x] Sound key fields added to `UISkin`: `SoundFocusMove`, `SoundActivate`, `SoundBack`, `SoundSliderTick`, `SoundToggle`
- [x] Matching `std::optional<std::string>` in `UISkinOverride`; merge in `GetEffectiveSkin`, deserialize in `ApplySkinBlock`
- [x] `UISkin::LoadFromFile` / `SaveToFile` handle `"sounds"` JSON block
- [x] `default.skin.json` ships with all sound keys empty (no sound by default)
- [x] Sound key cached per widget in `UIWidget::_interactSound` (set in `OnCollect` from effective skin); played in `OnActivate` and mouse-click paths — no skin access needed at interaction time
- [x] Button / NineSliceButton → `SoundActivate`; Switch / Checkbox → `SoundToggle`; Slider → `SoundSliderTick` on value step
- [x] `UIManager` plays `SoundFocusMove` when the focused widget changes (keyboard/gamepad nav); plays `SoundBack` before `OnBackPressed`
- [ ] Engine SFX pack: optional `engine-sounds.skin.json` overlay with default sound keys for engine screens (deferred — requires audio assets)
