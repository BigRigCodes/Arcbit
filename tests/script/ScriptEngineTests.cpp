// The Lua state on its own: running code, the sandbox, contained errors and
// limits, require, and the basic library (print, Log, Vec2).

#include "TestSupport.h"

#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::ContainsSubstring;

namespace
{
using Values = std::vector<std::string>;

void WriteScript(const std::filesystem::path& path, const std::string& code)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << code;
}
} // namespace

TEST_CASE("Run returns an expression's values and keeps globals between calls", "[script]")
{
    ScriptEngine lua;
    CHECK(lua.Run("1 + 2").Values == Values{ "3" });
    CHECK(lua.Run("return 'a', true, nil").Values == Values{ "a", "true", "nil" });
    REQUIRE(lua.Run("x = 5").Ok);
    CHECK(lua.Run("x * 2").Values == Values{ "10" });
}

TEST_CASE("Scripts can't reach the machine", "[script]")
{
    ScriptEngine lua;
    for (const char* name : { "io", "debug", "package", "load", "loadfile", "dofile", "collectgarbage", "os.execute",
                              "os.remove", "os.getenv", "string.dump" })
        CHECK(lua.Run(name).Values == Values{ "nil" });
    CHECK(lua.Run("type(os.time)").Values == Values{ "function" });
    CHECK(lua.Run("getmetatable('')").Values == Values{ "false" }); // the shared string metatable is hidden
}

TEST_CASE("An error is reported with a traceback, not thrown", "[script]")
{
    ScriptEngine       lua;
    const ScriptResult result = lua.Run("local function f() error('boom') end f()");
    CHECK_FALSE(result.Ok);
    CHECK_THAT(result.Error, ContainsSubstring("boom"));
    CHECK_THAT(result.Error, ContainsSubstring("stack traceback"));
    CHECK(lua.Run("1").Ok); // still usable
}

TEST_CASE("A runaway script is stopped, even through pcall", "[script]")
{
    ScriptEngine lua("assets/scripts", ScriptLimits{ .MaxInstructions = 1'000'000 });
    ScriptResult result = lua.Run("while true do end");
    CHECK_FALSE(result.Ok);
    CHECK_THAT(result.Error, ContainsSubstring("ran too long"));

    result = lua.Run("for i = 1, 100 do pcall(function() while true do end end) end return 'escaped'");
    CHECK_FALSE(result.Ok);
    CHECK(lua.Run("1 + 1").Values == Values{ "2" }); // each call gets a fresh budget
}

TEST_CASE("The state can't grow past its memory cap", "[script]")
{
    ScriptEngine       lua("assets/scripts", ScriptLimits{ .MaxMemory = 8u << 20 });
    const ScriptResult result = lua.Run("local t = {} for i = 1, 1e8 do t[i] = i end");
    CHECK_FALSE(result.Ok);
    CHECK_THAT(result.Error, ContainsSubstring("memory"));
    CHECK(lua.GetMemoryUsed() <= 8u << 20);
}

TEST_CASE("require loads modules from the root, once, each in its own environment", "[script]")
{
    const ArcbitTests::TempDir dir;
    WriteScript(dir.File("util/math.lua"), "Leak = 1\nstring.upper = nil\nloads = (loads or 0) + 1\n"
                                           "return { Double = function(x) return x * 2 end }");
    WriteScript(dir.File("loop_a.lua"), "return require('loop_b')");
    WriteScript(dir.File("loop_b.lua"), "return require('loop_a')");
    ScriptEngine lua(dir.File(""));

    CHECK(lua.Run("require('util.math').Double(4)").Values == Values{ "8" });
    CHECK(lua.Run("require('util.math') == require('util.math')").Values == Values{ "true" });
    CHECK(lua.Run("Leak").Values == Values{ "nil" });                // the module's globals are its own
    CHECK(lua.Run("string.upper('a')").Values == Values{ "A" });     // and so are its library copies

    CHECK_THAT(lua.Run("require('../secrets')").Error, ContainsSubstring("isn't a module name"));
    CHECK_THAT(lua.Run("require('missing')").Error, ContainsSubstring("no module"));
    CHECK_THAT(lua.Require("loop_a").Error, ContainsSubstring("requires itself"));
    CHECK(lua.Require("util.math").Ok);
}

TEST_CASE("print and Log go to the output", "[script]")
{
    ScriptEngine             lua;
    std::vector<std::string> lines;
    lua.SetOutput([&](const std::string_view line) { lines.emplace_back(line); });
    REQUIRE(lua.Run("print('hi', 1, nil)").Ok);
    REQUIRE(lua.Run("Log.Warn('careful', 2)").Ok);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "hi\t1\tnil");
    CHECK_THAT(lines[1], ContainsSubstring("[warn]") && ContainsSubstring("careful 2"));
}

TEST_CASE("Vec2 is a value type with arithmetic", "[script]")
{
    ScriptEngine lua;
    CHECK(lua.Run("Vec2(3, 4):Length()").Values == Values{ "5.0" });
    CHECK(lua.Run("tostring(Vec2(1, 2) + Vec2(2, 2) * 2)").Values == Values{ "Vec2(5, 6)" });
    CHECK(lua.Run("(-Vec2(1, 0)).X, Vec2(1, 1) == Vec2(1, 1)").Values == Values{ "-1.0", "true" });
    CHECK_FALSE(lua.Run("Vec2('a', {})").Ok); // a script's mistake is an error, not a crash
}
