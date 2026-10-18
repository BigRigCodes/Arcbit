// The localization checker (arcbit-loc) and the pseudo-locale.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/core/LocCheck.h>
#include <arcbit/core/Utf8.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
struct LocScope
{
    LocScope() { Loc::Clear(); }
    ~LocScope() { Loc::Clear(); }
};

void Write(const std::filesystem::path& file, const char* text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}
} // namespace

TEST_CASE("Pseudo accents letters outside placeholders, pads and brackets", "[core][loc]")
{
    const std::string pseudo = Loc::Pseudo("Save {count} files");
    CHECK(pseudo.front() == '[');
    CHECK(pseudo.back() == ']');
    CHECK(pseudo.find("{count}") != std::string::npos); // placeholders still fill
    CHECK(pseudo.find("Save") == std::string::npos);    // the words don't read as English
    CHECK(pseudo.ends_with(" ~~~~~~~]"));               // 18 characters, 35% longer: 7
    CHECK(Utf8::Length(pseudo) > 18 + 7);
    CHECK(Loc::Pseudo("") == "[]");
}

TEST_CASE("The pseudo-locale follows English, reloads included", "[core][loc]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    const auto           en = dir.File("en.json");
    Write(en, R"({ "a": "Hello" })");
    Loc::AddFolder(dir.File(""));
    Loc::AddPseudoLocale();
    REQUIRE(Loc::FindLocale(Loc::PseudoCode));
    REQUIRE(Loc::SetLanguage(Loc::PseudoCode));
    CHECK(Loc::Get("a") == Loc::Pseudo("Hello"));
    CHECK(Loc::GetMissing().empty());

    Write(en, R"({ "a": "Bye" })");
    std::filesystem::last_write_time(en, std::filesystem::last_write_time(en) + std::chrono::seconds(5));
    CHECK(Loc::CheckReloads() == 1);
    CHECK(Loc::Get("a") == Loc::Pseudo("Bye"));
}

TEST_CASE("Check finds missing and unused keys, raw layout text and coverage", "[core][loc][tools]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    Write(dir.File("locale/en.json"), R"({ "ui": { "used": "x", "unused": "y" }, "coins": { "one": "c", "other": "cs" } })");
    Write(dir.File("locale/fr.json"), R"({ "$locale": { "name": "Francais" }, "ui.used": "z" })");
    Write(dir.File("content/menu.arcui"), "{\n  \"text_key\": \"ui.used\",\n  \"text\": \"Hello there\",\n  \"text\": \"<\"\n}\n");
    Write(dir.File("content/shop.lua"), "local a = Loc.Format(\"coins\", { count = 1 })\nlocal b = Loc.Get('ui.nope')\n");
    Write(dir.File("content/code.cpp"), "label->SetTextKey(\"ui.used\");\n");

    const std::filesystem::path locales[] = { dir.File("locale") };
    const std::filesystem::path content[] = { dir.File("content") };
    const LocCheck::Report      report    = LocCheck::Check(locales, content);

    REQUIRE(report.Missing.size() == 1);
    CHECK(report.Missing[0].Text == "ui.nope");
    CHECK(report.Missing[0].Line == 2);
    CHECK(report.Unused == std::vector<std::string>{ "ui.unused" }); // coins.* count through "coins"
    REQUIRE(report.RawText.size() == 1);
    CHECK(report.RawText[0].Text == "Hello there");

    REQUIRE(report.Languages.size() == 2);
    CHECK(report.Languages[1].Code == "fr");
    CHECK(report.Languages[1].Translated == 1);
    CHECK(report.Languages[1].Total == 4);

    const auto file = nlohmann::ordered_json::parse(LocCheck::Template());
    CHECK(file.begin().key() == "$locale");
    CHECK(file["ui.unused"] == "y");
    CHECK(file.size() == 5);
}

TEST_CASE("The engine and the demos use only keys English has, and translate them all", "[core][loc][tools][shipped]")
{
    LocScope                    scope;
    const std::filesystem::path locales[] = { ArcbitTests::SourcePath("engine/assets/locale"),
                                              ArcbitTests::SourcePath("game/assets/locale") };
    const std::filesystem::path content[] = { ArcbitTests::SourcePath("engine/src"), ArcbitTests::SourcePath("engine/assets"),
                                              ArcbitTests::SourcePath("game/src"), ArcbitTests::SourcePath("game/assets") };
    const LocCheck::Report      report    = LocCheck::Check(locales, content);
    for (const auto& use : report.Missing) FAIL_CHECK(use.Text << " (" << use.File << ":" << use.Line << ") isn't in English");
    for (const auto& raw : report.RawText) FAIL_CHECK("raw text \"" << raw.Text << "\" in " << raw.File << ":" << raw.Line);
    for (const auto& language : report.Languages) CHECK(language.Translated == language.Total);
}
