// The demo's conversations (game/assets/dialog), played headlessly: the
// elder's gold check, gift and map, remembered on the next talk, and the
// guard who knows whether you've met her.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/dialog/DialogLibrary.h>
#include <arcbit/dialog/DialogRunner.h>
#include <arcbit/save/SaveData.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace Arcbit;

namespace
{
struct Fixture
{
    DialogLibrary            library;
    SaveData                 data;
    std::vector<std::string> calls;
    DialogRunner             runner;

    Fixture()
    {
        Loc::Clear();
        REQUIRE(library.AddFolder(ArcbitTests::SourcePath("game/assets/dialog")) >= 2);
    }
    ~Fixture() { Loc::Clear(); }

    void Talk(const char* id)
    {
        runner.Start(library.Get(id),
                     { .Data = &data, .Call = [this](const std::string_view m) { calls.emplace_back(m); } });
    }
    [[nodiscard]] std::string Key() const { return runner.GetLine().Key; }
    void                      Next() { runner.Next(); }
    [[nodiscard]] std::vector<std::string> Choices() const
    {
        std::vector<std::string> ids;
        for (const auto& c : runner.GetChoices()) ids.push_back(c.Id);
        return ids;
    }
    void Pick(const std::string& id)
    {
        const auto& choices = runner.GetChoices();
        const auto  it      = std::ranges::find(choices, id, &DialogRunner::Choice::Id);
        REQUIRE(it != choices.end());
        runner.Choose(static_cast<u32>(it - choices.begin()));
    }
};
} // namespace

TEST_CASE("The demo's elder: a gold check, a gift, a map, remembered next time", "[dialog][demo][shipped]")
{
    Fixture f;
    for (const std::string& id : f.library.GetAllIds()) CHECK(f.library.GetIssues(id).empty());

    f.Talk("elder");
    CHECK(f.Key() == "dialog.elder.start.welcome");
    CHECK(f.runner.GetLine().Name == "Elder Maren");
    f.Next();
    CHECK(f.Key() == "dialog.elder.start.chest");
    f.Next();
    CHECK(f.Key() == "dialog.elder.ask.what");
    f.Next();
    CHECK(f.Choices() == std::vector<std::string>{ "map_poor", "blessing", "bye" }); // no gold yet

    f.Pick("map_poor");
    CHECK(f.runner.GetLine().Text == "You have 0 gold. Come back with five - the chest might help.");
    f.Next();
    f.Next(); // "What can I do for you?" again
    f.Pick("blessing");
    CHECK(f.Key() == "dialog.elder.blessing.blessed");
    CHECK(f.data.Get<i64>("gold", 0) == 10);
    CHECK(f.calls == std::vector<std::string>{ "Bless" });
    f.Next();
    f.Next();
    CHECK(f.Choices() == std::vector<std::string>{ "map", "bye" }); // blessed once; now she can afford it

    f.Pick("map");
    CHECK(f.data.Get<i64>("gold", 0) == 5);
    CHECK(f.Key() == "dialog.elder.map.map_given");
    f.Next();
    CHECK(f.Key() == "dialog.elder.farewell.farewell");
    f.Next();
    CHECK_FALSE(f.runner.IsRunning());
    CHECK(f.runner.GetLastChoice() == "map");

    f.Talk("elder"); // she remembers
    CHECK(f.Key() == "dialog.elder.again_map.map_use");
    f.Next();
    f.Next();
    CHECK(f.Choices() == std::vector<std::string>{ "bye" });
    f.runner.Cancel(); // Esc: "Goodbye", and her farewell
    CHECK(f.Key() == "dialog.elder.farewell.farewell");
}

TEST_CASE("The demo's guard knows whether you've met the elder", "[dialog][demo][shipped]")
{
    Fixture f;
    f.Talk("guard");
    CHECK(f.Key() == "dialog.guard.start.halt");
    f.data.Set("elder.met", true);
    f.Talk("guard");
    CHECK(f.Key() == "dialog.guard.friendly.friendly");
}
