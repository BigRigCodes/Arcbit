// DialogRunner walking conversations headlessly, against a SaveData and an
// event bus, with Loc supplying the text; and DialogLibrary reading a folder.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/dialog/DialogLibrary.h>
#include <arcbit/dialog/DialogRunner.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/save/SaveData.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace Arcbit;

namespace
{
// Loc is a global table; start and end every test empty.
struct LocScope
{
    LocScope() { Loc::Clear(); }
    ~LocScope() { Loc::Clear(); }
};

// A dialog with its English in Loc, as DialogLibrary would leave it.
std::shared_ptr<const Dialog> Make(const char* json, const std::string_view id = "elder")
{
    std::vector<DialogIssue> issues;
    auto dialog = ParseDialog(nlohmann::ordered_json::parse(json), id, issues);
    REQUIRE(dialog);
    for (const DialogIssue& issue : issues) {
        INFO(issue.Message);
        CHECK_FALSE(issue.Error);
    }
    Loc::SetStrings(Loc::English, std::string("test:") + std::string(id), dialog->GetStrings());
    return std::make_shared<const Dialog>(std::move(*dialog));
}

const char* Elder = R"({
    "speaker": "elder",
    "speakers": { "elder": { "name": "Elder Maren", "portrait": "elder.png", "voice": "low.wav" } },
    "nodes": {
        "greet": [
            { "say": "Back again?", "if": { "met_elder": true }, "goto": "ask" },
            { "say": "Welcome, traveler.", "set": { "met_elder": true } },
            { "goto": "ask" } ],
        "ask": [
            { "say": "You have {gold} gold.", "portrait": "elder_smile.png" },
            { "choice": [
                { "id": "map", "text": "A map, please.", "if": { "gold": [">=", 5] }, "add": { "gold": -5 }, "goto": "map" },
                { "id": "bye", "text": "Goodbye.", "goto": "end" } ] } ],
        "map": [ { "say": "Mind the cliffs.", "event": { "name": "quest.map", "payload": { "price": 5 } } } ] } })";
} // namespace

TEST_CASE("A runner walks lines and choices, setting and testing SaveData", "[dialog]")
{
    LocScope     scope;
    SaveData     data;
    EventBus     bus;
    i64          price = 0;
    Subscription sub   = bus.Subscribe("quest.map", [&](const Event& e) { price = e.Payload.Get<i64>("price", 0); });
    data.Set("gold", i64{ 7 });

    DialogRunner runner;
    runner.Start(Make(Elder), { .Data = &data, .Events = &bus });
    REQUIRE(runner.GetState() == DialogRunner::State::Line);
    CHECK(runner.GetLine().Text == "Welcome, traveler.");
    CHECK(runner.GetLine().Name == "Elder Maren");
    CHECK(runner.GetLine().Portrait == "elder.png");
    CHECK(runner.GetLine().Voice == "low.wav");
    CHECK(data.Get("met_elder", false)); // a line's actions run as it shows

    runner.Next();
    CHECK(runner.GetLine().Text == "You have 7 gold.");
    CHECK(runner.GetLine().Portrait == "elder_smile.png");
    runner.Next();
    REQUIRE(runner.GetState() == DialogRunner::State::Choice);
    REQUIRE(runner.GetChoices().size() == 2);
    CHECK(runner.GetChoices()[0].Text == "A map, please.");
    CHECK(runner.GetLine().Text == "You have 7 gold."); // still on screen

    runner.Choose(0);
    CHECK(data.Get<i64>("gold", 0) == 2);
    CHECK(runner.GetLine().Text == "Mind the cliffs.");
    bus.Flush();
    CHECK(price == 5);
    runner.Next();
    CHECK(runner.GetState() == DialogRunner::State::Ended);
    CHECK(runner.GetLastChoice() == "map");
}

TEST_CASE("Options that don't hold are hidden, and a second talk is remembered", "[dialog]")
{
    LocScope scope;
    SaveData data;
    data.Set("gold", i64{ 2 });
    data.Set("met_elder", true);

    DialogRunner runner;
    runner.Start(Make(Elder), { .Data = &data });
    CHECK(runner.GetLine().Text == "Back again?");
    runner.Next();
    runner.Next();
    REQUIRE(runner.GetChoices().size() == 1);
    CHECK(runner.GetChoices()[0].Id == "bye");
    CHECK(runner.GetChoices()[0].Option == 1);
    runner.Choose(0);
    CHECK_FALSE(runner.IsRunning());
    CHECK(runner.GetLastChoice() == "bye");
}

TEST_CASE("Branches take the first arm that holds; conditions compare", "[dialog]")
{
    LocScope scope;
    SaveData data;
    auto     d = Make(R"({ "nodes": {
        "a": [ { "branch": [ { "if": { "rank": [">", 2] }, "goto": "high" },
                             { "if": { "any": [ { "title": "knight" }, { "title": "lord" } ] }, "goto": "noble" },
                             { "goto": "low" } ] } ],
        "high": [ "High." ], "noble": [ "Noble." ], "low": [ "Low." ] } })");

    DialogRunner runner;
    const auto   first = [&] {
        runner.Start(d, { .Data = &data });
        return runner.GetLine().Text;
    };
    CHECK(first() == "Low."); // missing keys read as 0 and ""
    data.Set("title", std::string("lord"));
    CHECK(first() == "Noble.");
    data.Set("rank", 2.5);
    CHECK(first() == "High.");
}

TEST_CASE("Lua hooks test conditions and run actions", "[dialog]")
{
    LocScope                 scope;
    std::vector<std::string> ran;
    DialogHost               host{ .Test = [](std::string_view expr) { return expr == "true"; },
                                   .Run  = [&](std::string_view s) { ran.emplace_back(s); },
                                   .Call = [&](std::string_view m) { ran.push_back("call " + std::string(m)); } };
    auto d = Make(R"({ "nodes": { "a": [
        { "say": "Skipped.", "if": { "lua": "false" } },
        { "say": "Shown.", "if": { "lua": "true" }, "lua": "x = 1", "call": "Wave" } ] } })");

    DialogRunner runner;
    runner.Start(d, host);
    CHECK(runner.GetLine().Text == "Shown.");
    CHECK(ran == std::vector<std::string>{ "x = 1", "call Wave" });

    runner.Start(d, {}); // no Lua: the condition fails, the dialog ends
    CHECK(runner.GetState() == DialogRunner::State::Ended);
}

TEST_CASE("Loops of gotos and missing nodes end the dialog", "[dialog]")
{
    LocScope     scope;
    DialogRunner runner;
    runner.Start(Make(R"({ "nodes": { "a": [ { "goto": "b" } ], "b": [ { "goto": "a" } ] } })"), {});
    CHECK(runner.GetState() == DialogRunner::State::Ended);

    runner.Start(Make(R"({ "nodes": { "a": [ "Hi." ] } })", "other"), {}, "nope");
    CHECK(runner.GetState() == DialogRunner::State::Ended);
    runner.Start(nullptr, {});
    CHECK(runner.GetState() == DialogRunner::State::Ended);
}

TEST_CASE("Lines show in the active language, and refresh when it changes", "[dialog]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    REQUIRE(Loc::Load(dir.Write("en.json", R"({ "ui.ok": "OK" })")));
    auto d = Make(R"({ "speakers": { "elder": { "name": "Elder" } }, "speaker": "elder",
                       "nodes": { "a": [ "Hello, {player}." ] } })");
    std::filesystem::create_directories(dir.File("fr"));
    std::ofstream(dir.File("fr/fr.json")) << R"({ "dialog.elder.a.1": "Bonjour, {player}.", "dialog.elder.speaker.elder": "Ancienne" })";
    Loc::AddFolder(dir.File("fr"));

    DialogRunner runner;
    runner.Start(d, { .Args = { { "player", "Ash" } } });
    CHECK(runner.GetLine().Text == "Hello, Ash.");
    REQUIRE(Loc::SetLanguage("fr"));
    runner.RefreshText();
    CHECK(runner.GetLine().Text == "Bonjour, Ash.");
    CHECK(runner.GetLine().Name == "Ancienne");
}

TEST_CASE("A library reads a folder of dialogs by path, English into Loc", "[dialog]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    std::filesystem::create_directories(dir.File("town"));
    dir.Write("elder.arcdialog", R"({ "nodes": { "a": [ "Hello." ] } })");
    dir.Write("town/guard.arcdialog", R"({ "nodes": { "a": [ "Halt.", { "goto": "b" } ] } })"); // b: an error, still loads
    dir.Write("notes.txt", "not a dialog");

    DialogLibrary library;
    CHECK(library.AddFolder(dir.File("")) == 2);
    CHECK(library.GetIds() == std::vector<std::string>{ "elder", "town/guard" });
    CHECK(Loc::Get("dialog.town.guard.a.1") == "Halt.");
    CHECK(library.Get("town/guard")->Nodes.size() == 1);
    CHECK(library.GetIssues("town/guard").size() == 1);
    CHECK(library.Get("missing") == nullptr);

    library.Clear();
    CHECK_FALSE(Loc::Has("dialog.elder.a.1"));
}

TEST_CASE("A library reloads a changed dialog; a runner keeps the one it began", "[dialog]")
{
    LocScope             scope;
    ArcbitTests::TempDir dir;
    const std::string    file = dir.Write("elder.arcdialog", R"({ "nodes": { "a": [ "Old line." ] } })");

    DialogLibrary library;
    REQUIRE(library.AddFolder(dir.File("")) == 1);
    DialogRunner runner;
    runner.Start(library.Get("elder"), {});
    CHECK(library.CheckReloads() == 0);

    dir.Write("elder.arcdialog", R"({ "nodes": { "a": [ "New line.", "And more." ] } })");
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
    CHECK(library.CheckReloads() == 1);
    CHECK(Loc::Get("dialog.elder.a.1") == "New line.");
    CHECK(library.Get("elder")->Nodes[0].Steps.size() == 2);
    CHECK(runner.GetDialog()->Nodes[0].Steps.size() == 1); // its own copy, still whole

    dir.Write("elder.arcdialog", "{ broken");
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10));
    CHECK(library.CheckReloads() == 1);
    CHECK(library.Get("elder")->Nodes[0].Steps.size() == 2); // a bad edit keeps the last good one
    CHECK_FALSE(library.GetIssues("elder").empty());
}
