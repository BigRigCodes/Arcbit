// The console's logic, without its screen: commands, Lua for the rest,
// cheats, history, completion and output.

#include <arcbit/app/DebugConsole.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using namespace Arcbit;
using Strings = std::vector<std::string>;

TEST_CASE("Lines split into words, with quoted text kept whole", "[console]")
{
    CHECK(DebugConsole::Split("  tp  10 -4 ") == Strings{ "tp", "10", "-4" });
    CHECK(DebugConsole::Split(R"(set quest "the dragon" "")") == Strings{ "set", "quest", "the dragon", "" });
}

TEST_CASE("A command runs with its arguments; anything else goes to Lua", "[console]")
{
    DebugConsole console;
    Strings      got, lua;
    console.Register({ .Name = "tp", .Help = "tp <x> <y>", .Run = [&](const DebugConsole::Args& a) { got = a; } });
    console.RunLua = [&](const std::string_view code) { lua.emplace_back(code); };

    console.Execute("tp 3 4");
    console.Execute("print(1 + 1)");
    CHECK(got == Strings{ "3", "4" });
    CHECK(lua == Strings{ "print(1 + 1)" });
    CHECK(console.GetLines().front() == "> tp 3 4");
    CHECK(console.GetHistory() == Strings{ "tp 3 4", "print(1 + 1)" });
}

TEST_CASE("Cheat commands and typed Lua are reported", "[console]")
{
    DebugConsole console;
    Strings      cheats;
    console.OnCheat = [&](const std::string_view what) { cheats.emplace_back(what); };
    console.RunLua  = [](std::string_view) {};
    console.Register({ .Name = "god", .Help = "god", .Cheat = true, .Run = [](const DebugConsole::Args&) {} });
    console.Register({ .Name = "fps", .Help = "fps", .Run = [](const DebugConsole::Args&) {} });
    console.Execute("fps");
    console.Execute("god");
    console.Execute("Save.Set('gold', 1e9)");
    CHECK(cheats == Strings{ "god", "lua" });
}

TEST_CASE("Mistakes print instead of throwing; help and completion list commands", "[console]")
{
    DebugConsole console;
    console.Register({ .Name = "scene", .Help = "scene <name> - switch",
                       .Run  = [](const DebugConsole::Args&) { throw std::runtime_error("no scene 'Nowhere'"); } });
    console.Register({ .Name = "save", .Help = "save <slot>", .Run = [](const DebugConsole::Args&) {} });
    console.Execute("scene Nowhere");
    CHECK(console.GetLines().back() == "scene: no scene 'Nowhere'");
    console.Execute("nope");
    CHECK(console.GetLines().back() == "unknown command 'nope' - try help");

    CHECK(console.Complete("s") == Strings{ "save", "scene" });
    console.Clear();
    console.Execute("help scene");
    CHECK(console.GetLines().back() == "scene <name> - switch");
}

TEST_CASE("Number arguments say what's wrong with them", "[console]")
{
    DebugConsole console;
    console.Register({ .Name = "give", .Help = "give <item> [count]", .Run = [](const DebugConsole::Args& a) {
                          (void)DebugConsole::Integer(a[1], "count", 1);
                      } });
    console.Execute("give key a");
    CHECK(console.GetLines().back() == "give: count must be a whole number, not 'a'");
    console.Execute("give key 3x");
    CHECK(console.GetLines().back() == "give: count must be a whole number, not '3x'");
    console.Execute("give key -5");
    CHECK(console.GetLines().back() == "give: count must be at least 1, not -5");
    console.Execute("give key 99999999999999999999");
    CHECK(console.GetLines().back() == "give: count '99999999999999999999' is too big");

    CHECK(DebugConsole::Integer("12", "count") == 12);
    CHECK(DebugConsole::Decimal("-2.5", "x") == -2.5f);
    CHECK_THROWS_WITH(DebugConsole::Decimal("left", "x"), "x must be a number, not 'left'");
}

TEST_CASE("Output keeps the newest lines", "[console]")
{
    DebugConsole console;
    console.MaxLines = 3;
    console.Print("a\nb");
    console.Print("c\nd");
    CHECK(Strings(console.GetLines().begin(), console.GetLines().end()) == Strings{ "b", "c", "d" });
}

TEST_CASE("Suggest predicts a command name, else asks Lua", "[console]")
{
    DebugConsole console;
    console.Register({ .Name = "scene", .Help = "scene", .Run = [](const DebugConsole::Args&) {} });
    CHECK(console.Suggest("he") == "lp");
    CHECK(console.Suggest("help").empty()); // already whole
    CHECK(console.Suggest("").empty());

    console.CompleteLua = [](const std::string_view line) {
        return line == "Save.G" ? Strings{ "et", "etAll" } : Strings{};
    };
    CHECK(console.Suggest("Save.G") == "et"); // the first, sorted
    CHECK(console.Suggest("help sc").empty());
}
