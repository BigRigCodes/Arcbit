#include "TestSupport.h"

#include <arcbit/core/Loc.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace Arcbit;

namespace
{
// Loc is a global table; start and end every test empty.
struct LocScope
{
    LocScope() { Loc::Clear(); }
    ~LocScope() { Loc::Clear(); }
};
} // namespace

TEST_CASE("Loc resolves keys and falls back to the key itself", "[core][loc]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    REQUIRE(Loc::Load(dir.Write("en.json", R"({ "ui.ok": "OK", "ui.count": 3 })")));

    CHECK(Loc::Get("ui.ok") == "OK");
    CHECK(Loc::Get("ui.missing") == "ui.missing");
    CHECK(Loc::Get("ui.count") == "ui.count"); // non-string values are ignored
    CHECK(&Loc::Get("ui.missing") == &Loc::Get("ui.missing")); // stable reference for missing keys
}

TEST_CASE("Later locale files override earlier ones; Clear empties the table", "[core][loc]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    REQUIRE(Loc::Load(dir.Write("base.json", R"({ "a": "base-a", "b": "base-b" })")));
    REQUIRE(Loc::Load(dir.Write("patch.json", R"({ "b": "patched-b" })")));
    CHECK(Loc::Get("a") == "base-a");
    CHECK(Loc::Get("b") == "patched-b");

    Loc::Clear();
    CHECK(Loc::Get("a") == "a");
}

TEST_CASE("Unreadable locale files fail without touching the table", "[core][loc]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    REQUIRE(Loc::Load(dir.Write("good.json", R"({ "k": "v" })")));
    CHECK_FALSE(Loc::Load(dir.File("missing.json").string()));
    CHECK_FALSE(Loc::Load(dir.Write("bad.json", R"({ "k": )")));
    CHECK(Loc::Get("k") == "v");
}

TEST_CASE("The shipped English locale loads and covers the engine UI", "[core][loc][shipped]")
{
    LocScope scope;
    REQUIRE(Loc::Load(ArcbitTests::SourcePath("engine/assets/locale/en.json").string()));
    CHECK(Loc::Get("ui.mainmenu.title") != "ui.mainmenu.title");
}

namespace
{
// Locale folders for the engine and a game, each file written as given.
struct Folders
{
    ArcbitTests::TempDir dir;
    std::filesystem::path Engine = dir.File("engine"), Game = dir.File("game");

    Folders()
    {
        std::filesystem::create_directories(Engine);
        std::filesystem::create_directories(Game);
    }
    void Write(const std::filesystem::path& folder, const char* name, const char* json) const
    {
        std::ofstream(folder / name, std::ios::binary) << json;
    }
    void AddBoth() const
    {
        Loc::AddFolder(Engine);
        Loc::AddFolder(Game);
    }
};
} // namespace

TEST_CASE("Locale folders: nested keys, the game's strings win, English fills the gaps", "[core][loc]")
{
    LocScope scope;
    Folders  f;
    f.Write(f.Engine, "en.json", R"({ "ui": { "ok": "OK", "quit": "Quit", "back": "Back" } })");
    f.Write(f.Engine, "fr.json", R"({ "ui": { "ok": "D'accord", "quit": "Quitter" } })");
    f.Write(f.Game, "en.json", R"({ "ui.quit": "Leave" })");
    f.Write(f.Game, "fr.json", R"({ "ui.quit": "Partir" })");
    f.AddBoth();
    REQUIRE(Loc::SetLanguage("en"));
    CHECK(Loc::Get("ui.ok") == "OK");
    CHECK(Loc::Get("ui.quit") == "Leave");

    REQUIRE(Loc::SetLanguage("fr"));
    CHECK(Loc::GetLanguage() == "fr");
    CHECK(Loc::Get("ui.ok") == "D'accord");
    CHECK(Loc::Get("ui.quit") == "Partir");
    CHECK(Loc::Get("ui.back") == "Back"); // not translated yet: English
    CHECK(Loc::Get("ui.nowhere") == "ui.nowhere");
    CHECK_FALSE(Loc::Has("ui.nowhere"));
}

TEST_CASE("A regional language falls back to its language, then English", "[core][loc]")
{
    LocScope scope;
    Folders  f;
    f.Write(f.Engine, "en.json", R"({ "a": "en-a", "b": "en-b", "c": "en-c" })");
    f.Write(f.Engine, "pt.json", R"({ "a": "pt-a", "b": "pt-b" })");
    f.Write(f.Engine, "pt-BR.json", R"({ "a": "br-a" })");
    f.AddBoth();
    REQUIRE(Loc::SetLanguage("pt_br")); // case and separator don't matter
    CHECK(Loc::GetLanguage() == "pt-BR");
    CHECK(Loc::Get("a") == "br-a");
    CHECK(Loc::Get("b") == "pt-b");
    CHECK(Loc::Get("c") == "en-c");
}

TEST_CASE("An unknown language changes nothing", "[core][loc]")
{
    LocScope scope;
    Folders  f;
    f.Write(f.Engine, "en.json", R"({ "a": "en-a" })");
    f.AddBoth();
    REQUIRE(Loc::SetLanguage("en"));
    const u64 generation = Loc::GetGeneration();
    CHECK_FALSE(Loc::SetLanguage("xx"));
    CHECK(Loc::GetLanguage() == "en");
    CHECK(Loc::GetGeneration() == generation);
    CHECK(Loc::Get("a") == "en-a");
}

TEST_CASE("Locales list English first, with $locale names and fonts", "[core][loc]")
{
    LocScope scope;
    Folders  f;
    f.Write(f.Engine, "en.json", R"({ "$locale": { "name": "English" }, "a": "x" })");
    f.Write(f.Engine, "ja.json", R"({ "a": "y" })");
    f.Write(f.Game, "ja.json", R"({ "$locale": { "name": "Nihongo", "fonts": ["../fonts/JP.otf"] } })");
    f.Write(f.Game, "de.json", R"({ "a": "z" })");
    f.AddBoth();

    const auto locales = Loc::GetLocales();
    REQUIRE(locales.size() == 3);
    CHECK(locales[0].Code == "en");
    CHECK(locales[0].Name == "English");
    CHECK(locales[1].Code == "de");
    CHECK(locales[1].Name == "de"); // no $locale: the code
    CHECK(locales[2].Name == "Nihongo");
    REQUIRE(locales[2].Fonts.size() == 1);
    CHECK(locales[2].Fonts[0] == (f.dir.File("fonts") / "JP.otf").lexically_normal().generic_string());
    CHECK_FALSE(Loc::Has("$locale.name")); // the block isn't strings
}

TEST_CASE("Switching language moves the generation; a game folder added late still wins", "[core][loc]")
{
    LocScope scope;
    Folders  f;
    f.Write(f.Engine, "en.json", R"({ "a": "engine" })");
    f.Write(f.Engine, "fr.json", R"({ "a": "moteur" })");
    Loc::AddFolder(f.Engine);
    REQUIRE(Loc::SetLanguage("en"));
    CHECK(Loc::Get("a") == "engine");

    const u64 before = Loc::GetGeneration();
    REQUIRE(Loc::SetLanguage("fr"));
    CHECK(Loc::GetGeneration() != before);

    f.Write(f.Game, "fr.json", R"({ "a": "jeu" })");
    Loc::AddFolder(f.Game);
    CHECK(Loc::Get("a") == "jeu");
}
