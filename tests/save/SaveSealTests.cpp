// Sealed saves: what SaveSeal produces and refuses, and SaveManager sealing
// its slots (both build modes, forced on or off here so either build tests both).

#include "TestSupport.h"

#include <arcbit/save/SaveManager.h>
#include <arcbit/save/SaveSeal.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
const std::string Json = R"({ "format": 1, "data": { "gold": 120 } })";

std::vector<u8> ReadAll(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

void WriteAll(const std::filesystem::path& path, const std::vector<u8>& bytes)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool Contains(const std::vector<u8>& bytes, const std::string& text)
{
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

SaveManager SealingManager(const ArcbitTests::TempDir& dir)
{
    SaveManager saves(dir.File("saves"));
    saves.SealSaves        = true;
    saves.AcceptPlainSaves = false; // as a release build
    saves.SetSealKey("test-game-secret");
    return saves;
}
} // namespace

TEST_CASE("A sealed save opens back to the same JSON", "[save][seal]")
{
    const SaveSeal::Key   key    = SaveSeal::DeriveKey("secret");
    const std::vector<u8> sealed = SaveSeal::Seal(Json, key);
    CHECK(SaveSeal::IsSealed(sealed));
    CHECK_FALSE(Contains(sealed, "gold")); // a text editor sees noise
    CHECK(SaveSeal::Open(sealed, key) == Json);
}

TEST_CASE("Sealing the same save twice gives different bytes", "[save][seal]")
{
    const SaveSeal::Key key = SaveSeal::DeriveKey("secret");
    CHECK(SaveSeal::Seal(Json, key) != SaveSeal::Seal(Json, key)); // a fresh nonce each time
}

TEST_CASE("Any changed byte, or another key, fails the check", "[save][seal]")
{
    const SaveSeal::Key   key    = SaveSeal::DeriveKey("secret");
    const std::vector<u8> sealed = SaveSeal::Seal(Json, key);

    CHECK_FALSE(SaveSeal::Open(sealed, SaveSeal::DeriveKey("other")));
    for (const size_t at : { size_t{ 8 }, size_t{ 9 }, size_t{ 20 }, sealed.size() / 2, sealed.size() - 1 }) {
        INFO("byte " << at); // the format byte, the nonce, the data, the tag
        std::vector<u8> edited = sealed;
        edited[at] ^= 0x01;
        CHECK_FALSE(SaveSeal::Open(edited, key));
    }
    const std::vector<u8> truncated(sealed.begin(), sealed.begin() + 30);
    CHECK_FALSE(SaveSeal::Open(truncated, key));
    CHECK_FALSE(SaveSeal::Open(std::vector<u8>(Json.begin(), Json.end()), key)); // not sealed at all
}

TEST_CASE("Keys come from the secret", "[save][seal]")
{
    CHECK(SaveSeal::DeriveKey("a") == SaveSeal::DeriveKey("a"));
    CHECK(SaveSeal::DeriveKey("a") != SaveSeal::DeriveKey("b"));
}

TEST_CASE("A sealing SaveManager writes noise and reads it back", "[save][seal]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves = SealingManager(dir);
    saves.Data().Set("gold", 120);
    REQUIRE(saves.Write(SaveSlot::Manual(1), saves.Capture({ "Village", "Village", 1, 1.0 })));

    CHECK_FALSE(Contains(ReadAll(saves.PathOf(SaveSlot::Manual(1))), "gold"));
    const std::optional<SaveGame> game = saves.Read(SaveSlot::Manual(1));
    REQUIRE(game);
    CHECK(game->Data.Get<i64>("gold", 0) == 120);
}

TEST_CASE("An edited sealed save is refused and its backup loads", "[save][seal]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                saves = SealingManager(dir);
    const SaveSlot             slot  = SaveSlot::Manual(1);
    saves.Data().Set("gold", 10);
    REQUIRE(saves.Write(slot, saves.Capture({ "earlier", "", 1, 0.0 })));
    saves.Data().Set("gold", 50);
    REQUIRE(saves.Write(slot, saves.Capture({ "later", "", 2, 0.0 })));

    std::vector<u8> bytes = ReadAll(saves.PathOf(slot));
    bytes[bytes.size() / 2] ^= 0x20; // a cheat attempt on the newest save
    WriteAll(saves.PathOf(slot), bytes);

    const std::optional<SaveGame> game = saves.Read(slot);
    REQUIRE(game);
    CHECK(game->Meta.Name == "earlier");
}

TEST_CASE("Release-style managers refuse plain saves; debug-style read both", "[save][seal]")
{
    const ArcbitTests::TempDir dir;
    SaveManager                plainWriter(dir.File("saves"));
    plainWriter.SealSaves = false;
    REQUIRE(plainWriter.Write(SaveSlot::Quick(), plainWriter.Capture({ "hand-written", "", 1, 0.0 })));

    SaveManager release = SealingManager(dir);
    CHECK_FALSE(release.Read(SaveSlot::Quick())); // a plain JSON "save" would be a free cheat

    SaveManager debug(dir.File("saves"));
    debug.AcceptPlainSaves = true;
    CHECK(debug.Read(SaveSlot::Quick()));
}
