# GUI

## Overview

The GUI system is a retained-mode, screen-space widget framework built on top of
the existing SDF text pipeline and sprite batcher. It handles all in-game UI:
HUD elements, menus, settings screens, the dialog box ([dialog.md](dialog.md))
and anything a game builds.

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
std::string TextKey;              // shows Loc::Get(TextKey), again when the language changes
TextAlign   Align      = TextAlign::Left;
bool        WordWrap   = false;
bool        AutoCenter = false;
bool        Markup     = false;   // read rich text tags (below)
```

`StartReveal(charsPerSecond)` types the text out ([Typewriter and
effects](#typewriter-and-effects)).

### `Button`

Clickable rect with label. Fires `OnClick` on mouse-up inside, or `OnActivate`
when confirmed via keyboard/gamepad. States: Normal → Hovered → Pressed →
Disabled, all resolved from the effective skin (`ButtonNormal` etc.).

```cpp
std::string            Text;
std::string            TextKey;
std::function<void()>  OnClick;
```

`NineSliceButton` and `NineSliceProgressBar` are the textured versions of
`Button` and `ProgressBar`; the input widgets (`TextInput`, `Slider`,
`Dropdown`, `Checkbox`, `RadioGroup`, `Switch`) are in `InputWidgets.h`. Their
`.arcui` fields are below.

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
| `placeholder_key` | string                        | Localization key — overrides `placeholder`       |
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
| `style` | `"line"` / `"bar"` | `line`: a thin track and thumb. `bar`: drawn like a ProgressBar (`ProgressBg` / `ProgressFill`, full height) with a slim handle - the audio settings' volumes |

A press anywhere on a slider jumps there and drags.

**Dropdown**

| Field      | Type            | Description                                  |
|------------|-----------------|----------------------------------------------|
| `selected` | int             | Index of the initially selected item         |
| `items`    | array of string | Selectable item labels                       |
| `item_keys` | array of string | Localization keys — override `items`        |
| `filter_threshold` | int     | With more items, typing filters the open list (default 8) |
| `max_visible` | int          | Rows shown before the list scrolls (default 8) |
| `item_height` | float        | Row height of the open list (default 28)     |

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

Player text in a layout is a key: `"text_key"` (Label, Button,
NineSliceButton), `"label_key"` (Checkbox), `"placeholder_key"` (TextInput),
`"item_keys"` (Dropdown, RadioGroup). The widget keeps the key and shows it in
the player's language - again whenever the language changes.

```json
{ "type": "Button", "name": "resume-btn", "text_key": "ui.pause.resume" }
```

A key with no string anywhere shows as the key itself. See
[localization.md](localization.md).

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

## Fonts and text

Text is UTF-8 everywhere: labels, buttons, text inputs (which move, delete
and count by character) and `DrawText`.

`FontAtlas` bakes a TTF or OTF font into 1024x1024 atlas pages:

- **At load**, a set of codepoint ranges - by default `FontAtlas::DefaultGlyphs()`:
  Latin-1, Latin Extended-A and common punctuation, enough for English,
  French, Spanish, German, Portuguese, Polish, Czech and more.
- **Anything else** is baked the first time it's drawn, onto a new page if
  needed; the new pixels reach the GPU before that frame renders
  (`FontAtlas::UploadAll`, called by `Application`). `Prepare(text)` bakes a
  text's glyphs ahead, so drawing it later doesn't stop to rasterize.
- **Fallbacks:** a codepoint the font lacks comes from its fallback faces
  (`AddFallback`), then the shared ones every font uses
  (`FontAtlas::SetSharedFallbacks` - a Japanese face, say), drawn at the
  font's em size. Faces are `FontFace::Load(path)`, shared between atlases.
- **Still missing:** a box, so a missing glyph is visible rather than silent.
  Space characters (no-break, narrow no-break, thin) and the UI triangles
  (up, down, left, right) a font lacks are drawn by the engine.

SDF fonts (all UI text) work with OpenType/CFF fonts as well as TrueType:
CFF outlines are rasterized large and turned into a distance field there,
since stb_truetype's own SDF can't read their curves.

`WrapText` (and `"word_wrap"` labels) break lines at spaces and between
Chinese / Japanese characters, never before closing punctuation (`。`,
`、`, small kana), and break a word wider than the line between letters.
Right-to-left text and complex shaping (Arabic, Hebrew, Thai, Hindi) aren't
supported yet.

## Rich text

A Label with `"markup": true` (`Label::Markup`) reads tags in its text -
localized strings included - and lays the styled runs out together, wrapping
across them:

```json
{ "type": "Label", "markup": true, "text": "Take the <color=gold>golden</color> key, <b>quickly</b>." }
```

| Tag | |
|---|---|
| `<b>` `<i>` `<u>` `<s>` | bold, italic, underline, strikethrough |
| `<color=#e33>` `<color=gold>` | color: `#rgb`, `#rrggbb`, `#rrggbbaa`, or white, black, gray, red, green, blue, yellow, gold, orange, purple, cyan |
| `<size=1.5>` | scale, times the label's |
| `<outline>` `<outline=#000>`, `<shadow>` | an outline, a drop shadow |
| `<wave>` `<shake>` `<rainbow>` | animated effects |
| `<speed=0.5>`, `<pause=0.4>` | a typewriter's pace, and a wait (seconds) |

Each closes with `</name>` (`<pause>` has none). Only these names are tags,
so `a < b` and `<3` stay text; `<<` writes a literal `<`; a tag with a bad
value (`<color=nonsense>`) stays as written, so the mistake shows.

### Text styles

A skin's `"text_style"` (`UISkin::Text`) is how every label and button draws
its text, and any widget can carry its own `"text_style"` (replacing the
skin's whole). Tags build on it - `<i>` in bold text is bold italic.

```json
"text_style": { "bold": false, "italic": false,
                "outline": true, "outline_width": 2, "outline_color": [0, 0, 0, 1],
                "shadow": true, "shadow_offset": [2, 2], "shadow_color": [0, 0, 0, 0.6], "shadow_softness": 1 }
```

Bold and italic come from a font's style faces when it has them
(`FontAtlas::SetStyleFace` - the engine font has Roboto Bold, Italic and Bold
Italic) and are faked otherwise: a thicker edge for bold, a slant for italic
(Japanese, for one). Outlines, glows and faux bold are distance-field
thresholds in `ui.frag`; shadows, underlines and strikethroughs are extra
quads.

In code: `ParseMarkup` / `StripMarkup`, and `LayoutRichText` (a glyph list:
each character's place, style and index - what a typewriter reveals and
effects move) with `DrawRichText`.

### Typewriter and effects

`Label::StartReveal(charsPerSecond)` shows a label's text a character at a
time, each one fading in; `SkipReveal` shows the rest (a confirm press
mid-line). It honors `<pause>` and `<speed>`. `GetReveal()` is the
`TextReveal` behind it: `OnBlip(codepoint)` for a voice sound per character
(never for spaces, at most every `BlipGap` seconds) and `OnDone`.

`TextReveal` works on any `TextLayout`: call `Update(dt, layout)` each frame
and draw with `TextDrawOptions::Visible = GetVisible()`. `<wave>`, `<shake>`
and `<rainbow>` run on a clock `UIManager::Update` advances
(`GetTextClock`, passed as `TextDrawOptions::Time`).

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
    GameSettingsScreen.h    — language, text speed, auto-advance
    DialogScreen.h          — the dialog box (docs/dialog.md)
    LoadingScreen.h         — the loading screen (docs/scenes.md)
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
    GameSettingsScreen.cpp
    DialogScreen.cpp
    SaveSlotScreen.cpp
    SavingIndicator.cpp
  assets/ui/                — engine-provided .arcui defaults
    pause_menu.arcui
    audio_settings.arcui
    graphics_settings.arcui
    input_rebind.arcui
    main_menu.arcui
    save_slots.arcui
    game_settings.arcui
    dialog.arcui            — the dialog box
    dialog/                 — its stock art and voice blip
    saving.png              — the default saving icon
  assets/fonts/             — engine fonts (deployed to assets/engine/fonts/)
    Roboto-Regular.ttf, -Bold, -Italic, -BoldItalic
  assets/skins/             — engine default skin (deployed to assets/engine/skins/)
    default.skin.json
  assets/locale/            — engine locale strings
    en.json, fr.json, ja.json
```

Engine assets are copied under `assets/engine/` in the runtime output directory:
- `assets/engine/ui/` — arcui layouts
- `assets/engine/fonts/` — Roboto
- `assets/engine/skins/` — default.skin.json
- `assets/engine/locale/` — locale strings

Game assets go in `game/assets/`.
