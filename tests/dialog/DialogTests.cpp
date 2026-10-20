// Dialog files: reading .arcdialog JSON into a Dialog, its text keys, and
// the checks that catch an author's mistakes.

#include <arcbit/dialog/Dialog.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>

using namespace Arcbit;

namespace
{
Dialog Parse(const char* json, std::vector<DialogIssue>& issues, const std::string_view id = "elder")
{
    auto dialog = ParseDialog(nlohmann::ordered_json::parse(json), id, issues);
    REQUIRE(dialog);
    return std::move(*dialog);
}

// Whether an issue of that kind mentions all of the words.
bool Mentions(const std::vector<DialogIssue>& issues, const bool error, const std::initializer_list<std::string_view> words)
{
    return std::ranges::any_of(issues, [&](const DialogIssue& i) {
        return i.Error == error && std::ranges::all_of(words, [&](std::string_view w) { return i.Message.contains(w); });
    });
}
} // namespace

TEST_CASE("A dialog reads speakers, lines, choices and the start node", "[dialog]")
{
    std::vector<DialogIssue> issues;
    const Dialog             d = Parse(R"({
        "speaker": "elder", "world": "pause",
        "speakers": { "elder": { "name": "Elder Maren", "portrait": "p/elder.png", "voice": "s/low.wav" } },
        "nodes": {
            "greet": [
                "Welcome.",
                { "say": "The wind is up.", "speaker": null },
                { "choice": [ { "id": "map", "text": "A map?", "goto": "map" }, { "text": "Bye.", "goto": "end" } ] } ],
            "map": [ { "say": "Here.", "set": { "has_map": true }, "add": { "gold": -5 } } ] } })",
                                       issues);
    CHECK(issues.empty());
    CHECK(d.Start == "greet");
    CHECK(d.PauseWorld);
    REQUIRE(d.Nodes.size() == 2);
    CHECK(d.Nodes[0].Id == "greet"); // file order, not sorted

    const auto& greet = d.Nodes[0].Steps;
    REQUIRE(greet.size() == 3);
    CHECK(greet[0].Kind == DialogStepKind::Say);
    CHECK(greet[0].Speaker == "elder"); // the dialog's speaker
    CHECK(greet[1].Speaker.empty());    // null: narration
    CHECK(greet[2].Kind == DialogStepKind::Choice);
    REQUIRE(greet[2].Options.size() == 2);
    CHECK(greet[2].Options[0].Id == "map");
    CHECK(greet[2].Options[1].Goto == "end");

    const DialogStep& map = d.Nodes[1].Steps[0];
    REQUIRE(map.Actions.Set.size() == 1);
    CHECK(map.Actions.Set[0].second == SaveValue{ true });
    CHECK(map.Actions.Add[0].second == SaveValue{ i64{ -5 } });
    CHECK(d.FindSpeaker("elder")->Voice == "s/low.wav");
}

TEST_CASE("Text keys come from the place, an id, or an explicit key", "[dialog]")
{
    std::vector<DialogIssue> issues;
    const Dialog             d = Parse(R"({
        "speakers": { "elder": { "name": "Maren" }, "guard": { "name": "Guard", "key": "npc.guard" } },
        "speaker": "elder",
        "nodes": { "greet": [
            "One.",
            { "say": "Two.", "id": "two" },
            { "say": "Three.", "key": "shared.three" },
            { "set": { "x": 1 } },
            "Four.",
            { "choice": [ { "text": "A" }, { "id": "b", "text": "B" } ] } ] } })",
                                       issues, "town/elder");
    CHECK(issues.empty());
    const auto& s = d.Nodes[0].Steps;
    CHECK(s[0].Key == "dialog.town.elder.greet.1");
    CHECK(s[1].Key == "dialog.town.elder.greet.two");
    CHECK(s[2].Key == "shared.three");
    CHECK(s[4].Key == "dialog.town.elder.greet.2"); // only unnamed text counts
    CHECK(s[5].Options[0].Key == "dialog.town.elder.greet.3");
    CHECK(s[5].Options[1].Key == "dialog.town.elder.greet.b");

    const auto strings = d.GetStrings();
    CHECK(std::ranges::count(strings, std::pair<std::string, std::string>{ "dialog.town.elder.speaker.elder", "Maren" }) == 1);
    CHECK(std::ranges::count(strings, std::pair<std::string, std::string>{ "npc.guard", "Guard" }) == 1);
    CHECK(std::ranges::count(strings, std::pair<std::string, std::string>{ "dialog.town.elder.greet.b", "B" }) == 1);
    CHECK(strings.size() == 2 + 4 + 2);
}

TEST_CASE("Conditions read tests, any, not and lua", "[dialog]")
{
    std::vector<DialogIssue> issues;
    const Dialog             d = Parse(R"({ "nodes": { "a": [
        { "say": "x", "if": { "gold": [">=", 5], "met": true, "any": [ { "a": 1 }, { "b": "yes" } ],
                              "not": { "angry": true }, "lua": "Save.Get('k') == 2" } } ] } })",
                                       issues);
    CHECK(issues.empty());
    const DialogCondition& c = d.Nodes[0].Steps[0].If;
    REQUIRE(c.All.size() == 2);
    CHECK(c.All[0].Key == "gold");
    CHECK(c.All[0].Op == DialogCompare::GreaterEqual);
    CHECK(c.All[0].Value == SaveValue{ i64{ 5 } });
    CHECK(c.All[1].Op == DialogCompare::Equal);
    CHECK(c.Any.size() == 2);
    CHECK(c.Not.size() == 1);
    CHECK(c.Lua == "Save.Get('k') == 2");
}

TEST_CASE("Mistakes are reported: targets, speakers, fields, keys, reachability", "[dialog]")
{
    std::vector<DialogIssue> issues;
    const Dialog             d = Parse(R"({ "speaker": "nobody", "nodes": {
        "a": [ "Hi.", { "goto": "nowhere" }, { "spaeker": "x", "say": "Typo." },
               { "say": "Dup.", "key": "k" }, { "say": "Dup again.", "key": "k" },
               { "choice": [] }, { "if": { "gold": ["=>", 1] }, "goto": "end" }, {} ],
        "lonely": [ "Nobody comes here." ],
        "end": [ "Reserved." ] } })",
                                       issues);
    CHECK(Mentions(issues, true, { "goto 'nowhere'" }));
    CHECK(Mentions(issues, true, { "no speaker 'nobody'" }));
    CHECK(Mentions(issues, false, { "unknown field 'spaeker'" }));
    CHECK(Mentions(issues, true, { "'k' is used twice" }));
    CHECK(Mentions(issues, true, { "step 6", "list of options" }));
    CHECK(Mentions(issues, true, { "step 7", "op one of" }));
    CHECK(Mentions(issues, true, { "step 8", "needs" }));
    CHECK(Mentions(issues, false, { "'lonely'", "nothing leads to it" }));
    CHECK(Mentions(issues, true, { "'end' can't be a node's name" }));
    CHECK(d.Nodes.size() == 2); // still loads what it can
}

TEST_CASE("A dialog with no nodes doesn't load", "[dialog]")
{
    std::vector<DialogIssue> issues;
    CHECK_FALSE(ParseDialog(nlohmann::ordered_json::parse(R"({ "speakers": {} })"), "x", issues));
    CHECK_FALSE(ParseDialog(nlohmann::ordered_json::parse("[1, 2]"), "x", issues));
    CHECK(issues.size() == 2);
}

TEST_CASE("Dialog ids make key prefixes", "[dialog]")
{
    CHECK(DialogKeyPrefix("elder") == "dialog.elder");
    CHECK(DialogKeyPrefix("town/guard") == "dialog.town.guard");
}
