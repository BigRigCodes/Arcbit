// Tasks: coroutines scripts start with self:Run / Task.Run / Timer, paused
// with Wait and advanced each tick by their scene - and ending with the
// script or scene that owns them.

#include "TestSupport.h"

#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/script/Script.h>
#include <arcbit/script/ScriptEngine.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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
    ScriptEngine         lua{ dir.File(""), ScriptLimits{ .MaxInstructions = 2'000'000 } };
    SceneManager         scenes;
    Lines                out;
    Entity               scripted;

    Fixture()
    {
        lua.SetOutput([this](const std::string_view line) { out.emplace_back(line); });
        lua.SetServices({ .Scenes = &scenes });
        scenes.SetHost({ .Scripts = &lua });
        scenes.FadeOutTime = scenes.FadeInTime = 0.0f;
        scenes.Register("Empty", [] { return std::make_unique<Scene>(); });
    }

    // A scene with one entity running `code` as its script.
    void Start(const char* code)
    {
        std::ofstream(dir.File("subject.lua")) << code;
        scenes.Register("Yard", [this] {
            auto scene  = std::make_unique<LambdaScene>();
            scene->Load = [this](Scene& s) {
                scripted = s.GetWorld().CreateEntity();
                s.GetWorld().AddComponent<Script>(scripted, Script{ .Module = "subject" });
            };
            return scene;
        });
        Go("Yard");
    }
    void Go(const char* name)
    {
        scenes.Switch(name);
        for (int i = 0; i < 5; ++i) scenes.Update(Dt);
    }
    void Tick(const int n = 1)
    {
        for (int i = 0; i < n; ++i) scenes.GetActive()->Update(Dt);
    }
};
} // namespace

TEST_CASE("A task runs to its first Wait at once, then resumes once the wait is over", "[script]")
{
    Fixture f;
    f.Start(R"(
local S = {}
function S:OnStart()
    self:Run(function(name)
        print("hello " .. name)
        Wait(0.05) -- three ticks
        print("later")
        Wait()     -- the next tick
        print("done")
    end, "yard")
end
return S
)");
    CHECK(f.out == Lines{ "hello yard" });
    f.Tick(2);
    CHECK(f.out.size() == 1);
    f.Tick();
    CHECK(f.out == Lines{ "hello yard", "later" });
    f.Tick();
    CHECK(f.out.back() == "done");
}

TEST_CASE("Timers fire after a delay or every interval until stopped", "[script]")
{
    Fixture f;
    f.Start(R"(
local S = {}
function S:OnStart()
    Timer.After(0.03, function() print("after") end)
    S.Ticker = Timer.Every(0.02, function() print("every") end)
end
return S
)");
    f.Tick(6); // ticks 2, 4, 6 for every; tick 2 for after, which started first
    CHECK(f.out == Lines{ "after", "every", "every", "every" });
    CHECK(f.lua.Run("require('subject').Ticker:IsRunning()").Values == Lines{ "true" });
    REQUIRE(f.lua.Run("require('subject').Ticker:Stop()").Ok);
    f.out.clear();
    f.Tick(6);
    CHECK(f.out.empty());
}

TEST_CASE("WaitUntil checks each tick until its test passes", "[script]")
{
    Fixture f;
    f.Start(R"(
local S = { Ready = false }
function S:OnStart()
    self:Run(function() WaitUntil(function() return S.Ready end) print("ready") end)
end
return S
)");
    f.Tick(3);
    CHECK(f.out.empty());
    REQUIRE(f.lua.Run("require('subject').Ready = true").Ok);
    f.Tick();
    CHECK(f.out == Lines{ "ready" });
}

TEST_CASE("Tasks end with their entity or scene - even one destroying its own entity", "[script]")
{
    Fixture f;
    f.Start(R"(
local S = {}
function S:OnStart()
    self:Run(function()
        Wait(0.02)
        print("bye")
        self.Entity:Destroy() -- ends this very task while it runs
        print("still here, until the next yield")
        Wait(1)
        print("never")
    end)
    Timer.Every(0.01, function() print("tick") end)
end
return S
)");
    f.Tick(3); // tick 2: the task (started first) says bye and its Destroy ends the timer too
    CHECK(f.out == Lines{ "tick", "bye", "still here, until the next yield" });
    f.out.clear();
    f.Tick(80);
    CHECK(f.out.empty()); // the timer ended with the entity too

    REQUIRE(f.lua.Run("T = Task.Run(function() while true do Wait(0.01) print('console') end end)").Ok);
    f.Go("Empty"); // a console task runs in the active scene and ends with it
    f.Tick(10);
    CHECK(f.lua.Run("T:IsRunning()").Values == Lines{ "false" });
}

TEST_CASE("A failing or runaway task is stopped and logged; the others carry on", "[script]")
{
    Fixture f;
    f.Start(R"(
local S = {}
function S:OnStart()
    self:Run(function() Wait() error("boom") end)
    self:Run(function() Wait() while true do end end)
    Timer.Every(0.01, function() print("alive") end)
end
return S
)");
    f.Tick(3);
    CHECK(f.out == Lines{ "alive", "alive", "alive" });
    CHECK_THAT(f.lua.Run("Wait(1)").Error, ContainsSubstring("only works inside a task"));
}
