#include "TestSupport.h"

#include <arcbit/tilemap/TileMap.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace Arcbit;
using nlohmann::json;

namespace
{
constexpr u32 Ground  = 0;
constexpr u32 Objects = 1;
constexpr u32 Overlay = 2;
} // namespace

// ---------------------------------------------------------------------------
// Tile storage
// ---------------------------------------------------------------------------

TEST_CASE("SetTile / GetTile across chunks and negative coordinates", "[tilemap]")
{
    TileMap tm;
    tm.SetTile(0, 0, Ground, 5);
    tm.SetTile(-1, -1, Objects, 6);   // chunk (-1, -1)
    tm.SetTile(17, 3, Overlay, 7);    // chunk (1, 0)
    tm.SetTile(-17, 40, Ground, 8);   // chunk (-2, 2)

    CHECK(tm.GetTile(0, 0, Ground) == 5);
    CHECK(tm.GetTile(-1, -1, Objects) == 6);
    CHECK(tm.GetTile(17, 3, Overlay) == 7);
    CHECK(tm.GetTile(-17, 40, Ground) == 8);
    CHECK(tm.GetTile(0, 0, Objects) == 0);      // layers are independent
    CHECK(tm.GetTile(500, 500, Ground) == 0);   // unallocated chunk reads empty
    CHECK(tm.GetChunks().size() == 4);
}

TEST_CASE("Tile to world coordinates and back", "[tilemap]")
{
    TileMap tm;
    tm.SetTileSize(32.0f);
    CHECK(tm.TileToWorld(0, 0) == Vec2{ 0.0f, 0.0f }); // tile centers sit on multiples of the tile size
    CHECK(tm.TileToWorld(2, -1) == Vec2{ 64.0f, -32.0f });

    i32 x = 0, y = 0;
    tm.WorldToTile({ 70.0f, -40.0f }, x, y);
    CHECK(x == 2);
    CHECK(y == -1);

    // Regression: WorldToTile used floor (corner convention) while tiles are
    // centered on TileToWorld, so the left / top half of every tile mapped to
    // its neighbor. Tile 0 spans [-16, 16).
    tm.WorldToTile({ 15.9f, -15.9f }, x, y);
    CHECK(x == 0);
    CHECK(y == 0);
    tm.WorldToTile({ 16.0f, -16.1f }, x, y);
    CHECK(x == 1);
    CHECK(y == -1);

    for (const i32 t : { -3, 0, 5 }) {
        tm.WorldToTile(tm.TileToWorld(t, -t), x, y);
        CHECK(x == t);
        CHECK(y == -t);
    }
}

TEST_CASE("IsSolid and BlocksLight look through every layer", "[tilemap]")
{
    TileMap tm;
    TileDef rock{};
    rock.Solid = true;
    TileDef glass{};
    glass.BlocksLight = true;
    tm.RegisterTile(10, rock);
    tm.RegisterTile(11, glass);

    tm.SetTile(0, 0, Objects, 10);
    tm.SetTile(1, 0, Overlay, 11);
    tm.SetTile(2, 0, Ground, 99); // no TileDef registered → default (not solid)

    CHECK(tm.IsSolid(0, 0));
    CHECK_FALSE(tm.BlocksLight(0, 0));
    CHECK(tm.BlocksLight(1, 0));
    CHECK_FALSE(tm.IsSolid(1, 0));
    CHECK_FALSE(tm.IsSolid(2, 0));
    CHECK(tm.FindTileDef(99) == nullptr);
}

// ---------------------------------------------------------------------------
// .tileatlas.json tile entries
// ---------------------------------------------------------------------------

TEST_CASE("ParseTileDef reads every tile property", "[tilemap][tileatlas-format]")
{
    const json tile = json::parse(R"({
        "local_id": 3, "solid": true, "blocks_light": true, "flat": true,
        "uv_scroll": { "x": 0.25, "y": -0.5 },
        "layer": 32,
        "blocked_from": [ { "center": -90, "half_width": 45 }, { "center": 180 } ]
    })");
    const TileDef def = TileMap::ParseTileDef(tile);

    CHECK(def.Solid);
    CHECK(def.BlocksLight);
    CHECK(def.Flat);
    CHECK(def.UVScroll == Vec2{ 0.25f, -0.5f });
    CHECK(def.Layer == CollisionLayers::Prop);
    REQUIRE(def.BlockedFrom.size() == 2);
    CHECK(def.BlockedFrom[0] == DirectionArc{ -90.0f, 45.0f });
    CHECK(def.BlockedFrom[1] == DirectionArc{ 180.0f, 180.0f }); // half_width defaults to full coverage
}

TEST_CASE("ParseTileDef keeps defaults for missing keys", "[tilemap][tileatlas-format]")
{
    const TileDef def = TileMap::ParseTileDef(json::parse(R"({ "local_id": 0 })"));
    const TileDef defaults{};
    CHECK(def.Solid == defaults.Solid);
    CHECK(def.BlocksLight == defaults.BlocksLight);
    CHECK(def.Flat == defaults.Flat);
    CHECK(def.UVScroll == defaults.UVScroll);
    CHECK(def.Layer == CollisionLayers::Wall);
    CHECK(def.BlockedFrom.empty());
}

TEST_CASE("Every shipped tile atlas parses", "[tilemap][tileatlas-format][shipped]")
{
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(ArcbitTests::SourcePath("game/assets/tilemaps"))) {
        if (!entry.path().string().ends_with(".tileatlas.json")) continue;
        INFO(entry.path().string());
        ++checked;
        const json j = json::parse(std::ifstream(entry.path()));
        CHECK(j.at("version").get<std::string>().starts_with("1"));
        CHECK(j.at("tile_width").get<u32>() > 0);
        CHECK(j.at("tile_height").get<u32>() > 0);
        CHECK_FALSE(j.at("texture").get<std::string>().empty());
        for (const auto& tile : j.value("tiles", json::array())) {
            CHECK(tile.contains("local_id"));
            CHECK_NOTHROW((void)TileMap::ParseTileDef(tile));
        }
    }
    CHECK(checked > 0);
}

// ---------------------------------------------------------------------------
// .arcmap documents
// ---------------------------------------------------------------------------

TEST_CASE("Map JSON round-trips tile data", "[tilemap][arcmap-format]")
{
    TileMap source;
    source.SetTileSize(48.0f);
    source.SetTile(0, 0, Ground, 1);
    source.SetTile(15, 15, Objects, 2);   // last cell of chunk (0, 0)
    source.SetTile(-3, 20, Overlay, 400); // chunk (-1, 1)

    const json doc = source.ToJson();
    CHECK(doc.at("version") == "1.0");
    CHECK(doc.at("chunks").size() == 2);
    for (const auto& c : doc.at("chunks")) CHECK(c.at("ground").size() == ChunkSize * ChunkSize);

    TileMap loaded;
    REQUIRE(loaded.LoadTilesFromJson(doc));
    CHECK(loaded.GetTileSize() == 48.0f);
    CHECK(loaded.GetTile(0, 0, Ground) == 1);
    CHECK(loaded.GetTile(15, 15, Objects) == 2);
    CHECK(loaded.GetTile(-3, 20, Overlay) == 400);
    CHECK(loaded.GetTile(1, 0, Ground) == 0);
}

TEST_CASE("Map JSON tolerates missing and short layers", "[tilemap][arcmap-format]")
{
    TileMap    tm;
    const json doc = json::parse(R"({ "chunks": [ { "x": 2, "y": -1, "objects": [0, 7] } ] })");
    REQUIRE(tm.LoadTilesFromJson(doc));
    CHECK(tm.GetTile(33, -16, Objects) == 7); // chunk (2, -1), cell index 1
    CHECK(tm.GetTile(32, -16, Ground) == 0);
    CHECK(tm.GetTileSize() == 32.0f);         // tile_size absent: unchanged
}

TEST_CASE("Malformed map JSON is rejected without modifying the map", "[tilemap][arcmap-format]")
{
    // Regression: an oversized layer array used to write past the chunk.
    json oversized          = json::parse(R"({ "chunks": [ { "x": 0, "y": 0 } ] })");
    oversized["chunks"][0]["ground"] = std::vector<u32>(ChunkSize * ChunkSize + 1, 3);

    const json cases[] = {
        oversized,
        json::parse(R"({ "chunks": [ { "x": 0, "y": 0, "ground": [1, -2] } ] })"),     // negative id
        json::parse(R"({ "chunks": [ { "x": 0, "y": 0, "ground": [1.5] } ] })"),       // non-integer
        json::parse(R"({ "chunks": [ { "x": 0, "y": 0, "ground": "nope" } ] })"),      // not an array
        json::parse(R"({ "chunks": [ { "x": 0.5, "y": 0 } ] })"),                      // fractional coord
        json::parse(R"({ "chunks": [ { "y": 0 } ] })"),                                // missing x
        json::parse(R"({ "chunks": { "x": 0 } })"),                                    // chunks not an array
        json::parse(R"([1, 2, 3])"),                                                   // not an object
        // A valid chunk followed by a bad one: nothing may be applied.
        json::parse(R"({ "tile_size": 64, "chunks": [ { "x": 5, "y": 5, "ground": [9] }, { "x": 0, "y": 0, "ground": [-1] } ] })"),
    };

    for (const json& doc : cases) {
        INFO(doc.dump());
        TileMap tm;
        tm.SetTile(0, 0, Ground, 42);
        CHECK_FALSE(tm.LoadTilesFromJson(doc));
        CHECK(tm.GetTile(0, 0, Ground) == 42);
        CHECK(tm.GetTile(80, 80, Ground) == 0);
        CHECK(tm.GetTileSize() == 32.0f);
        CHECK(tm.GetChunks().size() == 1);
    }
}

TEST_CASE("Loading tiles invalidates cached colliders", "[tilemap][arcmap-format]")
{
    TileMap tm;
    TileDef wall{};
    wall.Solid = true;
    tm.RegisterTile(1, wall);
    tm.SetTile(0, 0, Objects, 1);
    REQUIRE(tm.GetChunkColliders(0, 0).size() == 1); // builds the cache

    // A chunk listed with no layers replaces the existing one with an empty chunk.
    REQUIRE(tm.LoadTilesFromJson(json::parse(R"({ "chunks": [ { "x": 0, "y": 0 } ] })")));
    CHECK(tm.GetTile(0, 0, Objects) == 0);
    CHECK(tm.GetChunkColliders(0, 0).empty());
}
