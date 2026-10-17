// Save files, headless: SaveData values and the save document.

#include "TestSupport.h"

#include <arcbit/save/SaveGame.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

using namespace Arcbit;

namespace
{
SaveMetadata Meta(const char* name, const i64 savedAt) { return { name, "Village", savedAt, 12.5 }; }

void Overwrite(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}
} // namespace

// ---------------------------------------------------------------------------
// SaveData
// ---------------------------------------------------------------------------

TEST_CASE("SaveData keeps each kind of value and converts numbers on read", "[save]")
{
    SaveData data;
    data.Set("gold", 120);
    data.Set("speed", 1.5);
    data.Set("quest.dragon", "started");
    data.Set("door.open", true);
    data.Set("spawn", Vec2{ 3, 4 });

    CHECK(data.Get<i64>("gold", 0) == 120);
    CHECK(data.Get<f32>("gold", 0.0f) == 120.0f);
    CHECK(data.Get<i32>("speed", 0) == 1);
    CHECK(data.Get<std::string>("quest.dragon", "") == "started");
    CHECK(data.Get<bool>("door.open", false));
    CHECK(data.Get<Vec2>("spawn", Vec2{}) == Vec2{ 3, 4 });
    CHECK(data.Get<i64>("missing", -1) == -1);
    CHECK(data.Get<i64>("quest.dragon", -1) == -1); // text isn't a number
}

TEST_CASE("SaveData round-trips through JSON and skips what it can't save", "[save]")
{
    SaveData data;
    data.Set("gold", 120);
    data.Set("speed", 1.5);
    data.Set("name", "Hero");
    data.Set("spawn", Vec2{ 3, 4 });

    SaveData back;
    back.FromJson(data.ToJson());
    CHECK(back == data);
    CHECK(std::holds_alternative<f64>(*back.Find("speed"))); // decimals stay decimals

    nlohmann::json odd = data.ToJson();
    odd["nested"]      = { { "a", 1 } };
    back.FromJson(odd);
    CHECK_FALSE(back.Has("nested"));
    CHECK(back.Size() == 4);
}

// ---------------------------------------------------------------------------
// The save document
// ---------------------------------------------------------------------------

TEST_CASE("A save document round-trips", "[save]")
{
    SaveGame game;
    game.GameVersion = 3;
    game.Meta        = Meta("Village - Day 3", 1760000000);
    game.Data.Set("gold", 120);
    game.Sections["inventory"] = { { "potions", 2 } };

    const std::optional<SaveGame> back = SaveGame::FromJson(game.ToJson());
    REQUIRE(back);
    CHECK(back->GameVersion == 3);
    CHECK(back->Meta == game.Meta);
    CHECK(back->Data == game.Data);
    CHECK(back->Sections == game.Sections);
}

TEST_CASE("Saves this build can't read are refused", "[save]")
{
    CHECK_FALSE(SaveGame::FromJson(nlohmann::json::array()));
    CHECK_FALSE(SaveGame::FromJson({ { "format", SaveGame::FormatVersion + 1 } }));
    CHECK_FALSE(SaveGame::FromJson({ { "meta", 5 } })); // no format
}
