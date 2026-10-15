# Arcbit Tilemap System — Design Document

Phase 19 of the Arcbit blueprint. This document covers the full architecture of
the tilemap system: tile definitions, chunk storage, the tile atlas file format,
the map file format, the render pipeline, camera culling, multiple layers, light
layers, the tile occlusion grid, and shadow casting.

---

## Goals

- Represent arbitrarily large worlds without loading everything into memory.
- Render only what is visible, using the existing sprite batcher.
- Support three logical layers (ground / objects / overlay) that interleave
  correctly with entity sprites via Y-sorting.
- Animate large-area tiles via two modes: **UV scroll** (water, lava — continuous
  texture flow) and **flip-book** (coins, status effects — discrete frame swap);
  specific props (campfires, torches) are ECS entities.
- Drive the point-light shadow system via a per-frame GPU occlusion texture built
  from `light_blocking` tile flags.

---

## Core Concepts

### Tile ID

A `u32` where `0` means **empty** (no tile — transparent, passable, non-blocking).
IDs `>= 1` reference entries in the `TileDef` table loaded from a `TileAtlas`.
IDs are assigned left-to-right, top-to-bottom from the atlas texture, 1-indexed:

```
Atlas texture (4 columns × 2 rows):
  ID 1  ID 2  ID 3  ID 4
  ID 5  ID 6  ID 7  ID 8
```

### TileDef

The static definition for one tile type — immutable after atlas load:

```cpp
struct TileAnimFrame
{
    u32 TileId     = 0;   // which tile ID to display for this frame
    u32 DurationMs = 200; // how long to show it
};

struct TileDef
{
    u32         Id            = 0;
    std::string Name;          // human-readable; used in editor and Lua
    bool        Solid         = false; // blocks entity movement (NavGrid)
    bool        LightBlocking = false; // blocks light ray-march (shadow casting)
    bool        Interactable  = false; // can fire OnInteract (Phase 31)

    // --- Animation (mutually exclusive — use one mode or neither) -----------

    // Flip-book: cycle through discrete tile IDs on a timer.
    // Use for: coins spinning, status effects, anything with distinct visual states.
    // Position-based phase offset is applied automatically (see Animated Tiles).
    std::vector<TileAnimFrame> Animation;

    // UV scroll: continuously offset the sampling UV over time.
    // Use for: water, lava, conveyor belts, flowing rivers.
    // Units: UV space per second — 0.1 scrolls one full texture width in 10 s.
    // The source tile must be seamlessly tileable for this to look correct.
    Vec2 UVScroll = { 0.0f, 0.0f };
};
```

### TileLayer

Three named layers, rendered in order from bottom to top:

```cpp
enum class TileLayer : u32
{
    Ground  = 0, // terrain, floor, water
    Objects = 1, // walls, furniture, tree trunks — Y-sorted with entities
    Overlay = 2, // tree canopies, rooftops — always above everything
    Count   = 3
};
```

Ground and Overlay use fixed sprite layer integers. Objects tiles and all entity
sprites share a **Y-sorted band** so that tall props (tree trunks, fences, walls)
and moving characters depth-sort correctly against each other:

| Layer | Sort key | Examples |
|-------|----------|---------|
| Ground | Fixed: −100 | Grass, stone floor, water, paths |
| Objects | **Y position of tile bottom edge** | Tree trunks, walls, furniture, NPCs, player |
| Overlay | Fixed: +100 | Tree canopies, rooftops, flying effects |

See **Y-Sorting and Entity Depth** below for the full rationale and sort key formula.

---

## Animated Tiles

### Three categories, three solutions

| Category | Examples | Solution |
|----------|----------|----------|
| Continuous flowing tiles | Ocean water, lava, river, conveyor belts | UV scroll on `TileDef` |
| Discrete state tiles | Coins spinning, blinking lights, tide pools | Flip-book on `TileDef` |
| Specific interactive props | Campfires, torches, waterfalls, animated chests | ECS entity (SpriteRenderer + Animator) |

The dividing line between the two tile modes: if the animation looks like the
**texture itself is moving** (waves flowing, lava churning), use UV scroll. If it
looks like the tile is **cycling through distinct states** (coin face → edge →
back), use flip-book. If the object emits light, plays sounds, or reacts to
interaction, it should be an entity regardless.

### UV scroll animation

UV scroll is the right mode for water, lava, and anything designed as a
seamlessly tiling texture that flows continuously. Instead of swapping tile IDs,
the renderer adds a time-based offset to the UV coordinates before sampling —
the texture glides across the quad with no frame boundaries and no visual pop.

```json
{ "id": 5, "name": "ocean_water", "uv_scroll": { "x":  0.04, "y":  0.02 } },
{ "id": 6, "name": "river",       "uv_scroll": { "x":  0.08, "y":  0.0  } },
{ "id": 7, "name": "lava",        "uv_scroll": { "x": -0.03, "y":  0.03 } }
```

The diagonal scroll on ocean water (`x` and `y` both non-zero) is intentional —
pure horizontal scroll looks mechanical; a slight vertical component makes the
surface feel alive.

The renderer applies it at submission time with no per-tile state:

```cpp
UVRect uv = _atlas.GetUV(tileId);
if (def->UVScroll.X != 0.0f || def->UVScroll.Y != 0.0f)
{
    const f32 offsetU = std::fmod(elapsedSeconds * def->UVScroll.X, 1.0f);
    const f32 offsetV = std::fmod(elapsedSeconds * def->UVScroll.Y, 1.0f);
    uv.U0 += offsetU; uv.U1 += offsetU;
    uv.V0 += offsetV; uv.V1 += offsetV;
}
```

**The source tile must tile seamlessly** — if there is a hard edge at the tile
boundary the scroll will expose it as a repeating seam. Water tiles designed for
RPG tilesets (like the Water+ sheet) are almost always seamlessly tileable by
design.

All tiles of the same ID scroll at the same phase — there is no per-tile offset
for UV scroll (unlike flip-book below). This is correct: you want all ocean tiles
to flow in the same direction at the same rate, like a real body of water.

### Flip-book animation

The `TileDef::Animation` array lists frames with per-frame durations. The
renderer maintains a global elapsed-milliseconds counter and resolves the current
frame at render time with no per-tile state:

```json
{
    "id": 12, "name": "coin",
    "animation": [
        { "tile": 12, "duration_ms": 120 },
        { "tile": 13, "duration_ms": 80  },
        { "tile": 14, "duration_ms": 120 },
        { "tile": 13, "duration_ms": 80  }
    ]
}
```

Each frame references a tile ID from the same atlas. The base tile ID is the
first frame; the atlas needs all frames as distinct tile IDs.

#### Phase offset for variation

If all tiles of the same type animate from the same global clock they march in
lockstep, which looks artificial for things like tide pools or blinking gems. A
position-based hash offsets each tile's phase with no per-tile storage:

```cpp
u32 ResolveTileId(const TileDef& def, i32 tileX, i32 tileY, u64 elapsedMs)
{
    if (def.Animation.empty()) return def.Id;

    u32 totalMs = 0;
    for (const auto& f : def.Animation) totalMs += f.DurationMs;

    // Deterministic phase offset from tile position.
    const u32 phaseMs = static_cast<u32>((tileX * 37u + tileY * 53u) % totalMs);
    u32 t = static_cast<u32>((elapsedMs + phaseMs) % totalMs);

    for (const auto& f : def.Animation)
    {
        if (t < f.DurationMs) return f.TileId;
        t -= f.DurationMs;
    }
    return def.Id;
}
```

The constants 37 and 53 are arbitrary primes that spread tile coordinates across
the cycle. UV scroll tiles intentionally skip this offset — all water tiles flow
together.

### Entity-based animated props

Campfires, torches, waterfalls, and animated chests are ECS entities placed at
tile-grid positions. The editor places them via the Tile Object stamp mechanism
(an `interactable` or special-tag cell triggers entity placement). At runtime
they are fully normal entities:

```
campfire entity:
  Transform2D   — snapped to tile center
  SpriteRenderer — flame sprite sheet
  Animator       — cycles flame frames
  LightEmitter   — flickering warm light, LightLayer::World
  AudioSource    — crackling fire loop, Radius = 200
```

The tilemap has no knowledge of these entities. The editor records them in the
scene's entity list alongside NPCs and the player.

---

## Y-Sorting and Entity Depth

### The problem

A tree trunk occupies the Objects layer. A player character is an ECS entity.
Both need to depth-sort against each other: when the player stands above the
trunk they should appear in front of it; when below it, behind it. A fixed sprite
layer integer cannot express this — it is either always in front or always behind.

### The solution: sort by Y position of the bottom edge

Everything in the Objects band — both Objects-layer tile quads and entity sprites
— uses the **world-space Y coordinate of its bottom edge** as its sort key within
the band. Things higher on screen (smaller Y, farther away in top-down
perspective) render first and get covered by things lower on screen.

```
Sort key = Y_bottom_edge_in_world_space
         = tile: (tileY + 1) * tileSize          (bottom edge of tile cell)
         = entity: Transform2D.Position.Y          (pivot is at feet for characters)
```

The sprite batcher sorts all submissions within the Objects band by this key
before drawing. Ground and Overlay tiles bypass the sort entirely and use their
fixed layer integers.

### Why this works for Tile Objects

The tree design splits correctly across layers:

```
[ canopy  ]  → Overlay (+100 fixed) — always renders above everything
[ trunk   ]  → Objects (Y-sorted)   — sorts with the player naturally
[ roots   ]  → Ground  (−100 fixed) — always renders below everything
```

When the player is above the trunk (player Y < trunk Y):
- Player's sort key < trunk's sort key → player renders first → trunk covers player ✓

When the player is below the trunk (player Y > trunk Y):
- Player's sort key > trunk's sort key → trunk renders first → player covers trunk ✓

### NPCs and animals

NPCs, animals, and all other moving characters are ECS entities with
`SpriteRenderer` at the default layer 0. Layer 0 falls inside the Objects Y-sort
band. They automatically depth-sort against Objects-layer tiles and against each
other. **No 4th tilemap layer is needed** — the three existing layers combined
with Y-sorting handle all depth relationships correctly.

### Implementation note

The sprite batcher's sort needs to understand the two-level sort key:
`(band, Y_within_band)`. A practical encoding:

```cpp
// Packs a band integer and a Y position into a single f32 sort key.
// Ground = −100, Objects = Y-position mapped to [−9, +9], Overlay = +100.
f32 SpriteLayer(i32 band, f32 worldY)
{
    if (band != 0) return static_cast<f32>(band); // Ground or Overlay: fixed
    // Objects band: scale worldY into a narrow range that stays between −10 and +10.
    return worldY * 0.0001f; // at 10 000 world units tall, still fits in the band
}
```

The exact encoding is an implementation detail of `SpriteRenderSystem` and the
batcher. The important contract is that everything in the Objects band is sorted
by Y, and Ground/Overlay tiles are not.

---

## Data Model

### TileChunk

A fixed-size 16 × 16 block of tiles, storing one `u32` tile ID per cell per layer.
All three layers are stored together so a single chunk load covers the full stack.

```cpp
struct TileChunk
{
    static constexpr i32 Size = 16;

    // [layer][y * Size + x]; 0 = empty
    u32 Tiles[static_cast<u32>(TileLayer::Count)][Size * Size] = {};
};
```

Chunk memory per chunk: `3 × 256 × 4 bytes = 3 KB`. A 100 × 100 tile map
occupies roughly 7 × 7 = 49 chunks ≈ 147 KB — trivially small.

### TileMap

Owns all chunks and exposes the tile access API. Chunks are allocated on demand
and keyed by a packed 64-bit coordinate so the world is effectively infinite.

```cpp
class TileMap
{
public:
    // --- Asset loading ------------------------------------------------------
    void LoadAtlas(std::string_view path);              // parse TileAtlas JSON
    void LoadMap(std::string_view path);                // parse .arcmap JSON
    void SaveMap(std::string_view path) const;

    // --- Tile access --------------------------------------------------------
    u32  GetTile(i32 tileX, i32 tileY, TileLayer layer) const;
    void SetTile(i32 tileX, i32 tileY, TileLayer layer, u32 tileId);

    // --- Property helpers ---------------------------------------------------
    bool IsSolid(i32 tileX, i32 tileY) const;         // any layer solid?
    bool IsLightBlocking(i32 tileX, i32 tileY) const; // any layer blocking?

    // --- Coordinate conversion ----------------------------------------------
    Vec2 TileToWorld(i32 tileX, i32 tileY) const;     // returns tile center
    void WorldToTile(Vec2 pos, i32& outX, i32& outY) const;

    // --- Lookup -------------------------------------------------------------
    const TileDef* GetTileDef(u32 tileId) const;

    f32 GetTileSize() const { return _tileSize; }

private:
    // Chunk key: upper 32 bits = chunkY, lower 32 bits = chunkX (signed cast to u32).
    static u64 ChunkKey(i32 cx, i32 cy);

    TileChunk&       GetOrCreateChunk(i32 cx, i32 cy);
    const TileChunk* FindChunk(i32 cx, i32 cy) const;

    std::unordered_map<u64, TileChunk> _chunks;
    TileAtlas                          _atlas;
    f32                                _tileSize = 16.0f;
};
```

Tile → chunk mapping:

```
chunkX = floor(tileX / ChunkSize)   (integer division, rounds toward −∞)
chunkY = floor(tileY / ChunkSize)
localX = tileX - chunkX * ChunkSize
localY = tileY - chunkY * ChunkSize
```

Use `std::floor` on the tile coordinate divided by `ChunkSize` so negative tile
indices map correctly to negative chunk coordinates rather than rounding toward 0.

### TileAtlas

Loaded once per map; holds the texture handle, UV table, and TileDef array.

```cpp
struct TileAtlas
{
    TextureHandle            Texture;
    SamplerHandle            Sampler;
    i32                      TileWidth  = 16;
    i32                      TileHeight = 16;
    i32                      Columns    = 0;  // texture width / TileWidth
    std::vector<TileDef>     Defs;            // index = id - 1 (id 0 is empty)

    // Returns nullptr for id == 0 or id out of range.
    const TileDef* GetDef(u32 id) const;

    // UV rect for tile id (top-left origin, normalised 0–1).
    UVRect GetUV(u32 id) const;
};
```

---

## File Formats

### TileAtlas JSON (`.tileatlas.json`)

```json
{
    "version": "1.0",
    "texture": "assets/textures/Water+.png",
    "tile_width": 16,
    "tile_height": 16,
    "tiles": [
        { "local_id": 7,  "uv_scroll": { "x": 0.08, "y": 0.04 }, "blocks_light": true },
        { "local_id": 12, "solid": true, "layer": 32, "blocked_from": [ { "center": -90, "half_width": 90 } ] },
        { "local_id": 30, "flat": true }
    ]
}
```

Parsed by `TileMap::LoadAtlasJson` (file + texture) and `TileMap::ParseTileDef`
(one `tiles` entry → `TileDef`). Only tiles with non-default properties need an
entry; every other cell of the atlas is a plain, non-solid tile.

| Key            | Type                          | Default  | Meaning |
|----------------|-------------------------------|----------|---------|
| `local_id`     | integer                       | required | Cell index within this atlas; the registered tile ID is `base_id + local_id` |
| `solid`        | bool                          | `false`  | Blocks movement (feeds the greedy-meshed tile colliders) |
| `blocks_light` | bool                          | `false`  | Casts shadows |
| `flat`         | bool                          | `false`  | Objects-layer tile that always sorts below entities |
| `uv_scroll`    | `{ "x", "y" }`                | `0, 0`   | Continuous UV offset per second |
| `layer`        | integer bitmask               | `Wall`   | Collision layer (`CollisionLayers.h` bit values) |
| `blocked_from` | `[{ "center", "half_width" }]` | `[]`     | Directional arcs in degrees; empty = blocks from every direction |

Flip-book `TileDef::Animation` is not readable from the atlas JSON yet — it can
only be set from code with `RegisterTile`.

### Map File (`.arcmap`)

```json
{
    "version": "1.0",
    "tile_size": 32,
    "atlases": [
        { "base_id": 1,   "path": "assets/tilemaps/grass.tileatlas.json", "sampler": "nearest" },
        { "base_id": 401, "path": "assets/tilemaps/water.tileatlas.json", "sampler": "linear" }
    ],
    "chunks": [
        {
            "x": 0, "y": 0,
            "ground":  [1,1,1,1, 1,1,1,1, ...],
            "objects": [0,0,4,0, 0,0,0,0, ...],
            "overlay": [0,0,0,0, 0,0,0,0, ...]
        }
    ]
}
```

`TileMap::SaveMap` writes this (via `ToJson`); `TileMap::LoadMap` loads each atlas,
then applies `tile_size` and the chunks via `LoadTilesFromJson`. Each layer array is
row-major with `ChunkSize × ChunkSize = 256` tile IDs — the writer always emits all
256. On load a missing layer is empty and a shorter array leaves the remaining cells
empty, but any chunk with more than 256 entries, a negative or non-integer ID, or
non-integer `x` / `y` rejects the whole file without modifying the map.
Chunks absent from the file have all tiles = 0 (empty); the world outside is
implicitly empty.

---

## Tile Objects

A **Tile Object** is a named, multi-cell, multi-layer tile template — a tree
that is 1 tile wide and 2 tiles tall, a house that spans 6 × 4 tiles across
two layers, a bridge that occupies the Ground and Objects layers simultaneously.

### Runtime vs. editor

Tile Objects exist **only at editor time**. When the editor stamps a Tile Object
onto the map it writes the individual tile IDs into the affected chunk cells
exactly as if you had painted them by hand. The saved `.arcmap` contains only
tile IDs — there is no runtime concept of "this collection of tiles is a tree."
This keeps the runtime simple and makes map serialization a flat array.

The one exception: if a Tile Object contains a tile cell marked `interactable`,
the editor additionally places an **entity** (ECS) at that cell's world position
with the relevant components (e.g., a `DoorTrigger` script). That entity *is* a
runtime concept, stored in the scene's entity list rather than the tile data.

### Definition

Tile Objects are defined in the tileatlas JSON under an `"objects"` key, since
every cell references tile IDs from that same atlas:

```json
{
    "version": "1.0",
    "texture": "overworld_tiles.png",
    "tile_width": 16,
    "tile_height": 16,
    "tiles": [ ... ],

    "objects": [
        {
            "name": "oak_tree",
            "anchor": { "x": 0, "y": 1 },
            "cells": [
                { "dx": 0, "dy":  0, "layer": "overlay",  "tile": 12 },
                { "dx": 0, "dy":  1, "layer": "objects",  "tile": 11, "solid": true, "light_blocking": true }
            ]
        },
        {
            "name": "small_house",
            "anchor": { "x": 1, "y": 3 },
            "cells": [
                { "dx": 0, "dy": 0, "layer": "overlay",  "tile": 20 },
                { "dx": 1, "dy": 0, "layer": "overlay",  "tile": 21 },
                { "dx": 0, "dy": 1, "layer": "objects",  "tile": 22, "solid": true, "light_blocking": true },
                { "dx": 1, "dy": 1, "layer": "objects",  "tile": 22, "solid": true, "light_blocking": true },
                { "dx": 0, "dy": 2, "layer": "objects",  "tile": 23, "solid": true, "light_blocking": true },
                { "dx": 1, "dy": 2, "layer": "objects",  "tile": 24, "interactable": true },
                { "dx": 0, "dy": 3, "layer": "ground",   "tile": 3 },
                { "dx": 1, "dy": 3, "layer": "ground",   "tile": 3 }
            ]
        }
    ]
}
```

| Field | Description |
|-------|-------------|
| `name` | Identifier shown in the editor's object palette |
| `anchor` | The cell the editor cursor snaps to when placing — typically the bottom-center or the logical "root" tile |
| `cells[].dx`, `cells[].dy` | Offset in tiles from the anchor position |
| `cells[].layer` | Which layer this cell writes to (`"ground"`, `"objects"`, `"overlay"`) |
| `cells[].tile` | Tile ID from the atlas |
| `cells[].solid`, `light_blocking`, `interactable` | **Per-cell property overrides** — if omitted, the base `TileDef` for that tile ID applies; if present, they override it for this placement only |

### Per-cell property overrides

This is the mechanism for "the door of a house." The door tile itself (`tile: 24`
above) might be defined in the atlas as a plain visual tile (not interactable by
default), but the house object overrides `interactable: true` for that specific
cell. When the editor stamps the house, it writes the override into the chunk's
property layer rather than the atlas TileDef, so the door works as a trigger
while identical-looking tiles elsewhere are inert.

Property overrides are stored per-cell in the `.arcmap` as a sparse table
alongside the tile ID:

```json
{
    "x": 0, "y": 0,
    "ground":  [...],
    "objects": [...],
    "overlay": [...],
    "overrides": [
        { "x": 5, "y": 12, "layer": "objects", "interactable": true }
    ]
}
```

The runtime `TileMap::IsSolid` / `IsLightBlocking` / `GetTileDef` API checks the
override table before falling back to the base `TileDef`. Overrides are rare
enough that a small `std::vector` per chunk is more cache-friendly than a full
per-cell override array.

### Editor workflow

1. Open the **Object Palette** (populated from `"objects"` in the tileatlas).
2. Select a Tile Object — a preview shows all cells at their relative offsets.
3. Hover over the map — the anchor cell follows the cursor; other cells render
   as a ghost overlay.
4. Click to stamp: all cells are written to their layers and chunks; any
   `interactable` cells also spawn entities at the correct world positions.
5. To remove a placed object, the editor must undo as a unit (all cells
   together) — individual cell erasure is still supported via the normal tile
   painter.

---

## Scene Integration

`Scene` gains a `TileMap` member alongside `World`, `Camera2D`, and `WorldConfig`:

```
Scene
├── World       — ECS entities + systems
├── Camera2D    — active camera
├── WorldConfig — tile size, gravity
└── TileMap     — chunk storage, atlas, occlusion state
```

The Scene is the **merged map** — it is the single runtime object that holds both
the tile data and the placed entities. There is no separate concept of a
"combined map"; the Scene already is that.

`WorldConfig::TileSize` is kept in sync with `TileMap::GetTileSize()` after a
map load so movement systems see the right value.

---

## Scene File Format (`.arcscene`)

Because entities and tiles are both part of a level but stored differently, the
scene file is the single thing the editor saves and the engine loads. It
*references* the tilemap as a separate asset and *embeds* the entity list inline.

```json
{
    "version": "1.0",
    "tilemap": "maps/village.arcmap",
    "world_config": { "tile_size": 16 },
    "entities": [
        {
            "id": "e001",
            "tag": "NPC",
            "template": "Guard NPC",
            "transform": { "x": 96, "y": 64 },
            "overrides": {
                "AIBehavior": { "patrol_path": [[96,64],[160,64],[160,128]] }
            }
        },
        {
            "id": "e002",
            "tag": "Interactive",
            "transform": { "x": 80, "y": 112 },
            "components": [
                { "type": "SpriteRenderer", "texture": "assets/textures/chest.png" },
                { "type": "Interactable",   "on_interact": "scripts/open_chest.lua" }
            ]
        },
        {
            "id": "e003",
            "tag": "Prop",
            "transform": { "x": 32, "y": 48 },
            "components": [
                { "type": "SpriteRenderer", "texture": "assets/textures/campfire.png" },
                { "type": "Animator",       "sheet": "assets/spritesheets/campfire.json", "default_clip": "burn" },
                { "type": "LightEmitter",   "radius": 150, "intensity": 1.2, "layer": "World" },
                { "type": "AudioSource",    "path": "assets/sfx/fire_loop.wav",  "radius": 200 }
            ]
        }
    ]
}
```

### Why tilemap and entity list are separate files

- **The tilemap can be large** — hundreds of chunks, each 3 KB. Keeping it in a
  dedicated `.arcmap` means the editor can stream and dirty-write only the chunks
  that changed rather than rewriting the whole scene file.
- **The tilemap is an asset** — a `.arcmap` can be referenced by two `.arcscene`
  files if you want two variants of the same level with different enemy placements
  (e.g., a day variant and a night variant, or normal vs. hard mode).
- **The entity list is scene-specific** — entities belong to a particular
  play-through of a level, not to the tile geometry.

### Loading at runtime

`Scene::Load` handles both in one call (Phase 23):

```cpp
void Scene::Load(std::string_view scenePath)
{
    // 1. Load tilemap asset.
    const auto& j = ParseJson(scenePath);
    _tilemap.LoadMap(j["tilemap"]);
    _config.TileSize = j["world_config"]["tile_size"];

    // 2. Spawn entities.
    for (const auto& def : j["entities"])
    {
        Entity e = def.contains("template")
            ? _world.SpawnFromTemplate(def["template"], ReadVec2(def["transform"]))
            : _world.CreateEntity();

        ApplyTransform(e, def["transform"]);
        if (def.contains("overrides"))  ApplyOverrides(e, def["overrides"]);
        if (def.contains("components")) ApplyComponents(e, def["components"]);
    }
}
```

### Editor tool → file mapping

| Editor tool | Writes to |
|-------------|-----------|
| Tile painter | `.arcmap` (chunk tile IDs) |
| Tile Object stamp | `.arcmap` (tiles) + `.arcscene` (any spawned entities) |
| Entity placer | `.arcscene` (entity list) |
| Entity property panel | `.arcscene` (component overrides) |

The editor viewport shows tiles and entities together in one unified view, even
though they are saved to different files. This mirrors how Tiled separates tile
layers from object layers but displays both in the same canvas.

---

## Render Pipeline

### TilemapRenderSystem

Registered in the render-collect phase, **before** `SpriteRenderSystem`, so tile
quads land in the batcher before entity sprites. Layer ordering then sorts them
correctly by the sprite layer int.

```
Render-collect phase (per frame):
  TilemapRenderSystem   — emits tile quads into FramePacket.Sprites
  SpriteRenderSystem    — emits entity quads into FramePacket.Sprites
  LightRenderSystem     — emits PointLights + uploads occlusion texture
```

### Camera Culling

Only visible chunks are iterated. The culling bounds in tile space:

```
camLeft   = cameraPos.X - halfViewportWidth  / zoom
camRight  = cameraPos.X + halfViewportWidth  / zoom
camTop    = cameraPos.Y - halfViewportHeight / zoom
camBottom = cameraPos.Y + halfViewportHeight / zoom

minTileX = floor(camLeft   / tileSize) - 1   // one-tile margin for partial tiles
maxTileX = ceil (camRight  / tileSize) + 1
minTileY = floor(camTop    / tileSize) - 1
maxTileY = ceil (camBottom / tileSize) + 1
```

From tile range → chunk range → iterate only present chunks in that range via
the `_chunks` map.

### Per-tile Sprite submission

For each non-zero tile in a visible chunk, resolve the animation mode and push a
`Sprite` into `FramePacket`. The two animation modes are handled separately:
UV scroll adjusts the UV rect in place; flip-book swaps the source tile ID first.

```cpp
const TileDef* def = _atlas.GetDef(tileId);

// --- Flip-book: swap tile ID to get the current frame's UV.
const u32 resolvedId = (def && !def->Animation.empty())
    ? ResolveTileId(*def, tileX, tileY, elapsedMs)
    : tileId;

Sprite s{};
s.Texture  = _atlas.Texture;
s.Sampler  = _atlas.Sampler;
s.UV       = _atlas.GetUV(resolvedId);

// --- UV scroll: shift the UV rect continuously over time.
if (def && (def->UVScroll.X != 0.0f || def->UVScroll.Y != 0.0f))
{
    const f32 offsetU = std::fmod(elapsedSeconds * def->UVScroll.X, 1.0f);
    const f32 offsetV = std::fmod(elapsedSeconds * def->UVScroll.Y, 1.0f);
    s.UV.U0 += offsetU; s.UV.U1 += offsetU;
    s.UV.V0 += offsetV; s.UV.V1 += offsetV;
}

s.Position = TileToWorld(tileX, tileY);   // tile center

// Y-sort: Objects layer uses bottom-edge world Y; Ground and Overlay use fixed values.
const f32 bottomY = static_cast<f32>(tileY + 1) * _tileSize;
s.Layer = (layer == TileLayer::Objects) ? bottomY * 0.0001f
        : (layer == TileLayer::Ground)  ? -100.0f
        :                                  100.0f; // Overlay

FramePacket.Sprites.push_back(s);
```

No new GPU API is needed — tile quads go through the same instanced sprite
batcher as entities.

---

## Light Layers

Phase 19 splits point lights into two categories that differ only in whether
shadow casting applies:

```cpp
enum class LightLayer
{
    World,    // blocked by tile occlusion (walls cast shadows)
    Overhead, // bypasses occlusion — sunlight, moonlight, sky effects
};
```

`LightEmitter` gains a `LightLayer` field (default `World`):

```cpp
struct LightEmitter
{
    f32        Radius     = 200.0f;
    f32        Intensity  = 1.0f;
    Color      LightColor = Color::NaturalLight();
    LightLayer Layer      = LightLayer::World;
};
```

`Overhead` lights are rendered with full radial attenuation but the fragment
shader skips the ray-march, so they illuminate everything in their radius
regardless of walls. Use them for:
- Ambient daylight or moonlight
- Magical sky effects (aurora, meteor glow)
- UI indicators that should always be visible

---

## Tile Occlusion Grid

A single-channel GPU texture that encodes which tiles are light-blocking for the
current visible area. The fragment shader samples it during the shadow ray-march.

### Texture layout

- Format: `R8Unorm` (1 byte per texel; 0.0 = passable, 1.0 = blocking)
- Dimensions: `occlusionWidth × occlusionHeight` tiles, where the dimensions
  cover the visible tile range (clamped to a max of 128 × 128 for GPU budget)
- Origin texel corresponds to `(minVisibleTileX, minVisibleTileY)`

### Update strategy

Rebuilt **every frame** if any tile changed since the last frame, or if the
camera moved enough to shift the visible tile range. Because it covers only the
visible area and the max size is 128 × 128 = 16 KB, a full CPU-side rebuild and
`UpdateBuffer` upload costs roughly 16 µs — acceptable per frame.

A `_occlusionDirty` flag is set by `TileMap::SetTile()`. If the camera hasn't
moved and no tile changed, the GPU texture is reused as-is.

### Shader binding

Uploaded to the light render pass as a new descriptor binding (texture slot 1,
alongside the existing light buffer at slot 0). The fragment shader receives the
visible-area origin and dimensions as a push constant alongside the existing
light data.

---

## Shadow Casting

### World-layer lights — hard shadows

For each fragment lit by a `World`-layer point light, the fragment shader
ray-marches from the **fragment's tile coordinate** toward the **light's tile
coordinate** through the occlusion texture:

```glsl
// Pseudocode — actual GLSL in the light pass fragment shader
vec2 fragTile  = (fragWorldPos - occlusionOrigin) / tileSize;
vec2 lightTile = (lightWorldPos - occlusionOrigin) / tileSize;
vec2 dir       = normalize(lightTile - fragTile);
float dist     = length(lightTile - fragTile);

float stepSize = 0.5; // half-tile steps
float t        = stepSize;
bool  shadowed = false;

while (t < dist)
{
    vec2  samplePos = fragTile + dir * t;
    float occlusion = texture(occlusionTex, samplePos / occlusionSize).r;
    if (occlusion > 0.5) { shadowed = true; break; }
    t += stepSize;
}

float lightContrib = shadowed ? 0.0 : ComputeAttenuation(dist * tileSize, light);
```

### Overhead-layer lights

The while-loop is skipped entirely. `ComputeAttenuation` runs as normal.

### Soft shadows (configurable)

Instead of one ray per fragment, fire N slightly-offset rays and average the
results. N is stored in `project.arcbit` (Phase 34) under `render.shadow_samples`:

| shadow_samples | Quality | Cost |
|:-:|---|---|
| 1 (default) | Hard shadows | Baseline |
| 4 | Soft fringe (0.5-tile penumbra) | ~4× ray cost |
| 8 | Smoother penumbra | ~8× ray cost |

Soft shadows are off by default. The setting is applied as a compile-time
specialization constant in the SPIR-V shader (or a `#define` path) so the
default path pays zero cost for the branch.

---

## System Execution Order (Phase 19 additions)

```
Update phase (unchanged):
  LifetimeSystem
  FreeMovementSystem
  CameraFollowSystem
  AnimatorStateMachineSystem
  AnimatorSystem
  AudioSystem

Render-collect phase:
  TilemapRenderSystem    ← NEW: submits tile quads before entity sprites
  SpriteRenderSystem     — entity sprites at layer 0 (between Objects + Overlay)
  LightRenderSystem      ← UPDATED: uploads occlusion texture, handles LightLayer
```

`TilemapRenderSystem` runs as a render system (receives `Scene& + FramePacket&`)
but reads `scene.GetTileMap()` rather than querying the ECS World.

---

## Implementation Phases

The blueprint items map to discrete implementation steps in this order:

| Step | Blueprint item | What it enables |
|------|---------------|-----------------|
| 1 | Tile definition + TileAtlas | Load and query tile properties |
| 2 | Chunk storage + TileMap API | `SetTile` / `GetTile`, `IsSolid` |
| 3 | Map file load/save | Load `.arcmap`, persist editor changes |
| 4 | Tilemap renderer + culling | Tiles visible on screen |
| 5 | Multiple layers | Ground / Objects / Overlay z-order |
| 6 | Scene integration | `scene.GetTileMap()`, WorldConfig sync |
| 7 | LightLayer enum | Overhead lights bypass shadows |
| 8 | Occlusion grid upload | GPU texture built each frame |
| 9 | Shadow casting (hard) | World lights blocked by walls |
| 10 | Soft shadow option | Quality knob in project.arcbit |

Steps 1–6 form the core map system and are independent of the shadow work (7–10).
The shadow steps require the Vulkan backend to add a new texture binding and
update the light fragment shader — that is the only phase where the graphics
backend is touched.
