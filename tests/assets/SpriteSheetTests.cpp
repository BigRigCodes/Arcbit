// Sprite format (docs/sprite-format.md) parsing via SpriteSheet::FromJson —
// no render device: textures are stood in for by a fake handle and size.

#include "TestSupport.h"

#include <arcbit/assets/SpriteSheet.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace
{
const TextureHandle FakeTexture = TextureHandle::Make(1, 0);

SpriteSheet Parse(const json& j, const u32 texW = 128, const u32 texH = 64)
{
    return SpriteSheet::FromJson(j, FakeTexture, texW, texH);
}

json Minimal() { return { { "version", "1.0" }, { "texture", "sheet.png" } }; }
} // namespace

TEST_CASE("Version must be present and major version 1", "[assets][sprite-format]")
{
    json j = Minimal();
    CHECK(Parse(j).IsValid());

    for (const char* accepted : { "1", "1.0", "1.7" }) {
        j["version"] = accepted;
        CHECK(Parse(j).IsValid());
    }
    for (const char* rejected : { "2.0", "10.0", "", "1x", "v1.0" }) {
        INFO(rejected);
        j["version"] = rejected;
        CHECK_FALSE(Parse(j).IsValid());
    }
    j["version"] = 1.0; // numbers aren't valid version strings
    CHECK_FALSE(Parse(j).IsValid());
    j.erase("version");
    CHECK_FALSE(Parse(j).IsValid());
}

TEST_CASE("An invalid or empty texture rejects the sheet", "[assets][sprite-format]")
{
    CHECK_FALSE(SpriteSheet::FromJson(Minimal(), TextureHandle::Invalid(), 64, 64).IsValid());
    CHECK_FALSE(SpriteSheet::FromJson(Minimal(), FakeTexture, 0, 64).IsValid());
}

TEST_CASE("Frames convert pixel rects to UVs and read pivots", "[assets][sprite-format]")
{
    json j      = Minimal();
    j["frames"] = json::array({
        { { "name", "idle" }, { "rect", { { "x", 32 }, { "y", 16 }, { "w", 32 }, { "h", 16 } } },
          { "pivot", { { "x", 0.5 }, { "y", 1.0 } } } },
        { { "name", "plain" }, { "rect", { { "x", 0 }, { "y", 0 }, { "w", 16 }, { "h", 16 } } } },
    });
    const SpriteSheet sheet = Parse(j); // 128 × 64 texture

    const auto idle = sheet.GetFrame("idle");
    REQUIRE(idle.has_value());
    CHECK(idle->UV.U0 == 0.25f);
    CHECK(idle->UV.V0 == 0.25f);
    CHECK(idle->UV.U1 == 0.5f);
    CHECK(idle->UV.V1 == 0.5f);
    CHECK(idle->Pivot == Vec2{ 0.5f, 1.0f });

    CHECK(sheet.GetFrame("plain")->Pivot == Vec2{ 0.5f, 0.5f }); // default pivot: center
    CHECK_FALSE(sheet.GetFrame("missing").has_value());
}

TEST_CASE("Tile grids index left-to-right, top-to-bottom", "[assets][sprite-format]")
{
    json j         = Minimal();
    j["tile_grid"] = { { "tile_width", 32 }, { "tile_height", 32 } };
    const SpriteSheet sheet = Parse(j); // 128 × 64 → 4 × 2 tiles

    CHECK(sheet.TileCount() == 8);
    CHECK(sheet.TileColumns() == 4);
    CHECK(sheet.TileRows() == 2);
    CHECK(sheet.TilePixelSize() == Vec2{ 32.0f, 32.0f });

    const auto t5 = sheet.GetTile(5); // column 1, row 1
    REQUIRE(t5.has_value());
    CHECK(t5->U0 == 0.25f);
    CHECK(t5->V0 == 0.5f);
    CHECK(sheet.GetTile(1, 1)->U0 == t5->U0);

    CHECK_FALSE(sheet.GetTile(8).has_value());
    CHECK_FALSE(sheet.GetTile(4, 0).has_value()); // column out of range
}

TEST_CASE("A tile larger than the texture yields no grid", "[assets][sprite-format]")
{
    json j         = Minimal();
    j["tile_grid"] = { { "tile_width", 256 }, { "tile_height", 256 } };
    const SpriteSheet sheet = Parse(j);
    CHECK(sheet.IsValid());
    CHECK(sheet.TileCount() == 0);
}

TEST_CASE("Animations keep frame order, durations, loop flag, and events", "[assets][sprite-format]")
{
    json j          = Minimal();
    j["animations"] = json::array({
        { { "name", "walk" }, { "loop", true },
          { "frames", json::array({ { { "frame", "walk_0" }, { "duration_ms", 120 }, { "events", { "FootStep" } } },
                                    { { "frame", "walk_1" }, { "duration_ms", 80 } } }) } },
        { { "name", "die" }, { "frames", json::array({ { { "frame", "die_0" }, { "duration_ms", 300 } } }) } },
    });
    const SpriteSheet sheet = Parse(j);

    const AnimationClip* walk = sheet.GetAnimation("walk");
    REQUIRE(walk != nullptr);
    CHECK(walk->Loop);
    REQUIRE(walk->Frames.size() == 2);
    CHECK(walk->Frames[0].FrameName == "walk_0");
    CHECK(walk->Frames[0].DurationMs == 120);
    CHECK(walk->Frames[0].Events == std::vector<std::string>{ "FootStep" });
    CHECK(walk->Frames[1].Events.empty());

    CHECK_FALSE(sheet.GetAnimation("die")->Loop); // loop defaults to false
    CHECK(sheet.GetAnimation("missing") == nullptr);
}

TEST_CASE("pixels_per_unit controls world size", "[assets][sprite-format]")
{
    json j = Minimal();
    SECTION("absent: a sprite the size of a tile is one tile")
    {
        const SpriteSheet sheet = Parse(j);
        CHECK(sheet.PixelsPerUnit() == 0.0f);
        CHECK(sheet.ToWorldSize(32.0f, 32.0f, 32.0f) == Vec2{ 32.0f, 32.0f });
        CHECK(sheet.ToWorldSize(64.0f, 32.0f, 32.0f) == Vec2{ 64.0f, 32.0f });
    }
    SECTION("explicit: hi-res art still occupies one tile")
    {
        j["pixels_per_unit"]    = 64;
        const SpriteSheet sheet = Parse(j);
        CHECK(sheet.ToWorldSize(64.0f, 64.0f, 32.0f) == Vec2{ 32.0f, 32.0f });
        CHECK(sheet.ToWorldSize(128.0f, 64.0f, 32.0f) == Vec2{ 64.0f, 32.0f });
    }
}

TEST_CASE("Every shipped sprite sheet parses and its animations reference real frames", "[assets][sprite-format][shipped]")
{
    const std::filesystem::path root = ArcbitTests::SourcePath("game/assets");
    int                         checked = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto& p = entry.path();
        if (p.extension() != ".json" || p.string().find(".tileatlas") != std::string::npos) continue;

        const json j = json::parse(std::ifstream(p));
        if (!j.contains("frames") && !j.contains("tile_grid")) continue; // not a sprite sheet
        INFO(p.string());
        ++checked;

        // Real texture sizes need the image; a large stand-in is enough to
        // validate structure (UV math is covered above).
        const SpriteSheet sheet = SpriteSheet::FromJson(j, FakeTexture, 4096, 4096, p.string());
        REQUIRE(sheet.IsValid());
        for (const auto& clip : j.value("animations", json::array()))
            for (const auto& frame : clip.at("frames")) {
                if (!j.contains("frames")) continue; // tile-grid-only sheets name frames differently
                INFO("animation " << clip.at("name") << " frame " << frame.at("frame"));
                CHECK(sheet.GetFrame(frame.at("frame").get<std::string>()).has_value());
            }
    }
    CHECK(checked > 0);
}
