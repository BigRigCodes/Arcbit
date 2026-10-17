// Developer tools for scripts: console commands registered from Lua, the
// error handler, and hot reload of edited scripts.

#include "TestSupport.h"

#include <arcbit/app/DebugConsole.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/script/Script.h>
#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::ContainsSubstring;

namespace
{
using Lines = std::vector<std::string>;

constexpr f32 Dt = 1.0f / 60.0f;

class LambdaScene : public Scene
{
public:
    std::function<void(Scene&)> Load;
    void                        OnLoad() override { if (Load) Load(*this); }
};

struct Fixture
{
    ArcbitTests::TempDir dir;
    DebugConsole         console;
    ScriptEngine         lua{ dir.File("") };
    SceneManager         scenes;
    Lines                out, errors;
    Entity               scripted;

    Fixture()
    {
        lua.SetOutput([this](const std::string_view line) { out.emplace_back(line); });
        lua.SetErrorHandler([this](const std::string_view error) { errors.emplace_back(error); });
        lua.SetServices({ .Scenes = &scenes, .Console = &console });
        scenes.SetHost({ .Scripts = &lua });
        scenes.FadeOutTime = scenes.FadeInTime = 0.0f;
        scenes.Register("Yard", [this] {
            auto scene  = std::make_unique<LambdaScene>();
            scene->Load = [this](Scene& s) {
                scripted = s.GetWorld().CreateEntity();
                s.GetWorld().AddComponent<Script>(scripted, Script{ .Module = "subject" });
            };
            return scene;
        });
    }

    // Write the script; a later write is stamped a few seconds on, so the
    // change is seen whatever the file system's timestamp resolution.
    void Write(const char* code, const int version = 0) const
    {
        const auto path = dir.File("subject.lua");
        std::ofstream(path) << code;
        if (version > 0)
            std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(5 * version));
    }
    void Go()
    {
        scenes.Switch("Yard");
        for (int i = 0; i < 5; ++i) scenes.Update(Dt);
    }
    void Tick(const int n = 1)
    {
        for (int i = 0; i < n; ++i) scenes.GetActive()->Update(Dt);
    }
};
} // namespace

TEST_CASE("A script's console command runs its function, and ends with the script", "[script][console]")
{
    Fixture f;
    f.Write(R"(
local S = {}
function S:OnStart()
    Console.Register("heal", function(amount) print("healed " .. amount) end, "heal <n> - restore health", true)
end
return S
)");
    f.Go();
    REQUIRE(f.console.Find("heal"));
    CHECK(f.console.Find("heal")->Cheat);
    f.console.Execute("heal 25");
    CHECK(f.out == Lines{ "healed 25" });

    f.scenes.GetActive()->GetWorld().DestroyEntity(f.scripted);
    CHECK_FALSE(f.console.Find("heal"));
}

TEST_CASE("Script errors reach the error handler", "[script][console]")
{
    Fixture f;
    f.Write("local S = {} function S:OnStart() error('broken chest') end return S");
    f.Go();
    REQUIRE(f.errors.size() == 1);
    CHECK_THAT(f.errors[0], ContainsSubstring("broken chest"));
}

TEST_CASE("Hot reload swaps in new functions; instances and module data keep their state", "[script][console]")
{
    Fixture f;
    f.Write(R"(
local M = { Total = 0 }
function M:OnUpdate() M.Total = M.Total + 1 self.Count = (self.Count or 0) + 1 end
return M
)");
    f.Go();
    f.Tick(2);
    CHECK(f.lua.CheckReloads() == 0); // nothing changed

    f.Write(R"(
local M = { Total = 100 }
function M:OnUpdate() self.Count = self.Count + 1 print("v2 count " .. self.Count .. " total " .. M.Total) end
return M
)", 1);
    CHECK(f.lua.CheckReloads() == 1);
    f.out.clear();
    f.Tick();
    CHECK(f.out == Lines{ "v2 count 3 total 2" }); // the instance's count and the module's total carried over
}

TEST_CASE("Fixing a script brings back the OnUpdate its error turned off", "[script][console]")
{
    Fixture f;
    f.Write("local M = {} function M:OnUpdate() error('typo') end return M");
    f.Go();
    f.Tick(3);
    CHECK(f.errors.size() == 1); // turned off after the first

    f.Write("local M = {} function M:OnUpdate() print('fixed') end return M", 1);
    REQUIRE(f.lua.CheckReloads() == 1);
    f.out.clear();
    f.Tick();
    CHECK(f.out == Lines{ "fixed" });
}
