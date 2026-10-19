# Tilemaps

A scene's ground, walls and scenery are a `TileMap`: chunks of tile ids in
three layers, drawn through the sprite batcher, colliding through the physics
tile rectangles (`docs/physics.md`) and casting shadows from the lights that ask
for them. Tile types come from atlases - a texture plus a `.tileatlas.json` of
per-tile properties - and a map is saved as an `.arcmap`.

Source: `engine/include/arcbit/tilemap/` (`TileMap.h`, `TileDef.h`,
`TileAtlas.h`), `engine/src/TileMap.cpp`, `TileAtlas.cpp`, and the
`TilemapRender` / `LightRender` systems in `Systems.cpp`.

---

## Tile ids and atlases

A tile id is a `u32`; `0` is **empty** (transparent, passable, blocks
nothing). A map can use several atlases, each owning a contiguous id range from
its `base_id`:

```
grass atlas, base_id 1    ids 1 .. 400
water atlas, base_id 401  ids 401 ..
```

Within an atlas, a tile's `local_id` counts its cells from 0, left to right then
top to bottom, so its id is `base_id + local_id`. Every cell of an atlas is a
tile; only tiles with properties need an entry in the atlas file.

---

## TileDef

The properties of one tile id:

```cpp
struct TileAnimFrame
{
    u32 TileX      = 0;   // atlas cell shown for this frame
    u32 TileY      = 0;
    u32 DurationMs = 150;
};

struct TileDef
{
    bool Solid       = false; // blocks movement (feeds the tile colliders)
    bool BlocksLight = false; // casts shadows - independent of Solid
    bool Flat        = false; // an Objects-layer tile that always sorts below entities (sticks, leaves, decals)

    std::vector<TileAnimFrame> Animation; // flip-book frames; empty = none
    Vec2 UVScroll = {};                    // UV offset per second; continuous flow

    std::vector<DirectionArc> BlockedFrom = {};                   // empty = blocks from every direction
    u32                       Layer       = CollisionLayers::Wall; // collision layer
};
```

`TileMap::FindTileDef(id)` returns it (null for a tile without properties),
and `IsSolid(x, y)` / `BlocksLight(x, y)` check every layer at a cell.

---

## Layers and draw order

Three layers, stored together in each chunk:

| Layer | Holds | Sorted at |
|-------|-------|-----------|
| `Ground` (0) | terrain, floors, water, paths | `-1000000` - behind everything |
| `Objects` (1) | trunks, walls, furniture | the tile's bottom edge (world Y), so it sorts with characters; a `Flat` tile at `-999999` |
| `Overlay` (2) | canopies, rooftops | `1000000` - in front of everything |

Sprites draw in ascending `Layer`. An Objects tile's layer is the world Y of its
bottom edge, so a character drawn with **its own feet's Y as its layer** sorts
against them: above a trunk it's covered by it, below it covers it. A tree
splits across the layers - canopy in Overlay, trunk in Objects, roots in Ground.

Entity sprites don't do this by themselves: `SpriteRenderer.Layer` is fixed
until something sets it. A character that walks among Objects tiles sets its
layer to its feet's Y as it moves (the World demo's player does each tick:
`sr->Layer = static_cast<i32>(position.Y + scale.Y * 0.5f)`).

---

## Animated tiles

| Kind | Examples | How |
|------|----------|-----|
| Continuous flow | ocean, lava, rivers, conveyors | `uv_scroll` on the tile |
| Discrete states | spinning coins, blinking lights, a whirlpool | `animation` (flip-book) on the tile |
| Props that do more | campfires, torches, chests | an entity: `SpriteRenderer` + `Animator` (+ light, sound, script) |

If the texture itself seems to move, scroll it; if the tile cycles between
distinct pictures, flip-book it; if it lights, sounds or reacts, make it an
entity.

**UV scroll** offsets the tile's UVs by `elapsed * uv_scroll` (wrapped), so a
seamlessly tiling texture glides with no frame boundaries. Every tile of an id
scrolls in phase - all the water flows together. The texture must tile
seamlessly, or the scroll exposes a repeating seam; a slight diagonal scroll
looks less mechanical than a pure horizontal one.

**Flip-book** frames name cells of the same atlas, with durations. Placing the
tile's id plays the sequence - the frame cells needn't be placed anywhere. Each
tile's phase is offset by its position (`(x * 37 + y * 53) % cycle`), so a
field of them doesn't blink in lockstep. A tile with both uses the flip-book.
The shipped example is the whirlpool in `water.tileatlas.json`, in the
Triggers demo.

Neither mode keeps per-tile state: both resolve from a global clock as the
tile is drawn.

---

## Chunks and the TileMap

The map is a sparse grid of 16x16 chunks, keyed by chunk coordinate, so it's
effectively unbounded; a chunk holds one id per cell per layer (3 KB). Chunks
are created as tiles are set; cells outside every chunk are empty.

```cpp
TileMap& map = scene.GetTileMap();
map.SetTile(x, y, 1, treeTrunkId);           // layer 0 Ground, 1 Objects, 2 Overlay
const u32  id    = map.GetTile(x, y, 0);
const Vec2 world = map.TileToWorld(x, y);    // the tile's centre
map.WorldToTile(world, x, y);
```

Tile coordinates map to chunks with floor division, so negative tiles land in
negative chunks. Tile `t`'s centre is at `t * tileSize`, and it spans half a
tile either side.

Setting a tile rebuilds that chunk's collision rectangles. Once a scene has
loaded, the map records the tiles that change (`BeginTrackingChanges` /
`GetChanges`), which is how a save keeps a dug hole or an opened gate
(`docs/saves.md`).

Atlases are registered before tiles that use them: `LoadMap` does it from the
map file; code can call `LoadAtlasJson(baseId, path, ...)` or `RegisterAtlas`.

---

## File formats

### Atlas (`.tileatlas.json`)

```json
{
    "version": "1.0",
    "texture": "assets/textures/Water+.png",
    "tile_width": 16,
    "tile_height": 16,
    "tiles": [
        { "local_id": 7,  "uv_scroll": { "x": 0.08, "y": 0.04 }, "blocks_light": true },
        { "local_id": 12, "solid": true, "layer": 32, "blocked_from": [ { "center": -90, "half_width": 90 } ] },
        { "local_id": 30, "flat": true },
        { "local_id": 156, "animation": [ { "local_id": 156, "duration_ms": 200 }, { "local_id": 157 } ] }
    ]
}
```

| Key            | Type                          | Default  | Meaning |
|----------------|-------------------------------|----------|---------|
| `local_id`     | integer                       | required | The cell; the tile's id is `base_id + local_id` |
| `solid`        | bool                          | `false`  | Blocks movement (feeds the greedy-meshed tile colliders) |
| `blocks_light` | bool                          | `false`  | Casts shadows |
| `flat`         | bool                          | `false`  | Objects-layer tile that always sorts below entities |
| `uv_scroll`    | `{ "x", "y" }`                | `0, 0`   | UV offset per second |
| `layer`        | integer bitmask               | `Wall`   | Collision layer (`CollisionLayers.h` bit values) |
| `blocked_from` | `[{ "center", "half_width" }]` | `[]`     | Directional arcs in degrees; empty = blocks from every direction |
| `animation`    | `[{ "local_id", "duration_ms" }]` | `[]` | Flip-book frames (cells of this atlas); `duration_ms` defaults to 150 |

A flip-book frame without a `local_id`, or outside the atlas, is dropped with a
warning.

### Map (`.arcmap`)

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

`TileMap::SaveMap` writes it; `LoadMap` loads each atlas, then the tile size and
the chunks. Each layer array is row-major, 16 x 16 = 256 ids, and the writer
always writes all 256. On load a missing layer is empty and a short array
leaves the rest empty; a chunk with more than 256 ids, a negative or
non-integer id, or non-integer `x` / `y` rejects the whole file, leaving the
map untouched.

A scene file names its map (`"tilemap"`) and can change tile properties for that
scene only (`"tile_overrides"`); entities live in the scene file, not the map,
so one map can back several scenes ([scenes.md](scenes.md)). The scene's
`config.tile_size` is the map's tile size too.

---

## Rendering

`TilemapRender` is the first render-collect system, ahead of entity sprites. It
walks only the chunks the camera can see, skips tiles outside the view, resolves
each visible tile's UVs (flip-book frame, UV scroll) and pushes it into the
frame's sprite list with its layer (above). Tiles go through the same batched
sprite renderer as everything else - no separate pipeline. The debug overlay
counts chunks and tiles drawn.

`TileMap::CollectGridDebugDraw` draws tile edges, for the demos' grid toggle.

---

## Shadows

A light with `LightEmitter.CastsShadows` is shadowed by tiles with
`BlocksLight`. Each frame, for each such light (up to 8), the CPU casts 256
rays around it through the tile grid (a DDA walk), recording how far each gets
before a light-blocking tile - a one-dimensional polar shadow map. The sprite
shader looks up the map for its angle from the light and compares distances,
averaging three neighboring angles (PCF) to soften the edge. Lights without
the flag light everything in their radius.

Shadows are hard-edged at a tile's resolution. Light layers (overhead lights
that ignore shadows), a GPU occlusion grid and wider soft shadows are open
items in the blueprint (Phase 19).
