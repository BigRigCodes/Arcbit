// Loc for scripts: strings, formatting and plurals, the language, and widget
// text keys.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
using Values = std::vector<std::string>;

struct Fixture
{
    ArcbitTests::TempDir     dir;
    ScriptEngine             lua{ dir.File("") };
    std::vector<std::string> switched; // what Loc.SetLanguage asked the application for

    Fixture()
    {
        Loc::Clear();
        std::ofstream(dir.File("en.json")) << R"({ "$locale": { "name": "English" }, "hi": "Hello {name}",
                                                   "coins": { "one": "{count} coin", "other": "{count} coins" } })";
        std::ofstream(dir.File("fr.json")) << R"({ "$locale": { "name": "Francais" }, "hi": "Bonjour {name}" })";
        Loc::AddFolder(dir.File(""));
        REQUIRE(Loc::SetLanguage("en"));
    }
    ~Fixture() { Loc::Clear(); }
};
} // namespace

TEST_CASE("Scripts read strings, fill placeholders and pick plural forms", "[script][loc]")
{
    Fixture f;
    CHECK(f.lua.Run("return Loc.Get('hi')").Values == Values{ "Hello {name}" });
    CHECK(f.lua.Run("return Loc.Format('hi', { name = 'Ana' })").Values == Values{ "Hello Ana" });
    CHECK(f.lua.Run("return Loc.Format('coins', { count = 1 })").Values == Values{ "1 coin" });
    CHECK(f.lua.Run("return Loc.Format('coins', { count = 3 })").Values == Values{ "3 coins" });
    CHECK(f.lua.Run("return Loc.Format('coins', { count = 2.5 })").Values == Values{ "2.5 coins" });
    CHECK(f.lua.Run("return Loc.Interpolate('{a}+{b}', { a = 1, b = true })").Values == Values{ "1+true" });
    CHECK(f.lua.Run("return Loc.Has('hi'), Loc.Has('nope')").Values == Values{ "true", "false" });
    CHECK(f.lua.Run("return Loc.Get('not.there')").Values == Values{ "not.there" });
}

TEST_CASE("Scripts see the languages and switch through the application", "[script][loc]")
{
    Fixture f;
    CHECK(f.lua.Run("return Loc.Language()").Values == Values{ "en" });
    CHECK(f.lua.Run("local l = Loc.Languages() return #l, l[1].Code, l[2].Name").Values == Values{ "2", "en", "Francais" });

    // Without the application, Loc switches by itself.
    CHECK(f.lua.Run("return Loc.SetLanguage('fr')").Values == Values{ "true" });
    CHECK(Loc::GetLanguage() == "fr");

    f.lua.SetServices({ .SetLanguage = [&](const std::string_view code) {
        f.switched.emplace_back(code);
        return Loc::SetLanguage(code);
    } });
    CHECK(f.lua.Run("return Loc.SetLanguage('en'), Loc.SetLanguage('xx')").Values == Values{ "true", "false" });
    CHECK(f.switched == std::vector<std::string>{ "en", "xx" });
}
