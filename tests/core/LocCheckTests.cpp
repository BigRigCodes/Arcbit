// The localization checker (arcbit-loc) and the pseudo-locale.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/core/LocCheck.h>
#include <arcbit/core/Utf8.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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

TEST_CASE("Check reads dialog files: their English, their problems, stale translations", "[core][loc][tools]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    Write(dir.File("locale/en.json"), R"({ "dialog.elder.a.2": "Changed in en.json", "npc.guard": "Guard" })");
    Write(dir.File("locale/fr.json"),
          R"({ "dialog.elder.a.1": "Bonjour.", "dialog.elder.old.1": "Ancien.", "npc.guard": "Garde" })");
    Write(dir.File("dialog/elder.arcdialog"), R"({ "speakers": { "g": { "key": "npc.guard" }, "m": { "key": "npc.missing" } },
        "nodes": { "a": [ "Hello.", "Bye.", { "goto": "nowhere" } ] } })");

    const std::filesystem::path locales[] = { dir.File("locale") };
    const std::filesystem::path dialogs[] = { dir.File("dialog") };
    const LocCheck::Report      report    = LocCheck::Check(locales, {}, dialogs);

    CHECK(report.HasDialogErrors()); // goto 'nowhere'
    const auto mentions = [&](const char* text, const bool error) {
        return std::ranges::any_of(report.Dialogs, [&](const LocCheck::DialogProblem& p) {
            return p.Dialog == "elder" && p.Error == error && p.Message.find(text) != std::string::npos;
        });
    };
    CHECK(mentions("nowhere", true));
    CHECK(mentions("dialog.elder.a.2", false)); // en.json replaces a line's English
    REQUIRE(report.Missing.size() == 1);       // a nameless speaker's key nobody defines
    CHECK(report.Missing[0].Text == "npc.missing");
    CHECK(report.Unused.empty());               // dialog keys are used by their dialog

    REQUIRE(report.Stale.size() == 1);
    CHECK(report.Stale[0].Code == "fr");
    CHECK(report.Stale[0].Key == "dialog.elder.old.1");
    REQUIRE(report.Languages.size() == 2);
    CHECK(report.Languages[1].Total == 3); // the two lines and the guard
    CHECK(report.Languages[1].Translated == 2);

    const auto file = nlohmann::ordered_json::parse(LocCheck::Template());
    CHECK(file["dialog.elder.a.1"] == "Hello.");
    CHECK(file["dialog.elder.a.2"] == "Changed in en.json");
}

TEST_CASE("The engine and the demos use only keys English has, and translate them all", "[core][loc][tools][shipped]")
{
    LocScope                    scope;
    const std::filesystem::path locales[] = { ArcbitTests::SourcePath("engine/assets/locale"),
                                              ArcbitTests::SourcePath("game/assets/locale") };
    const std::filesystem::path content[] = { ArcbitTests::SourcePath("engine/src"), ArcbitTests::SourcePath("engine/assets"),
                                              ArcbitTests::SourcePath("game/src"), ArcbitTests::SourcePath("game/assets") };
    const std::filesystem::path dialogs[] = { ArcbitTests::SourcePath("game/assets/dialog") };
    const LocCheck::Report      report    = LocCheck::Check(locales, content, dialogs);
    for (const auto& use : report.Missing) FAIL_CHECK(use.Text << " (" << use.File << ":" << use.Line << ") isn't in English");
    for (const auto& raw : report.RawText) FAIL_CHECK("raw text \"" << raw.Text << "\" in " << raw.File << ":" << raw.Line);
    for (const auto& problem : report.Dialogs) FAIL_CHECK("dialog " << problem.Dialog << ": " << problem.Message);
    for (const auto& stale : report.Stale) FAIL_CHECK(stale.Code << " translates " << stale.Key << ", which English lacks");
    for (const auto& language : report.Languages) CHECK(language.Translated == language.Total);
}
