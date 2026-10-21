# Arcbit Sprite Format — Specification v1.0

This is the single canonical format for all sprite sheet assets in Arcbit.
It covers named frames, uniform tile grids, and animation clips in one file.
`SpriteSheet` loads it; the editor (Phase 40) will read and write it, and
`arcbit-pack` (Phase 36) will compile it into the `.arcasset` container for
shipped builds.

---

## Full example

```json
{
  "version": "1.0",
  "texture": "player.png",
  "pixels_per_unit": 16,

  "frames": [
    { "name": "idle_down_0", "rect": { "x":  0, "y": 0, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } },
    { "name": "idle_down_1", "rect": { "x": 45, "y": 0, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } },
    { "name": "walk_down_0", "rect": { "x":  0, "y": 35, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } },
    { "name": "walk_down_1", "rect": { "x": 45, "y": 35, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } }
  ],

  "animations": [
    {
      "name": "idle_down",
      "loop": true,
      "frames": [
        { "frame": "idle_down_0", "duration_ms": 1600 },
        { "frame": "idle_down_1", "duration_ms": 60 }
      ]
    },
    {
      "name": "walk_down",
      "loop": true,
      "frames": [
        { "frame": "walk_down_0", "duration_ms": 120 },
        { "frame": "walk_down_1", "duration_ms": 120 }
      ]
    }
  ],

  "tile_grid": {
    "tile_width": 16,
    "tile_height": 16
  }
}
```

---

## Field reference

### `version` *(required)*

```json
"version": "1.0"
```

Format version string. The loader rejects files with an unrecognized version
rather than silently misreading them. Always `"1.0"` for files created now;
the minor component may be bumped for additive changes, the major component for
breaking changes.

---

### `texture` *(required)*

```json
"texture": "player.png"
```

Path to the PNG sprite sheet, relative to the JSON file's own directory. The
loader resolves it relative to the metadata file so both files can live in the
same folder.

---

### `pixels_per_unit` *(optional)*

```json
"pixels_per_unit": 64
```

How many texture pixels correspond to one world tile. Used by `SpriteSheet::ToWorldSize`
to compute the natural world-space size of a sprite, and by the editor to preview
sprites at their correct in-game scale.

**If omitted, the world's tile size is used as the denominator** — so a sprite
whose pixel dimensions equal the tile size renders as exactly one tile by default.
Only specify this field when intentionally deviating from that default.

| Scenario | pixels_per_unit | Result (worldTileSize = 32) |
|----------|-----------------|----------------------------|
| 32×32 sprite, standard resolution | omit | 32×32 world px — 1 tile |
| 64×64 sprite, high-res but still 1 tile | 64 | 32×32 world px — 1 tile |
| 64×64 sprite, intentional 2-tile size | omit or 32 | 64×64 world px — 2 tiles |
| 16×16 sprite on a 32-px tile grid | 16 | 64×64 world px — 2 tiles (16-px art scaled up) |

---

### `frames` *(optional)*

An array of named rectangular regions of the texture. Each frame has a unique
name that animations and game code use to look it up.

```json
"frames": [
  {
    "name":  "idle_down_0",
    "rect":  { "x": 0, "y": 0, "w": 45, "h": 35 },
    "pivot": { "x": 0.5, "y": 1.0 }
  }
]
```

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Unique identifier for this frame within the file |
| `rect.x`, `rect.y` | yes | Top-left corner of the frame in texture pixels |
| `rect.w`, `rect.h` | yes | Width and height in texture pixels |
| `pivot.x`, `pivot.y` | no | Normalized origin point (0–1). Default `{ "x": 0.5, "y": 0.5 }` (center) |

Common pivot values:
- `{ 0.5, 0.5 }` — center (default)
- `{ 0.5, 1.0 }` — bottom-center (standing characters)
- `{ 0.0, 0.0 }` — top-left

The engine uses the pivot to offset the sprite so its logical position matches
`Transform2D.Position`. A character with pivot `{ 0.5, 1.0 }` whose
Transform2D.Position is at tile center will stand with its feet on the tile
rather than its center floating above it.

---

### `animations` *(optional)*

Named sequences of frames with per-frame durations. Requires `frames` to be
defined in the same file (animation frames reference frame names, not indices).

```json
"animations": [
  {
    "name": "walk_down",
    "loop": true,
    "frames": [
      { "frame": "walk_down_0", "duration_ms": 100 },
      { "frame": "walk_down_1", "duration_ms": 100, "events": ["FootStep"] },
      { "frame": "walk_down_2", "duration_ms": 100 },
      { "frame": "walk_down_3", "duration_ms": 100 },
      { "frame": "walk_down_4", "duration_ms": 100, "events": ["FootStep"] },
      { "frame": "walk_down_5", "duration_ms": 100 }
    ]
  },
  {
    "name": "attack_down",
    "loop": false,
    "frames": [
      { "frame": "attack_down_0", "duration_ms": 80 },
      { "frame": "attack_down_1", "duration_ms": 80 },
      { "frame": "attack_down_2", "duration_ms": 80, "events": ["AttackHit"] },
      { "frame": "attack_down_3", "duration_ms": 80 }
    ]
  }
]
```

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Unique animation name used by the `Animator` component |
| `loop` | no | Whether the clip loops when it reaches the last frame. Default `false` |
| `frames[].frame` | yes | Name of a frame defined in the top-level `frames` array |
| `frames[].duration_ms` | yes | How long this frame is displayed, in milliseconds |
| `frames[].events` | no | Array of event name strings fired when this frame becomes active |

Animation frames reference frames **by name**, not by array index. This means
reordering the `frames` array never silently breaks animations.

#### Frame events

The optional `events` array on each animation frame entry lists named strings
that the engine fires the tick that frame becomes active. `AnimatorSystem` calls
`Animator::OnEvent(eventName)` once per name, in array order.

Event names are arbitrary strings — the engine does not interpret them. Game code
registers a callback on the `Animator` component and responds however it likes:

```cpp
anim.OnEvent = [&](std::string_view ev)
{
    if (ev == "FootStep") audio.PlayOneShot("sfx/step_grass");
    if (ev == "AttackHit") SpawnHitParticles(playerPos);
};
```

Conventions used in `player.json`:

| Event | Placement | Intended use |
|-------|-----------|--------------|
| `FootStep` | Walk frames 1 & 4 (each foot strike) | Footstep SFX, dust particle |
| `AttackHit` | Attack frame 2 (impact frame) | Hit SFX, screen shake, hitbox active |
| `Land` | Fall frame 3 (landing frame) | Land SFX, impact particle |

Events fire only on frame **transitions** — not when the clip first starts at
frame 0, and not on repeated ticks that stay on the same frame.

---

### `tile_grid` *(optional)*

Defines a uniform grid layout for tileset sheets where every cell is the same
size. Tiles are indexed left-to-right, top-to-bottom starting at 0.

```json
"tile_grid": {
  "tile_width":  16,
  "tile_height": 16
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `tile_width` | yes | Width of each tile in pixels |
| `tile_height` | yes | Height of each tile in pixels |

`tile_grid` and `frames` can coexist in the same file. A tileset might define
a grid for the bulk of its tiles and also list a few named frames for
special-purpose tiles that game code needs to look up by name.

---

## Which sections to include

A file may use any combination of the optional sections:

| Use case | Sections needed |
|----------|-----------------|
| Uniform tileset | `tile_grid` |
| Named sprite atlas (no animation) | `frames` |
| Animated character | `frames` + `animations` |
| Animated character on a tile grid | `tile_grid` + `frames` + `animations` |

---

## Carrot import

Importing sheets in the Carrot engine's similar format (integer frame indices,
no version) is planned - an explicit import, never auto-detected (blueprint
Phase 12). Until then a `.json` without a `"version"` is rejected.
