// Save files, headless, against a temp folder: SaveData values, the save
// document, slots on disk (crash-safe writes and the .bak fallback),
// migrations and participants.

#include "TestSupport.h"

#include <arcbit/save/SaveManager.h>

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

// ---------------------------------------------------------------------------
// Slots on disk
// ---------------------------------------------------------------------------

TEST_CASE("A slot round-trips the game's data and participants", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    i32                        potions = 2;
    saves.RegisterParticipant(
        "inventory", [&](nlohmann::json& s) { s["potions"] = potions; },
        [&](const nlohmann::json& s) { potions = s.value("potions", 0); });
    saves.Data().Set("gold", 120);

    REQUIRE(saves.Write(SaveSlot::Manual(1), saves.Capture(Meta("first", 100))));
    CHECK(std::filesystem::exists(dir.File("saves") / "slot_01.arcsave"));

    potions = 0;
    saves.Data().Clear();
    const std::optional<SaveGame> game = saves.Read(SaveSlot::Manual(1));
    REQUIRE(game);
    saves.Restore(*game);
    CHECK(potions == 2);
    CHECK(saves.Data().Get<i64>("gold", 0) == 120);
    CHECK(game->Meta == Meta("first", 100));
}

TEST_CASE("A participant added after a save was made reads an empty section", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    REQUIRE(saves.Write(SaveSlot::Quick(), saves.Capture(Meta("old", 1))));

    bool           called = false;
    nlohmann::json seen;
    saves.RegisterParticipant("quests", {}, [&](const nlohmann::json& s) { called = true; seen = s; });
    saves.Restore(*saves.Read(SaveSlot::Quick()));
    CHECK(called);
    CHECK(seen == nlohmann::json::object());
}

TEST_CASE("A damaged or missing slot falls back to its backup", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    const SaveSlot             slot = SaveSlot::Manual(2);
    REQUIRE(saves.Write(slot, saves.Capture(Meta("older", 1))));
    REQUIRE(saves.Write(slot, saves.Capture(Meta("newer", 2)))); // "older" becomes the .bak

    Overwrite(saves.PathOf(slot), "{ not json"); // damaged
    std::optional<SaveGame> game = saves.Read(slot);
    REQUIRE(game);
    CHECK(game->Meta.Name == "older");

    std::filesystem::remove(saves.PathOf(slot)); // a crash between Write's renames
    game = saves.Read(slot);
    REQUIRE(game);
    CHECK(game->Meta.Name == "older");
}

TEST_CASE("Slots list newest first, and delete with their backups", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    REQUIRE(saves.Write(SaveSlot::Manual(1), saves.Capture(Meta("a", 100))));
    REQUIRE(saves.Write(SaveSlot::Manual(1), saves.Capture(Meta("a2", 300)))); // leaves a .bak
    REQUIRE(saves.Write(SaveSlot::Auto(1), saves.Capture(Meta("b", 200))));
    REQUIRE(saves.Write(SaveSlot::Quick(), saves.Capture(Meta("c", 50))));

    const std::vector<SaveSlotInfo> list = saves.List();
    REQUIRE(list.size() == 3);
    CHECK(list[0].Meta.Name == "a2");
    CHECK(list[1].Meta.Name == "b");
    CHECK(list[2].Slot == SaveSlot::Quick());

    CHECK(saves.Delete(SaveSlot::Manual(1)));
    CHECK_FALSE(saves.Exists(SaveSlot::Manual(1))); // the .bak went too
    CHECK(saves.List().size() == 2);
}

TEST_CASE("A thumbnail is scaled down beside its save, listed, and deleted with it", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    saves.ThumbnailWidth = 32;
    REQUIRE(saves.Write(SaveSlot::Manual(2), saves.Capture(Meta("a", 100))));

    const std::vector<u8> frame(128 * 72 * 4, 200); // a 16:9 gray frame
    REQUIRE(saves.WriteThumbnail(SaveSlot::Manual(2), 128, 72, frame));
    const std::filesystem::path png = saves.ThumbnailOf(SaveSlot::Manual(2));
    CHECK(png.filename() == "slot_02.png");

    // The PNG header: signature, then IHDR's big-endian width and height.
    std::vector<u8> head(24);
    {
        std::ifstream file(png, std::ios::binary); // closed again before the delete below
        REQUIRE(file.read(reinterpret_cast<char*>(head.data()), 24));
    }
    CHECK(head[1] == 'P');
    CHECK(head[19] == 32); // width
    CHECK(head[23] == 18); // height: the aspect kept

    CHECK(saves.List().front().Thumbnail == png);
    saves.Delete(SaveSlot::Manual(2));
    CHECK_FALSE(std::filesystem::exists(png));
}

TEST_CASE("Slots name themselves for logs and events", "[save]")
{
    CHECK(ToString(SaveSlot::Manual(3)) == "manual_3");
    CHECK(ToString(SaveSlot::Auto(1)) == "auto_1");
    CHECK(ToString(SaveSlot::Quick()) == "quick");
}

TEST_CASE("Autosaves fill the empty slots, then replace the oldest", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    saves.AutoSlots = 3;
    CHECK(saves.NextAutoSlot() == SaveSlot::Auto(1));
    REQUIRE(saves.Write(SaveSlot::Auto(1), saves.Capture(Meta("1", 300))));
    REQUIRE(saves.Write(SaveSlot::Auto(2), saves.Capture(Meta("2", 100))));
    CHECK(saves.NextAutoSlot() == SaveSlot::Auto(3));
    REQUIRE(saves.Write(SaveSlot::Auto(3), saves.Capture(Meta("3", 200))));
    CHECK(saves.NextAutoSlot() == SaveSlot::Auto(2)); // the oldest
}

// ---------------------------------------------------------------------------
// Migrations
// ---------------------------------------------------------------------------

TEST_CASE("Older saves are migrated step by step on read", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    saves.Data().Set("coins", 7);
    REQUIRE(saves.Write(SaveSlot::Manual(1), saves.Capture(Meta("v1", 1)))); // game version 1

    // Version 2 renamed "coins" to "gold"; version 3 doubled everyone's gold.
    saves.CurrentGameVersion = 3;
    saves.RegisterMigration(1, [](nlohmann::json& save) {
        save["data"]["gold"] = save["data"]["coins"];
        save["data"].erase("coins");
    });
    saves.RegisterMigration(2, [](nlohmann::json& save) { save["data"]["gold"] = save["data"]["gold"].get<i64>() * 2; });

    const std::optional<SaveGame> game = saves.Read(SaveSlot::Manual(1));
    REQUIRE(game);
    CHECK(game->GameVersion == 3);
    CHECK(game->Data.Get<i64>("gold", 0) == 14);
    CHECK_FALSE(game->Data.Has("coins"));
}

TEST_CASE("Saves from a newer game or with a missing migration aren't loaded", "[save]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves(dir.File("saves"));
    saves.CurrentGameVersion = 2;
    REQUIRE(saves.Write(SaveSlot::Manual(1), saves.Capture(Meta("old", 1))));
    REQUIRE(saves.Write(SaveSlot::Manual(1), saves.Capture(Meta("new", 2)))); // version 2, "old" is the .bak

    saves.CurrentGameVersion = 1; // an older build of the game reading it
    CHECK_FALSE(saves.Read(SaveSlot::Manual(1))); // refused - and not swapped for the older .bak

    saves.CurrentGameVersion = 3; // a newer build with no 2 -> 3 step registered
    CHECK_FALSE(saves.Read(SaveSlot::Manual(1)));
}
